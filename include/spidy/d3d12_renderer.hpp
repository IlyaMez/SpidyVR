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
    void waitIdle();
    // Diagnostic RGBA8 readback; target enters and exits in RENDER_TARGET state.
    std::vector<unsigned char> readback(ID3D12Resource* target);
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
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    unsigned width_{}, height_{};
    std::size_t capacity_{};
};
void addBox(std::vector<Vertex>& out, Vec3 min, Vec3 max, Vec3 color);
void addBeam(std::vector<Vertex>& out, Vec3 a, Vec3 b, float radius, Vec3 color);
void addTrackedHand(std::vector<Vertex>& out, Pose grip, unsigned hand, float squeeze, bool webGesture);
} // namespace spidy
