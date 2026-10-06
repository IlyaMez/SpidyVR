#pragma once
#include "math.hpp"
#include "vertex.hpp"
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <span>
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
                const Mat4& viewProjection, std::span<const Vertex> vertices, bool preserveColor = false);
    struct ViewTarget {
        ID3D12Resource* texture{};
        DXGI_FORMAT format{};
        unsigned width{}, height{};
        Mat4 viewProjection{};
    };
    // One upload, command list, and submission for a complete eye pair. When
    // asynchronous, the next reuse waits for this submission's own fence only.
    void renderViews(std::span<const ViewTarget>, std::span<const Vertex>, bool preserveColor,
                     bool waitForCompletion = true);
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

  private:
    void pipeline(DXGI_FORMAT format);
    void resources();
    void waitForSubmission();
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12DescriptorHeap> rtv_, dsv_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> pipeline_;
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
    // blit(): a full-screen triangle sampling one shader-visible descriptor.
    ComPtr<ID3D12DescriptorHeap> srv_;
    ComPtr<ID3D12RootSignature> blitRoot_;
    ComPtr<ID3D12PipelineState> blitPipeline_;
    DXGI_FORMAT blitFormat_ = DXGI_FORMAT_UNKNOWN;
};
void addBox(std::vector<Vertex>& out, Vec3 min, Vec3 max, Vec3 color);
void addBeam(std::vector<Vertex>& out, Vec3 a, Vec3 b, float radius, Vec3 color);
void addTrackedHand(std::vector<Vertex>& out, Pose grip, unsigned hand, float squeeze, bool webGesture);
} // namespace spidy
