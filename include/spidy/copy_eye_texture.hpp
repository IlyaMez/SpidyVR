#pragma once
#include "eye_resolution.hpp"
#include <d3d12.h>
#include <utility>

namespace spidy {
inline bool rgba8CopyTarget(const D3D12_RESOURCE_DESC& d) {
    // VDXR's sRGB swapchain exposes R8G8B8A8_TYPELESS backing resources.
    // D3D12 CopyResource accepts formats in the same typeless family.
    return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.DepthOrArraySize == 1 && d.MipLevels == 1 &&
           d.SampleDesc.Count == 1 && d.SampleDesc.Quality == 0 && validEyeSize(d.Width) &&
           validEyeSize(d.Height) &&
           (d.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
}
// Caller owns submission ordering and has validated dimensions, formats, and
// device identity. Both images return to their incoming resource states.
template <class Barrier>
void copyEyeTexture(ID3D12GraphicsCommandList* list, ID3D12Resource* source,
                    D3D12_RESOURCE_STATES sourceState, ID3D12Resource* target, Barrier barrier,
                    D3D12_RESOURCE_STATES targetState = D3D12_RESOURCE_STATE_RENDER_TARGET) {
    D3D12_RESOURCE_BARRIER src{};
    src.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    src.Transition = {source, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, sourceState,
                      D3D12_RESOURCE_STATE_COPY_SOURCE};
    D3D12_RESOURCE_BARRIER dst{};
    dst.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    dst.Transition = {target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, targetState,
                      D3D12_RESOURCE_STATE_COPY_DEST};
    if (sourceState != D3D12_RESOURCE_STATE_COPY_SOURCE)
        barrier(list, 1, &src);
    barrier(list, 1, &dst);
    list->CopyResource(target, source);
    std::swap(dst.Transition.StateBefore, dst.Transition.StateAfter);
    barrier(list, 1, &dst);
    std::swap(src.Transition.StateBefore, src.Transition.StateAfter);
    if (sourceState != D3D12_RESOURCE_STATE_COPY_SOURCE)
        barrier(list, 1, &src);
}
} // namespace spidy
