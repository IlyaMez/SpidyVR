#include "spidy/copy_eye_texture.hpp"
#include "spidy/d3d12_renderer.hpp"
#include "spidy/lab_world.hpp"
#include "spidy/web_visual.hpp"
#include <cstring>
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace spidy;
void require(HRESULT r) {
    if (FAILED(r))
        throw std::runtime_error("D3D12 smoke test HRESULT=" + std::to_string(static_cast<unsigned long>(r)));
}
void saveBmp(const std::filesystem::path& file, const std::vector<unsigned char>& rgba, unsigned width,
             unsigned height) {
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    fh.bfType = 0x4d42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + width * height * 4;
    ih.biSize = sizeof(ih);
    ih.biWidth = width;
    ih.biHeight = -static_cast<LONG>(height);
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    auto bgra = rgba;
    for (size_t i = 0; i < bgra.size(); i += 4)
        std::swap(bgra[i], bgra[i + 2]);
    std::ofstream f(file, std::ios::binary);
    f.write(reinterpret_cast<char*>(&fh), sizeof(fh));
    f.write(reinterpret_cast<char*>(&ih), sizeof(ih));
    f.write(reinterpret_cast<const char*>(bgra.data()), bgra.size());
    if (!f)
        throw std::runtime_error("Failed to save diagnostic image");
}
int main(int argc, char** argv) {
    try {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;
        require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter1> gpu;
        require(factory->EnumAdapters1(0, &gpu));
        DXGI_ADAPTER_DESC1 gpuDesc{};
        require(gpu->GetDesc1(&gpuDesc));
        D3D12Renderer renderer;
        renderer.initialize(gpuDesc.AdapterLuid, D3D_FEATURE_LEVEL_11_0);
        ComPtr<ID3D12InfoQueue> diagnostics;
        renderer.device()->QueryInterface(IID_PPV_ARGS(&diagnostics));
        const unsigned width = argc > 2 ? std::stoul(argv[2]) : 960;
        const unsigned height = argc > 3 ? std::stoul(argv[3]) : argc > 2 ? width : 720;
        if (!validEyeSize(width) || !validEyeSize(height))
            throw std::runtime_error("Unsupported diagnostic resolution");
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = width;
        td.Height = height;
        td.DepthOrArraySize = 1;
        td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        td.SampleDesc.Count = 1;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        for (auto invalid :
             {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_TYPELESS}) {
            auto incompatible = td;
            incompatible.Format = invalid;
            if (rgba8CopyTarget(incompatible))
                throw std::runtime_error("Incompatible eye format accepted");
        }
        auto layered = td;
        layered.DepthOrArraySize = 2;
        if (rgba8CopyTarget(layered))
            throw std::runtime_error("Array eye copy accepted as a single texture");
        auto oversized = td;
        oversized.Width = maximumEyeSize + 1;
        if (rgba8CopyTarget(oversized))
            throw std::runtime_error("Unbounded eye dimensions accepted");
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        std::array<ComPtr<ID3D12Resource>, 2> targets, copied;
        for (unsigned i = 0; i < 2; ++i) {
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            require(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td,
                                                               D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                               IID_PPV_ARGS(&targets[i])));
            // Exercise both typed sRGB and the actual VDXR typeless backing format.
            td.Format = i ? DXGI_FORMAT_R8G8B8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            if (!rgba8CopyTarget(td))
                throw std::runtime_error("Compatible eye texture rejected");
            require(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td,
                                                               D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                               IID_PPV_ARGS(&copied[i])));
        }
        LabWorld world;
        std::vector<Vertex> vertices;
        for (const auto& box : world.boxes())
            addBox(vertices, box.min, box.max, box.color);
        addBox(vertices, {-.08f, 18, 17}, {.08f, 20, 17.16f}, {1, .05f, .05f});
        addBeam(vertices, {.3f, 19.2f, 18.5f}, {17, 30, -5}, .04f, {.85f, .92f, 1});
        std::array<std::vector<unsigned char>, 2> pixels;
        for (int eye = 0; eye < 2; ++eye) {
            Pose camera{{eye ? .032f : -.032f, 19.7f, 19}, {}};
            auto vp = multiply(projection(-.8f, .8f, -.65f, .65f), viewMatrix(camera));
            renderer.render(targets[eye].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, width, height, vp, vertices);
            pixels[eye] = renderer.readback(targets[eye].Get());
        }
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        require(renderer.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                          IID_PPV_ARGS(&allocator)));
        require(renderer.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                                     nullptr, IID_PPV_ARGS(&list)));
        for (unsigned i = 0; i < 2; ++i)
            copyEyeTexture(list.Get(), targets[i].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, copied[i].Get(),
                           [](ID3D12GraphicsCommandList* cmd, UINT n, const D3D12_RESOURCE_BARRIER* b) {
                               cmd->ResourceBarrier(n, b);
                           });
        require(list->Close());
        ID3D12CommandList* cmd = list.Get();
        renderer.queue()->ExecuteCommandLists(1, &cmd);
        renderer.waitIdle();
        for (unsigned i = 0; i < 2; ++i) {
            if (renderer.readback(copied[i].Get()) != pixels[i] ||
                renderer.readback(targets[i].Get()) != pixels[i])
                throw std::runtime_error("Paired eye copy changed pixels or source images");
        }
        // Queue six changing native pairs through the same staging textures.
        // Each pair is read twice before the next overwrite, without a CPU drain.
        // Keeping separate output textures lets us check every queued result.
        {
            std::array<ComPtr<ID3D12Resource>, 2> staging;
            auto stageDesc = targets[0]->GetDesc();
            stageDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            for (auto& image : staging)
                require(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &stageDesc,
                                                                   D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                                                                   IID_PPV_ARGS(&image)));
            struct Batch {
                ComPtr<ID3D12CommandAllocator> allocator;
                ComPtr<ID3D12GraphicsCommandList> commands;
                std::array<ComPtr<ID3D12Resource>, 2> outputs;
            };
            std::array<Batch, 12> batches;
            auto barriers = [](ID3D12GraphicsCommandList* list, UINT n, const D3D12_RESOURCE_BARRIER* b) {
                list->ResourceBarrier(n, b);
            };
            for (unsigned n = 0; n < batches.size(); ++n) {
                auto& batch = batches[n];
                require(renderer.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                  IID_PPV_ARGS(&batch.allocator)));
                require(renderer.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                             batch.allocator.Get(), nullptr,
                                                             IID_PPV_ARGS(&batch.commands)));
                for (unsigned eye = 0; eye < 2; ++eye) {
                    const auto outputDesc = copied[eye]->GetDesc();
                    require(renderer.device()->CreateCommittedResource(
                        &heap, D3D12_HEAP_FLAG_NONE, &outputDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                        IID_PPV_ARGS(&batch.outputs[eye])));
                    if (n % 2 == 0)
                        copyEyeTexture(batch.commands.Get(), targets[(eye + n / 2) % 2].Get(),
                                       D3D12_RESOURCE_STATE_RENDER_TARGET, staging[eye].Get(), barriers,
                                       D3D12_RESOURCE_STATE_COPY_SOURCE);
                    copyEyeTexture(batch.commands.Get(), staging[eye].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                   batch.outputs[eye].Get(), barriers);
                }
                require(batch.commands->Close());
                ID3D12CommandList* commands = batch.commands.Get();
                renderer.queue()->ExecuteCommandLists(1, &commands);
            }
            renderer.waitIdle();
            for (unsigned n = 0; n < batches.size(); ++n)
                for (unsigned eye = 0; eye < 2; ++eye)
                    if (renderer.readback(batches[n].outputs[eye].Get()) != pixels[(eye + n / 2) % 2])
                        throw std::runtime_error("Staged image reused a newer generation or mixed eyes");
        }
        size_t changed = 0, redPixels = 0;
        double xsum[2]{};
        size_t counts[2]{};
        for (size_t p = 0; p < pixels[0].size(); p += 4) {
            if (pixels[0][p] != pixels[1][p] || pixels[0][p + 1] != pixels[1][p + 1] ||
                pixels[0][p + 2] != pixels[1][p + 2])
                ++changed;
            for (int eye = 0; eye < 2; ++eye) {
                if (pixels[eye][p] > 180 && pixels[eye][p] > pixels[eye][p + 1] * 1.5) {
                    xsum[eye] += static_cast<double>((p / 4) % width);
                    ++counts[eye];
                }
            }
        }
        redPixels = counts[0] + counts[1];
        if (changed < 500 || redPixels < 100 || counts[0] == 0 || counts[1] == 0)
            throw std::runtime_error("Stereo output missing geometry or disparity");
        const double disparity = xsum[0] / counts[0] - xsum[1] / counts[1];
        if (disparity < 2)
            throw std::runtime_error("Eye parallax is reversed or missing");
        D3D12Renderer overlay;
        overlay.initialize(renderer.device(), renderer.queue());
        std::vector<Vertex> hands;
        addTrackedHand(hands, {{-.15f, 19.5f, 18.5f}, {}}, 0, 1, true);
        addTrackedHand(hands, {{.15f, 19.5f, 18.5f}, {}}, 1, .3f, false);
        std::array<std::vector<unsigned char>, 2> overlayPixels;
        std::array<D3D12Renderer::ViewTarget, 2> pairedViews;
        for (unsigned eye = 0; eye < 2; ++eye) {
            const Pose camera{{eye ? .032f : -.032f, 19.7f, 19}, {}};
            const auto vp = multiply(projection(-.8f, .8f, -.65f, .65f), viewMatrix(camera));
            pairedViews[eye] = {copied[eye].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, vp};
            overlay.render(copied[eye].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, vp, {}, true);
            if (overlay.readback(copied[eye].Get()) != pixels[eye])
                throw std::runtime_error("Empty overlay cleared the native scene");
            overlay.render(copied[eye].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, vp, hands,
                           true);
            const auto combined = overlay.readback(copied[eye].Get());
            overlayPixels[eye] = combined;
            size_t differences{};
            for (size_t i = 0; i < combined.size(); i += 4)
                differences +=
                    !std::equal(combined.begin() + i, combined.begin() + i + 4, pixels[eye].begin() + i);
            if (differences < 100 || differences > width * height / 4)
                throw std::runtime_error("Hand overlay missing or overwrote the scene");
            if (renderer.readback(targets[eye].Get()) != pixels[eye])
                throw std::runtime_error("Overlay changed native source image");
            if (argc >= 2) {
                std::filesystem::create_directories(argv[1]);
                saveBmp(std::filesystem::path(argv[1]) / (eye ? "right-overlay.bmp" : "left-overlay.bmp"),
                        combined, width, height);
            }
        }
        // Reuse upload memory and allocator across asynchronous stereo batches.
        // Both eyes must match the independently rendered synchronous reference.
        for (unsigned frame = 0; frame < 16; ++frame)
            overlay.renderViews(pairedViews, hands, true, false);
        for (unsigned eye = 0; eye < 2; ++eye)
            if (overlay.readback(copied[eye].Get()) != overlayPixels[eye])
                throw std::runtime_error("Asynchronous stereo overlay changed the reference image");
        // Session snapshots: a nonblocking copy queued behind an asynchronous
        // overlay returns the submitted image exactly, from typed and typeless
        // targets, and leaves the target as it was.
        for (unsigned eye = 0; eye < 2; ++eye) {
            overlay.renderViews(pairedViews, hands, true, false);
            const auto ticket = overlay.capture(copied[eye].Get());
            D3D12Renderer::Captured shot;
            for (unsigned waited = 0; ticket && waited < 5000 && !overlay.captured(ticket, shot); ++waited)
                Sleep(1);
            if (!ticket || !overlay.captured(ticket, shot) || shot.width != width || shot.height != height ||
                shot.rowPitch < width * 4)
                throw std::runtime_error("Eye snapshot was not captured");
            for (unsigned y = 0; y < height; ++y)
                if (std::memcmp(shot.pixels + static_cast<size_t>(y) * shot.rowPitch,
                                overlayPixels[eye].data() + static_cast<size_t>(y) * width * 4, width * 4))
                    throw std::runtime_error("Eye snapshot differs from the submitted image");
            if (overlay.captured(ticket + 1, shot) || overlay.captured(0, shot) || overlay.capture(nullptr))
                throw std::runtime_error("Eye snapshot accepted an unknown ticket or target");
            if (overlay.readback(copied[eye].Get()) != overlayPixels[eye])
                throw std::runtime_error("Eye snapshot changed the submitted image");
        }
        std::cout << "PASS nonblocking eye snapshots after asynchronous overlays: exact pixels.\n";
        {
            // Game-style webs: a taut web with its splat, a slack web, and one
            // still being shot. Drawn over the left eye for visual inspection.
            const Pose camera{{-.032f, 19.7f, 19}, {}};
            const auto vp = multiply(projection(-.8f, .8f, -.65f, .65f), viewMatrix(camera));
            const float pixelAngle = 2 * std::tan(.8f) / width;
            const Pose right{{.15f, 19.45f, 18.55f}, Quat{0, 0, 0, 1}};
            const Pose left{{-.17f, 19.4f, 18.5f}, Quat{0, 0, 0, 1}};
            std::vector<Vertex> webs = hands;
            WebLine line;
            line.start = webWrist(right);
            line.end = {17, 30, -5};
            appendWeb(webs, line, camera.position, pixelAngle);
            line = {};
            line.start = webWrist(left);
            line.end = {-12, 26, -20};
            line.slack = 3;
            line.seed = 1.7f;
            appendWeb(webs, line, camera.position, pixelAngle);
            line = {};
            line.start = webWrist(right);
            line.end = {5, 35, -40};
            line.extended = .6f;
            appendWeb(webs, line, camera.position, pixelAngle);
            overlay.render(copied[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, vp, webs, true);
            const auto image = overlay.readback(copied[0].Get());
            size_t drawn{}, white{};
            for (size_t i = 0; i < image.size(); i += 4) {
                if (!std::equal(image.begin() + i, image.begin() + i + 4, overlayPixels[0].begin() + i)) {
                    ++drawn;
                    white += image[i] > 200 && image[i + 1] > 200 && image[i + 2] > 200;
                }
            }
            if (drawn < 300 || white < 100 || drawn > width * height / 4)
                throw std::runtime_error("Web overlay missing, dark, or overwrote the scene");
            if (argc >= 2)
                saveBmp(std::filesystem::path(argv[1]) / "web-overlay.bmp", image, width, height);
            std::cout << "PASS game-style web overlay: " << drawn << " pixels, " << white << " bright.\n";
        }
        {
            // The game screen: the game's presented frame drawn into an eye
            // image. Display-encoded back buffers keep their stored values in
            // the sRGB (VDXR typeless) target; light values are encoded.
            const auto scene = multiply(projection(-.8f, .8f, -.65f, .65f), viewMatrix({{0, 19.7f, 19}, {}}));
            auto make = [&](DXGI_FORMAT format) {
                auto d = td;
                d.Format = format;
                ComPtr<ID3D12Resource> texture;
                require(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                                   IID_PPV_ARGS(&texture)));
                return texture;
            };
            auto drawn = [&](DXGI_FORMAT format, DXGI_FORMAT view) {
                auto texture = make(format);
                renderer.render(texture.Get(), view, width, height, scene, vertices);
                return texture;
            };
            auto toShaderResource = [&](ID3D12Resource* texture) {
                require(allocator->Reset());
                require(list->Reset(allocator.Get(), nullptr));
                D3D12_RESOURCE_BARRIER b{};
                b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition = {texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
                list->ResourceBarrier(1, &b);
                require(list->Close());
                ID3D12CommandList* lists[] = {list.Get()};
                renderer.queue()->ExecuteCommandLists(1, lists);
                renderer.waitIdle();
            };
            auto worst = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
                int largest = a.size() == b.size() ? 0 : 255;
                for (size_t i = 0; largest < 255 && i < a.size(); ++i)
                    if (i % 4 != 3)
                        largest = std::max(largest, std::abs(int(a[i]) - int(b[i])));
                return largest;
            };
            const auto encoded8 = drawn(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM);
            const auto encoded10 = drawn(DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM);
            const auto light = drawn(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT);
            const auto stored = renderer.readback(encoded8.Get());
            const auto srgbScene = drawn(DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
            const auto encodedLight = renderer.readback(srgbScene.Get());
            const auto eye = make(DXGI_FORMAT_R8G8B8A8_TYPELESS);
            const D3D12Renderer::ViewTarget target{eye.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, {}};
            int errors[3]{};
            for (int source = 0; source < 3; ++source) {
                const auto& texture = source == 0 ? encoded8 : source == 1 ? encoded10 : light;
                toShaderResource(texture.Get());
                renderer.blit(texture.Get(),
                              source == 0   ? DXGI_FORMAT_R8G8B8A8_UNORM
                              : source == 1 ? DXGI_FORMAT_R10G10B10A2_UNORM
                                            : DXGI_FORMAT_R16G16B16A16_FLOAT,
                              source == 2, target);
                errors[source] = worst(renderer.readback(eye.Get()), source == 2 ? encodedLight : stored);
            }
            if (errors[0] > 1 || errors[1] > 1 || errors[2] > 1)
                throw std::runtime_error("Game screen changed the presented frame's values (" +
                                         std::to_string(errors[0]) + ", " + std::to_string(errors[1]) + ", " +
                                         std::to_string(errors[2]) + " levels)");
            std::cout << "PASS game screen blit into the sRGB eye image: 8-bit, 10-bit and float frames within "
                      << std::max({errors[0], errors[1], errors[2]}) << " level.\n";
        }
        if (argc >= 2) {
            std::filesystem::path folder = argv[1];
            std::filesystem::create_directories(folder);
            for (int eye = 0; eye < 2; ++eye)
                saveBmp(folder / (eye ? "right-eye.bmp" : "left-eye.bmp"), pixels[eye], width, height);
        }
        std::cout << "PASS real D3D12 stereo render: " << changed << " pixels differ; near-object disparity "
                  << disparity << " px at 64 mm IPD.\n";
        std::cout
            << "PASS RGBA8 copies to sRGB and VDXR typeless targets: exact pixels, sources preserved.\n";
        std::cout
            << "PASS tracked hand overlay on shared device/queue: native scene and sources preserved.\n";
        std::cout << "PASS asynchronous paired overlay reuse at " << width << 'x' << height << ".\n";
        std::cout << "PASS six staged stereo generations with repeated presentation: exact pixels.\n";
        if (diagnostics) {
            for (UINT64 i = 0; i < diagnostics->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
                SIZE_T size{};
                require(diagnostics->GetMessage(i, nullptr, &size));
                std::vector<unsigned char> buffer(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
                require(diagnostics->GetMessage(i, message, &size));
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                    throw std::runtime_error(message->pDescription);
            }
            std::cout << "PASS D3D12 debug layer: no errors.\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
