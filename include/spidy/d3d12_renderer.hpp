#pragma once
#include "math.hpp"
#include "vertex.hpp"
#include <array>
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <span>
#include <string>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

namespace spidy {
template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
class D3D12Renderer {
  public:
    ~D3D12Renderer();
    void initialize(LUID adapter, D3D_FEATURE_LEVEL minimum);
    void initialize(ID3D12Device* device, ID3D12CommandQueue* queue);
    void render(ID3D12Resource* target, DXGI_FORMAT format, unsigned width, unsigned height,
                const Mat4& viewProjection, std::span<const Vertex> vertices, bool preserveColor = false,
                std::span<const Vertex> translucent = {});
    struct ViewTarget {
        ID3D12Resource* texture{};
        DXGI_FORMAT format{};
        unsigned width{}, height{};
        Mat4 viewProjection{};
    };
    // Slow motion's look (slow_motion.hpp), recoloured into each view's image
    // under the overlay: colour drained toward grey, a cool cast and a darker
    // rim by `amount`, and a ring of light that bends the image a little as it
    // crosses the view outward after slow motion starts or ends.
    struct Grade {
        float amount{};         // 0..1
        float ripple = -1;      // 0..1 of the ring's way out; below 0 none
        float rippleStrength{}; // 0..1
        bool entering{};        // the ring follows a start (cool) or an end (pale)
        // Each view's lens: the tangents of its field of view, left, right,
        // up and down (left and down negative).
        std::array<std::array<float, 4>, 2> tangents{};
        bool visible() const {
            return amount > .002f || (ripple >= 0 && ripple < 1 && rippleStrength > 0);
        }
    };
    // A flat picture shown on a quad in the views: the phone's screen. Its
    // vertices lie in its own plane - x right and y up in metres from its
    // middle, halfWidth and halfHeight to its edges, z out of it: a layer
    // further out covers one further in, and one drawn later one at its own
    // z. It is drawn once a frame into a texture of its own (pictureWidth x
    // pictureHeight, about four texels to a pixel of the eye's at arm's
    // length) and mipmapped, and each view samples that on `corners` (world;
    // bottom left, bottom right, top right, top left), depth-tested and
    // depth-writing with the overlay's opaque geometry. One surface, so
    // nothing on it can fight another for the depth, and no stroke of it
    // shimmers as the hand moves: the phone's screen was layers a millimetre
    // apart, lettering and all, before October 24.
    static constexpr unsigned pictureWidth = 960, pictureHeight = 2048, pictureMips = 6;
    struct Picture {
        std::span<const Vertex> vertices;
        float halfWidth{}, halfHeight{};
        std::array<Vec3, 4> corners{};
        Vec3 background{}; // what the texture is cleared to (linear RGB)
    };
    // One upload, command list, and submission for a complete eye pair. When
    // asynchronous, the next reuse waits for this submission's own fence only.
    // `translucent` is drawn after the other vertices, in its own order, each
    // vertex blended by its opacity: nearer overlay geometry hides it, but it
    // never hides anything itself (it leaves the depth buffer alone). A
    // visible `grade` recolours each preserved image before the overlay. A
    // `picture` with vertices is drawn first and shown after the opaque
    // geometry, before the translucent.
    void renderViews(std::span<const ViewTarget>, std::span<const Vertex>, bool preserveColor,
                     bool waitForCompletion = true, std::span<const Vertex> translucent = {},
                     const Grade* grade = nullptr, const Picture* picture = nullptr);
    // Scales `source` (read as `sourceView`, in PIXEL_SHADER_RESOURCE state)
    // over the whole target, which enters and leaves in RENDER_TARGET state.
    // A display-encoded source reaches an sRGB target with its stored values
    // unchanged; `linearSource` holds light values and is encoded. Asynchronous
    // like renderViews.
    void blit(ID3D12Resource* source, DXGI_FORMAT sourceView, bool linearSource, const ViewTarget& target);
    void waitIdle();
    // Diagnostic RGBA8 readback; target enters and exits in RENDER_TARGET state.
    std::vector<unsigned char> readback(ID3D12Resource* target);
    // Nonblocking diagnostic copy of an RGBA8 target that is in RENDER_TARGET
    // state (as renderViews leaves it), queued after the work already
    // submitted. Returns a ticket, or 0 when no copy was queued: a previous
    // copy is still running, or the target cannot be copied. Never throws.
    std::uint64_t capture(ID3D12Resource* target) noexcept;
    struct Captured {
        const unsigned char* pixels{};
        unsigned width{}, height{}, rowPitch{};
    };
    // The rows of `ticket` once the GPU has finished its copy. They stay
    // unchanged until the next capture() that returns a ticket.
    bool captured(std::uint64_t ticket, Captured& out) const noexcept;
    ID3D12Device* device() const {
        return device_.Get();
    }
    ID3D12CommandQueue* queue() const {
        return queue_.Get();
    }
    // Set once a frame by renderViews when preparing the picture's texture
    // and pipelines throws: what it caught, so a picture that fails to build
    // is skipped for that frame instead of ending the session. Empty when
    // nothing went wrong.
    const std::string& paintIssue() const {
        return paintIssue_;
    }

  private:
    void pipeline(DXGI_FORMAT format);
    void resources();
    void waitForSubmission();
    // blit's root signature and pipeline for a target format, and its draw
    // into list_ (open, after any copy the draw reads).
    void prepareBlit(DXGI_FORMAT targetFormat);
    void recordBlit(ID3D12Resource* source, DXGI_FORMAT sourceView, bool linearSource, const ViewTarget& target);
    // The grade's pipeline for a target format, and its pass over one view
    // into list_ (open): the image copied aside, then drawn back recoloured
    // through the render target view at `rtv`.
    void prepareGrade(DXGI_FORMAT targetFormat);
    void recordGrade(const ViewTarget& target, D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Grade& grade, unsigned view);
    // The picture's texture, depth, views and pipelines (made once; the
    // quad's again for another target format), its drawing into list_ (open,
    // its vertices `first` on in the bound vertex buffer, the texture left
    // readable), and the quad's draw into a view.
    void preparePicture(DXGI_FORMAT targetFormat);
    void recordPicture(const Picture& picture, unsigned first);
    void recordPictureQuad(const Picture& picture, const Mat4& viewProjection);
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12DescriptorHeap> rtv_, dsv_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> pipeline_, translucent_;
    ComPtr<ID3D12Resource> depth_, vertices_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_{};
    std::uint64_t fenceValue_{};
    // capture(): its own command list, and a readback buffer mapped for its lifetime.
    ComPtr<ID3D12CommandAllocator> captureAllocator_;
    ComPtr<ID3D12GraphicsCommandList> captureList_;
    ComPtr<ID3D12Resource> captureBuffer_;
    const unsigned char* captureMapped_{};
    std::uint64_t captureBytes_{}, captureFence_{}, captureTicket_{};
    Captured captureLayout_{};
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    unsigned width_{}, height_{};
    std::size_t capacity_{};
    std::string paintIssue_;
    // blit(): a full-screen triangle sampling one shader-visible descriptor.
    ComPtr<ID3D12DescriptorHeap> srv_;
    ComPtr<ID3D12RootSignature> blitRoot_;
    ComPtr<ID3D12PipelineState> blitPipeline_;
    DXGI_FORMAT blitFormat_ = DXGI_FORMAT_UNKNOWN;
    // The grade: its own descriptor, root signature and pipeline, and the
    // copy of a view's image it reads (made like the targets, reused).
    ComPtr<ID3D12DescriptorHeap> gradeHeap_;
    ComPtr<ID3D12RootSignature> gradeRoot_;
    ComPtr<ID3D12PipelineState> gradePipeline_;
    DXGI_FORMAT gradeFormat_ = DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D12Resource> gradeCopy_;
    D3D12_RESOURCE_STATES gradeCopyState_ = D3D12_RESOURCE_STATE_COPY_DEST;
    DXGI_FORMAT gradeViewFormat_ = DXGI_FORMAT_UNKNOWN;
    // The picture: its sRGB texture with its mips and its depth, a render
    // target view a mip, a shader view a mip and one of them all, and the
    // pipelines that draw it, shrink each mip from the one above, and show it.
    ComPtr<ID3D12Resource> pictureTexture_, pictureDepth_;
    ComPtr<ID3D12DescriptorHeap> pictureRtv_, pictureDsv_, pictureSrv_;
    ComPtr<ID3D12RootSignature> pictureRoot_, mipRoot_, quadRoot_;
    ComPtr<ID3D12PipelineState> picturePipeline_, mipPipeline_, quadPipeline_;
    DXGI_FORMAT quadFormat_ = DXGI_FORMAT_UNKNOWN;
};
void addBox(std::vector<Vertex>& out, Vec3 min, Vec3 max, Vec3 color);
void addBeam(std::vector<Vertex>& out, Vec3 a, Vec3 b, float radius, Vec3 color);
} // namespace spidy
