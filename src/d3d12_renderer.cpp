#include "spidy/d3d12_renderer.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <stdexcept>
#include <string>
namespace spidy {
namespace {
void hr(HRESULT result, const char* context) {
    if (FAILED(result))
        throw std::runtime_error(std::string(context) +
                                 " HRESULT=" + std::to_string(static_cast<unsigned long>(result)));
}
D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{};
    h.Type = type;
    h.CreationNodeMask = h.VisibleNodeMask = 1;
    return h;
}
void triangle(std::vector<Vertex>& v, Vec3 a, Vec3 b, Vec3 c, Vec3 color) {
    v.push_back({a, color});
    v.push_back({b, color});
    v.push_back({c, color});
}
} // namespace
D3D12Renderer::~D3D12Renderer() {
    try {
        waitIdle();
    } catch (...) {
    }
    if (fenceEvent_)
        CloseHandle(fenceEvent_);
}
void D3D12Renderer::initialize(LUID adapter, D3D_FEATURE_LEVEL minimum) {
    ComPtr<IDXGIFactory4> factory;
    hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create DXGI factory");
    ComPtr<IDXGIAdapter1> gpu;
    hr(factory->EnumAdapterByLuid(adapter, IID_PPV_ARGS(&gpu)), "Find OpenXR GPU");
    hr(D3D12CreateDevice(gpu.Get(), minimum, IID_PPV_ARGS(&device_)), "Create D3D12 device");
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr(device_->CreateCommandQueue(&q, IID_PPV_ARGS(&queue_)), "Create direct queue");
    resources();
}
void D3D12Renderer::initialize(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (!device || !queue || device_ || queue_ || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        throw std::runtime_error("Invalid overlay device or direct queue");
    ComPtr<ID3D12Device> owner;
    hr(queue->GetDevice(IID_PPV_ARGS(&owner)), "Check overlay queue device");
    if (owner.Get() != device)
        throw std::runtime_error("Overlay queue belongs to another device");
    device_ = device;
    queue_ = queue;
    resources();
}
void D3D12Renderer::resources() {
    hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)),
       "Create allocator");
    hr(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
                                  IID_PPV_ARGS(&list_)),
       "Create command list");
    hr(list_->Close(), "Close initial list");
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.NumDescriptors = 2;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtv_)), "Create RTV heap");
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    desc.NumDescriptors = 1;
    hr(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&dsv_)), "Create DSV heap");
    hr(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "Create GPU fence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
        throw std::runtime_error("CreateEvent failed");
}
void D3D12Renderer::waitIdle() {
    if (!queue_ || !fence_ || !fenceEvent_)
        return;
    hr(queue_->Signal(fence_.Get(), ++fenceValue_), "Signal GPU fence");
    waitForSubmission();
}
void D3D12Renderer::waitForSubmission() {
    if (!fence_ || !fenceEvent_ || !fenceValue_)
        return;
    if (fence_->GetCompletedValue() == UINT64_MAX)
        throw std::runtime_error("Overlay graphics device removed");
    if (fence_->GetCompletedValue() < fenceValue_) {
        hr(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_), "Arm GPU fence");
        if (WaitForSingleObject(fenceEvent_, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU fence timed out");
    }
}
std::vector<unsigned char> D3D12Renderer::readback(ID3D12Resource* target) {
    waitIdle();
    const auto desc = target->GetDesc();
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        desc.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS)
        throw std::runtime_error("Readback requires RGBA8");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows{};
    UINT64 rowBytes{}, total{};
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = total;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto h = heap(D3D12_HEAP_TYPE_READBACK);
    ComPtr<ID3D12Resource> readback;
    hr(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                        nullptr, IID_PPV_ARGS(&readback)),
       "Create diagnostic readback");
    hr(allocator_->Reset(), "Reset readback allocator");
    hr(list_->Reset(allocator_.Get(), nullptr), "Reset readback list");
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list_->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = target;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list_->ResourceBarrier(1, &barrier);
    hr(list_->Close(), "Close readback list");
    ID3D12CommandList* lists[] = {list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    waitIdle();
    void* mapped{};
    D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    hr(readback->Map(0, &range, &mapped), "Map readback");
    std::vector<unsigned char> pixels(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + y * desc.Width * 4,
                    static_cast<unsigned char*>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch,
                    static_cast<size_t>(desc.Width) * 4);
    D3D12_RANGE written{0, 0};
    readback->Unmap(0, &written);
    return pixels;
}
std::uint64_t D3D12Renderer::capture(ID3D12Resource* target) noexcept {
    if (!target || !device_ || !queue_ || !fence_)
        return 0;
    const auto completed = fence_->GetCompletedValue();
    if (completed == UINT64_MAX || completed < captureFence_)
        return 0; // device removed, or the previous copy still writes the buffer
    const auto desc = target->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
        (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
         desc.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS))
        return 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 total{};
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    if (!total || total == UINT64_MAX)
        return 0;
    if (!captureBuffer_ || captureBytes_ < total) {
        captureBuffer_.Reset();
        captureMapped_ = nullptr;
        captureBytes_ = 0;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = total;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto h = heap(D3D12_HEAP_TYPE_READBACK);
        ComPtr<ID3D12Resource> created;
        void* mapped{};
        if (FAILED(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &buffer,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&created))) ||
            FAILED(created->Map(0, nullptr, &mapped)) || !mapped)
            return 0;
        captureBuffer_ = created;
        captureMapped_ = static_cast<const unsigned char*>(mapped);
        captureBytes_ = total;
    }
    if (!captureList_) {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
            FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&list))) ||
            FAILED(list->Close()))
            return 0;
        captureAllocator_ = allocator;
        captureList_ = list;
    }
    if (FAILED(captureAllocator_->Reset()) || FAILED(captureList_->Reset(captureAllocator_.Get(), nullptr)))
        return 0;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    captureList_->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = target;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = captureBuffer_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    captureList_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    captureList_->ResourceBarrier(1, &barrier);
    if (FAILED(captureList_->Close()))
        return 0;
    ID3D12CommandList* lists[] = {captureList_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    // The fence value is shared with renderViews, which waits for the newest.
    if (FAILED(queue_->Signal(fence_.Get(), fenceValue_ + 1)))
        return 0;
    captureFence_ = ++fenceValue_;
    captureLayout_ = {captureMapped_ + footprint.Offset, static_cast<unsigned>(desc.Width), desc.Height,
                      footprint.Footprint.RowPitch};
    return ++captureTicket_;
}
bool D3D12Renderer::captured(std::uint64_t ticket, Captured& out) const noexcept {
    if (!ticket || ticket != captureTicket_ || !fence_ || !captureMapped_)
        return false;
    const auto completed = fence_->GetCompletedValue();
    if (completed == UINT64_MAX || completed < captureFence_)
        return false;
    out = captureLayout_;
    return true;
}
void D3D12Renderer::pipeline(DXGI_FORMAT format) {
    if (pipeline_ && format == format_)
        return;
    const char* shader = R"(
cbuffer Eye : register(b0) { row_major float4x4 vp; };
struct In { float3 position:POSITION; float4 color:COLOR; };
struct Out { float4 position:SV_POSITION; float4 color:COLOR; };
Out vs(In i) { Out o; o.position=mul(vp,float4(i.position,1)); o.color=i.color; return o; }
float4 ps(Out i):SV_TARGET { return i.color; }
)";
    ComPtr<ID3DBlob> vs, ps, error;
    hr(D3DCompile(shader, std::strlen(shader), "spidy_lab", nullptr, nullptr, "vs", "vs_5_0",
                  D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs, &error),
       "Compile vertex shader");
    hr(D3DCompile(shader, std::strlen(shader), "spidy_lab", nullptr, nullptr, "ps", "ps_5_0",
                  D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps, &error),
       "Compile pixel shader");
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.Num32BitValues = 16;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 1;
    rs.pParameters = &parameter;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature;
    hr(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error),
       "Serialize root signature");
    root_.Reset();
    hr(device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                    IID_PPV_ARGS(&root_)),
       "Create root signature");
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    static_assert(sizeof(Vertex) == 28, "the overlay's input layout reads position, colour and opacity");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = root_.Get();
    p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    p.InputLayout = {layout, 2};
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets = 1;
    p.RTVFormats[0] = format;
    p.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    p.SampleDesc.Count = 1;
    p.SampleMask = UINT_MAX;
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable = TRUE;
    // A vertex's opacity blends it over the image (soft marker edges); at 1,
    // as nearly everything is drawn, it replaces the pixel as before.
    auto& blend = p.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    p.DepthStencilState.DepthEnable = TRUE;
    p.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    p.DepthStencilState.FrontFace.StencilFailOp = p.DepthStencilState.FrontFace.StencilDepthFailOp =
        p.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    p.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    p.DepthStencilState.BackFace = p.DepthStencilState.FrontFace;
    pipeline_.Reset();
    hr(device_->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pipeline_)), "Create graphics pipeline");
    // Translucent geometry is tested against the depth, never written to it.
    p.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    translucent_.Reset();
    hr(device_->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&translucent_)), "Create translucent pipeline");
    format_ = format;
}
void D3D12Renderer::render(ID3D12Resource* target, DXGI_FORMAT format, unsigned width, unsigned height,
                           const Mat4& vp, std::span<const Vertex> vertices, bool preserveColor,
                           std::span<const Vertex> translucent) {
    const ViewTarget view{target, format, width, height, vp};
    renderViews(std::span(&view, 1), vertices, preserveColor, true, translucent);
}
void D3D12Renderer::renderViews(std::span<const ViewTarget> views, std::span<const Vertex> vertices,
                                bool preserveColor, bool waitForCompletion,
                                std::span<const Vertex> translucent, const Grade* grade) {
    if (views.empty() || views.size() > 2)
        throw std::runtime_error("Overlay requires one or two views");
    const auto format = views[0].format;
    const auto width = views[0].width, height = views[0].height;
    for (const auto& view : views)
        if (!view.texture || !width || !height || view.width != width || view.height != height ||
            view.format != format)
            throw std::runtime_error("Overlay view dimensions or formats differ");
    waitForSubmission();
    pipeline(format);
    // Only an image that stays can be recoloured.
    const bool grading = grade && preserveColor && grade->visible();
    if (grading)
        prepareGrade(format);
    if (!depth_ || width != width_ || height != height_) {
        depth_.Reset();
        width_ = width;
        height_ = height;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = width;
        d.Height = height;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_D32_FLOAT;
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = d.Format;
        clear.DepthStencil.Depth = 1;
        auto h = heap(D3D12_HEAP_TYPE_DEFAULT);
        hr(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                            &clear, IID_PPV_ARGS(&depth_)),
           "Create depth target");
        device_->CreateDepthStencilView(depth_.Get(), nullptr, dsv_->GetCPUDescriptorHandleForHeapStart());
    }
    // One buffer: the depth-writing vertices, then the translucent ones.
    const auto bytes = vertices.size_bytes() + translucent.size_bytes();
    if (bytes > capacity_) {
        vertices_.Reset();
        capacity_ = std::max<std::size_t>(bytes, 1024 * 1024);
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = capacity_;
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto h = heap(D3D12_HEAP_TYPE_UPLOAD);
        hr(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ,
                                            nullptr, IID_PPV_ARGS(&vertices_)),
           "Create vertex upload buffer");
    }
    if (bytes) {
        void* data{};
        D3D12_RANGE read{0, 0};
        hr(vertices_->Map(0, &read, &data), "Map vertices");
        if (!vertices.empty())
            std::memcpy(data, vertices.data(), vertices.size_bytes());
        if (!translucent.empty())
            std::memcpy(static_cast<char*>(data) + vertices.size_bytes(), translucent.data(),
                        translucent.size_bytes());
        vertices_->Unmap(0, nullptr);
    }
    hr(allocator_->Reset(), "Reset allocator");
    hr(list_->Reset(allocator_.Get(), pipeline_.Get()), "Reset list");
    D3D12_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = format;
    rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto dsv = dsv_->GetCPUDescriptorHandleForHeapStart();
    const auto rtvStride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (size_t eye = 0; eye < views.size(); ++eye) {
        auto rtv = rtv_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += eye * rtvStride;
        device_->CreateRenderTargetView(views[eye].texture, &rv, rtv);
        if (grading)
            recordGrade(views[eye], rtv, *grade, static_cast<unsigned>(eye));
        list_->SetGraphicsRootSignature(root_.Get());
        list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (bytes) {
            D3D12_VERTEX_BUFFER_VIEW buffer{vertices_->GetGPUVirtualAddress(), static_cast<UINT>(bytes),
                                            sizeof(Vertex)};
            list_->IASetVertexBuffers(0, 1, &buffer);
        }
        // XR_KHR_D3D12_enable requires color images in RENDER_TARGET state on
        // acquire/release. We render directly and leave them in that state.
        const float clear[] = {.025f, .045f, .08f, 1};
        if (!preserveColor)
            list_->ClearRenderTargetView(rtv, clear, 0, nullptr);
        list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
        list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        D3D12_RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        list_->RSSetViewports(1, &viewport);
        list_->RSSetScissorRects(1, &rect);
        list_->SetGraphicsRoot32BitConstants(0, 16, views[eye].viewProjection.data(), 0);
        if (!vertices.empty()) {
            list_->SetPipelineState(pipeline_.Get());
            list_->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
        }
        if (!translucent.empty()) {
            list_->SetPipelineState(translucent_.Get());
            list_->DrawInstanced(static_cast<UINT>(translucent.size()), 1, static_cast<UINT>(vertices.size()), 0);
        }
    }
    hr(list_->Close(), "Close render list");
    ID3D12CommandList* lists[] = {list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    hr(queue_->Signal(fence_.Get(), ++fenceValue_), "Signal overlay completion");
    if (waitForCompletion)
        waitForSubmission();
}
namespace {
bool srgb(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
           format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}
} // namespace
void D3D12Renderer::prepareBlit(DXGI_FORMAT targetFormat) {
    if (!blitRoot_) {
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = {1, &range};
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants.Num32BitValues = 4;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rs{2, parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> signature, error;
        hr(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error),
           "Serialize blit root signature");
        hr(device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                        IID_PPV_ARGS(&blitRoot_)),
           "Create blit root signature");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 1;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srv_)), "Create blit descriptor heap");
    }
    if (!blitPipeline_ || blitFormat_ != targetFormat) {
        // An sRGB target encodes what the pixel shader returns, so display-
        // encoded input is decoded first and stored values come out unchanged.
        const char* shader = R"(
Texture2D source : register(t0);
SamplerState scaled : register(s0);
cbuffer Mode : register(b0) { uint linearSource; uint srgbTarget; float2 targetPixel; };
float4 vs(uint id : SV_VertexID) : SV_POSITION {
    const float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
}
float3 decode(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 encode(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055; }
float4 ps(float4 position : SV_POSITION) : SV_TARGET {
    // From the pixel's own centre: a coordinate interpolated across the
    // screen-covering triangle drifts off the source's texel centres on
    // large targets (4608 x 4896 eyes changed copied values by 2 levels).
    float3 c = source.SampleLevel(scaled, position.xy * targetPixel, 0).rgb;
    if (linearSource)
        c = srgbTarget ? max(c, 0) : encode(saturate(c));
    else if (srgbTarget)
        c = decode(saturate(c));
    return float4(c, 1);
}
)";
        ComPtr<ID3DBlob> vs, ps, error;
        hr(D3DCompile(shader, std::strlen(shader), "spidy_blit", nullptr, nullptr, "vs", "vs_5_0",
                      D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs, &error),
           "Compile blit vertex shader");
        hr(D3DCompile(shader, std::strlen(shader), "spidy_blit", nullptr, nullptr, "ps", "ps_5_0",
                      D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps, &error),
           "Compile blit pixel shader");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
        p.pRootSignature = blitRoot_.Get();
        p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.NumRenderTargets = 1;
        p.RTVFormats[0] = targetFormat;
        p.SampleDesc.Count = 1;
        p.SampleMask = UINT_MAX;
        p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        p.RasterizerState.DepthClipEnable = TRUE;
        auto& blend = p.BlendState.RenderTarget[0];
        blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        blitPipeline_.Reset();
        hr(device_->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&blitPipeline_)), "Create blit pipeline");
        blitFormat_ = targetFormat;
    }
}
void D3D12Renderer::recordBlit(ID3D12Resource* source, DXGI_FORMAT sourceView, bool linearSource,
                               const ViewTarget& target) {
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = sourceView;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(source, &sv, srv_->GetCPUDescriptorHandleForHeapStart());
    D3D12_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = target.format;
    rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto rtv = rtv_->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(target.texture, &rv, rtv);
    list_->SetPipelineState(blitPipeline_.Get());
    list_->SetGraphicsRootSignature(blitRoot_.Get());
    ID3D12DescriptorHeap* heaps[] = {srv_.Get()};
    list_->SetDescriptorHeaps(1, heaps);
    list_->SetGraphicsRootDescriptorTable(0, srv_->GetGPUDescriptorHandleForHeapStart());
    const UINT mode[] = {linearSource ? 1u : 0u, srgb(target.format) ? 1u : 0u,
                         std::bit_cast<UINT>(1.f / static_cast<float>(target.width)),
                         std::bit_cast<UINT>(1.f / static_cast<float>(target.height))};
    list_->SetGraphicsRoot32BitConstants(1, 4, mode, 0);
    list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(target.width), static_cast<float>(target.height), 0, 1};
    D3D12_RECT rect{0, 0, static_cast<LONG>(target.width), static_cast<LONG>(target.height)};
    list_->RSSetViewports(1, &viewport);
    list_->RSSetScissorRects(1, &rect);
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list_->DrawInstanced(3, 1, 0, 0);
}
void D3D12Renderer::blit(ID3D12Resource* source, DXGI_FORMAT sourceView, bool linearSource, const ViewTarget& target) {
    if (!source || !target.texture || !target.width || !target.height)
        throw std::runtime_error("Blit requires a source and a target");
    waitForSubmission();
    prepareBlit(target.format);
    hr(allocator_->Reset(), "Reset blit allocator");
    hr(list_->Reset(allocator_.Get(), blitPipeline_.Get()), "Reset blit list");
    recordBlit(source, sourceView, linearSource, target);
    hr(list_->Close(), "Close blit list");
    ID3D12CommandList* lists[] = {list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    hr(queue_->Signal(fence_.Get(), ++fenceValue_), "Signal blit completion");
}
namespace {
bool typeless(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R8G8B8A8_TYPELESS || format == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
           format == DXGI_FORMAT_B8G8R8X8_TYPELESS;
}
// The typeless format of a format's family: a copy in it takes any of them.
DXGI_FORMAT family(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_TYPELESS;
    default:
        return format;
    }
}
D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}
} // namespace
void D3D12Renderer::prepareGrade(DXGI_FORMAT targetFormat) {
    if (!gradeRoot_) {
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = {1, &range};
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants.Num32BitValues = 12;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rs{2, parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> signature, error;
        hr(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error),
           "Serialize grade root signature");
        hr(device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                        IID_PPV_ARGS(&gradeRoot_)),
           "Create grade root signature");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 1;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&gradeHeap_)), "Create grade descriptor heap");
    }
    if (gradePipeline_ && gradeFormat_ == targetFormat)
        return;
    // Light values throughout: an sRGB view decodes what it reads and encodes
    // what it writes; through a plain one the shader does it.
    const char* shader = R"(
Texture2D source : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Grade : register(b0) {
    float4 lens; // tangents of the view: left, right, up, down
    float2 pixel;
    float amount, ripple, rippleStrength, entering;
    uint decodeSource, encodeTarget;
};
float4 vs(uint id : SV_VertexID) : SV_POSITION {
    const float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
}
float3 decode(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 encode(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055; }
float3 light(float2 uv) {
    const float3 c = source.SampleLevel(linearClamp, uv, 0).rgb;
    return decodeSource ? decode(saturate(c)) : c;
}
float4 ps(float4 position : SV_POSITION) : SV_TARGET {
    const float2 uv = position.xy * pixel;
    // The pixel's direction from the eye, in tangents right and up of its axis:
    // the rim and the ring are round about the lens, whatever the image's shape.
    const float2 t = float2(lerp(lens.x, lens.y, uv.x), lerp(lens.z, lens.w, uv.y));
    const float r = length(t);
    float3 c;
    float ring = 0;
    if (ripple >= 0 && ripple < 1) {
        // Out from the middle of the view to past its edge, slowing as it goes.
        const float travelled = 1 - (1 - ripple) * (1 - ripple);
        const float d = (r - lerp(.05, 2.0, travelled)) / (.08 + .22 * ripple);
        ring = exp(-d * d) * (1 - ripple) * rippleStrength;
        // The image under it bends outward, under a degree at its crest, red a
        // little more than blue.
        const float2 along = r > 1e-4 ? t / r : float2(0, 0);
        const float2 shift = along * ring * .014 / float2(lens.y - lens.x, lens.w - lens.z);
        c = float3(light(uv - shift * 1.35).r, light(uv - shift).g, light(uv - shift * .65).b);
    } else {
        c = light(uv);
    }
    // Drained toward grey, cooler, darker toward the rim.
    const float luma = dot(c, float3(.2126, .7152, .0722));
    float3 graded = lerp(c, luma.xxx, .55 * amount);
    graded *= lerp(float3(1, 1, 1), float3(.84, .96, 1.18), amount);
    graded *= 1 - .55 * amount * smoothstep(.5, 1.7, r);
    graded += ring * (entering > .5 ? float3(.08, .2, .3) : float3(.2, .22, .24));
    if (encodeTarget)
        graded = encode(saturate(graded));
    return float4(graded, 1);
}
)";
    ComPtr<ID3DBlob> vs, ps;
    const auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
        ComPtr<ID3DBlob> error;
        const auto result = D3DCompile(shader, std::strlen(shader), "spidy_grade", nullptr, nullptr, entry,
                                       target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &out, &error);
        if (FAILED(result) && error)
            throw std::runtime_error(std::string("Compile grade shader: ") +
                                     static_cast<const char*>(error->GetBufferPointer()));
        hr(result, "Compile grade shader");
    };
    compile("vs", "vs_5_0", vs);
    compile("ps", "ps_5_0", ps);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = gradeRoot_.Get();
    p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets = 1;
    p.RTVFormats[0] = targetFormat;
    p.SampleDesc.Count = 1;
    p.SampleMask = UINT_MAX;
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable = TRUE;
    auto& blend = p.BlendState.RenderTarget[0];
    blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    gradePipeline_.Reset();
    hr(device_->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&gradePipeline_)), "Create grade pipeline");
    gradeFormat_ = targetFormat;
}
void D3D12Renderer::recordGrade(const ViewTarget& target, D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Grade& grade,
                                unsigned view) {
    const auto desc = target.texture->GetDesc();
    // A typeless image is read through the target's own view; a typed one as it is.
    const DXGI_FORMAT viewFormat = typeless(desc.Format) ? target.format : desc.Format;
    // One copy serves both views, typed or typeless (the views share a size
    // and a view format), so it never changes while a list records with it.
    bool fresh = !gradeCopy_ || gradeViewFormat_ != viewFormat;
    if (!fresh) {
        const auto copy = gradeCopy_->GetDesc();
        fresh = copy.Width != desc.Width || copy.Height != desc.Height || copy.Format != family(desc.Format) ||
                copy.DepthOrArraySize != desc.DepthOrArraySize || copy.MipLevels != desc.MipLevels;
    }
    if (fresh) {
        gradeCopy_.Reset();
        auto d = desc;
        d.Format = family(desc.Format);
        d.Alignment = 0;
        d.Flags = D3D12_RESOURCE_FLAG_NONE;
        auto h = heap(D3D12_HEAP_TYPE_DEFAULT);
        hr(device_->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST,
                                            nullptr, IID_PPV_ARGS(&gradeCopy_)),
           "Create grade copy");
        gradeCopyState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = viewFormat;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(gradeCopy_.Get(), &sv,
                                          gradeHeap_->GetCPUDescriptorHandleForHeapStart());
        gradeViewFormat_ = viewFormat;
    }
    // The image aside (the copy of the view before waits for its draw).
    D3D12_RESOURCE_BARRIER barriers[2] = {
        transition(target.texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
        transition(gradeCopy_.Get(), gradeCopyState_, D3D12_RESOURCE_STATE_COPY_DEST)};
    list_->ResourceBarrier(gradeCopyState_ == D3D12_RESOURCE_STATE_COPY_DEST ? 1 : 2, barriers);
    list_->CopyResource(gradeCopy_.Get(), target.texture);
    barriers[0] =
        transition(target.texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[1] = transition(gradeCopy_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    list_->ResourceBarrier(2, barriers);
    gradeCopyState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const auto& lens = grade.tangents[view < 2 ? view : 0];
    const auto value = [](float v, float lo, float hi) {
        return std::bit_cast<UINT>(std::isfinite(v) ? std::clamp(v, lo, hi) : lo);
    };
    const UINT constants[12] = {value(lens[0], -8, 8),
                                value(lens[1], -8, 8),
                                value(lens[2], -8, 8),
                                value(lens[3], -8, 8),
                                std::bit_cast<UINT>(1.f / static_cast<float>(target.width)),
                                std::bit_cast<UINT>(1.f / static_cast<float>(target.height)),
                                value(grade.amount, 0, 1),
                                value(grade.ripple, -1, 1),
                                value(grade.rippleStrength, 0, 1),
                                std::bit_cast<UINT>(grade.entering ? 1.f : 0.f),
                                srgb(viewFormat) ? 0u : 1u,
                                srgb(target.format) ? 0u : 1u};
    list_->SetPipelineState(gradePipeline_.Get());
    list_->SetGraphicsRootSignature(gradeRoot_.Get());
    ID3D12DescriptorHeap* heaps[] = {gradeHeap_.Get()};
    list_->SetDescriptorHeaps(1, heaps);
    list_->SetGraphicsRootDescriptorTable(0, gradeHeap_->GetGPUDescriptorHandleForHeapStart());
    list_->SetGraphicsRoot32BitConstants(1, 12, constants, 0);
    list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(target.width), static_cast<float>(target.height), 0, 1};
    D3D12_RECT rect{0, 0, static_cast<LONG>(target.width), static_cast<LONG>(target.height)};
    list_->RSSetViewports(1, &viewport);
    list_->RSSetScissorRects(1, &rect);
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list_->DrawInstanced(3, 1, 0, 0);
}
void addBox(std::vector<Vertex>& out, Vec3 lo, Vec3 hi, Vec3 color) {
    Vec3 p[] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    const int faces[][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
                            {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    for (int f = 0; f < 6; ++f) {
        const auto& v = faces[f];
        const auto c = color * (.65f + .07f * f);
        triangle(out, p[v[0]], p[v[1]], p[v[2]], c);
        triangle(out, p[v[0]], p[v[2]], p[v[3]], c);
    }
}
void addBeam(std::vector<Vertex>& out, Vec3 a, Vec3 b, float r, Vec3 color) {
    const Vec3 d = normalized(b - a);
    if (length(d) < .1f)
        return;
    const Vec3 u = normalized(cross(d, std::abs(d.y) > .95f ? Vec3{1, 0, 0} : Vec3{0, 1, 0})) * r,
               v = normalized(cross(d, u)) * r;
    Vec3 ring[] = {u + v, -u + v, -u - v, u - v};
    for (int i = 0; i < 4; ++i) {
        const auto j = (i + 1) % 4;
        triangle(out, a + ring[i], b + ring[i], b + ring[j], color);
        triangle(out, a + ring[i], b + ring[j], a + ring[j], color);
    }
}
void addTrackedHand(std::vector<Vertex>& out, Pose grip, unsigned hand, float squeeze, bool webGesture) {
    const size_t begin = out.size();
    const Vec3 red{.65f, .025f, .035f}, seam{.025f, .015f, .02f}, cuff{.035f, .06f, .24f};
    addBox(out, {-.041f, -.02f, -.05f}, {.041f, .02f, .042f}, red);
    addBox(out, {-.035f, -.024f, .035f}, {.035f, .024f, .09f}, cuff);
    for (int finger = 0; finger < 4; ++finger) {
        const float x = -.031f + finger * .021f;
        const float bend = webGesture && (finger == 0 || finger == 3) ? 0.f : std::clamp(squeeze, 0.f, 1.f);
        const float size = (finger == 3 ? .017f : .022f);
        Vec3 from{x, 0, -.049f};
        for (int joint = 0; joint < 3; ++joint) {
            const float angle = bend * (.45f + joint * .8f);
            const Vec3 to = from + Vec3{0, -std::sin(angle) * size, -std::cos(angle) * size};
            addBeam(out, from, to, .0085f, red);
            addBox(out, to - Vec3{.0087f, .0015f, .0015f}, to + Vec3{.0087f, .0015f, .0015f}, seam);
            from = to;
        }
    }
    const float side = hand ? -1.f : 1.f;
    addBeam(out, {side * .035f, -.005f, .025f}, {side * .065f, -.012f, -.007f}, .011f, red);
    addBeam(out, {side * .065f, -.012f, -.007f}, {side * .061f, -.018f, -.036f}, .009f, red);
    for (int line = -1; line <= 1; ++line)
        addBeam(out, {line * .022f, .0205f, .035f}, {line * .022f, .0205f, -.045f}, .0009f, seam);
    for (size_t i = begin; i < out.size(); ++i)
        out[i].position = grip.position + grip.orientation.rotate(out[i].position);
}
} // namespace spidy
