// Inspect runtime-owned D3D12 images without beginning a frame or touching the game.
#include "spidy/xr_session.hpp"
#include <iostream>
#include <stdexcept>
using namespace spidy;
int main() {
    try {
        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            throw std::runtime_error("DXGI factory failed");
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(0, &adapter)))
            throw std::runtime_error("DXGI adapter failed");
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)))
            throw std::runtime_error("DXGI description failed");
        D3D12Renderer renderer;
        renderer.initialize(desc.AdapterLuid, D3D_FEATURE_LEVEL_11_0);
        XrRuntime runtime;
        runtime.initialize(renderer.device(), renderer.queue(), 512);
        std::cout << "Swapchain inspection complete. No frames were submitted.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
