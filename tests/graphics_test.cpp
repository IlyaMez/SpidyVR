#include "spidy/copy_eye_texture.hpp"
#include "spidy/d3d12_renderer.hpp"
#include "spidy/lab_world.hpp"
#include "spidy/vr_settings_canvas.hpp"
#include "spidy/web_visual.hpp"
#include <algorithm>
#include <cmath>
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
            // Aim markers over the left eye, on sky and on walls: each shows its
            // colour inside a dark outline. Saved for visual inspection.
            const Pose camera{{-.032f, 19.7f, 19}, {}};
            const auto vp = multiply(projection(-.8f, .8f, -.65f, .65f), viewMatrix(camera));
            const float pixelAngle = 2 * std::tan(.8f) / width;
            struct Expected {
                AimMarker marker;
                bool (*colour)(const unsigned char*);
                const char* name;
            };
            const Expected expected[] = {
                {{AimMark::anchor, {-6, 22, 0}}, [](const unsigned char* p) { return p[0] > 220 && p[1] > 220 && p[2] > 230; },
                 "anchor"},
                {{AimMark::anchor, {-2, 18.5f, 8}, 0, .5f}, [](const unsigned char* p) { return p[0] > 220 && p[1] > 220 && p[2] > 230; },
                 "squeezed anchor"},
                {{AimMark::air, {8, 40, -79}}, [](const unsigned char* p) { return p[0] > 140 && p[0] < 205 && p[2] > p[0]; },
                 "open air"},
                {{AimMark::blocked, {4, 18, 4}}, [](const unsigned char* p) { return p[0] > 200 && p[1] < 110 && p[2] < 110; },
                 "miss"},
                {{AimMark::target, {.8f, 18.6f, 11}, .45f}, [](const unsigned char* p) { return p[0] > 230 && p[1] > 170 && p[1] < 235 && p[2] < 130; },
                 "catch"},
            };
            std::vector<Vertex> marks;
            for (const auto& e : expected)
                appendAimMarker(marks, e.marker, camera.position, pixelAngle);
            overlay.render(copied[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, vp, marks, true);
            const auto image = overlay.readback(copied[0].Get());
            for (const auto& e : expected) {
                const auto& p = e.marker.point;
                const float clip[4] = {vp[0] * p.x + vp[1] * p.y + vp[2] * p.z + vp[3],
                                       vp[4] * p.x + vp[5] * p.y + vp[6] * p.z + vp[7], 0,
                                       vp[12] * p.x + vp[13] * p.y + vp[14] * p.z + vp[15]};
                const int cx = static_cast<int>((clip[0] / clip[3] * .5f + .5f) * width);
                const int cy = static_cast<int>((.5f - clip[1] / clip[3] * .5f) * height);
                size_t coloured{}, dark{};
                const int reach = 40 * static_cast<int>(width) / 1536 + 30;
                for (int y = std::max(cy - reach, 0); y < std::min(cy + reach, static_cast<int>(height)); ++y)
                    for (int x = std::max(cx - reach, 0); x < std::min(cx + reach, static_cast<int>(width)); ++x) {
                        const auto* pixel = image.data() + (static_cast<size_t>(y) * width + x) * 4;
                        coloured += e.colour(pixel);
                        dark += pixel[0] < 45 && pixel[1] < 45 && pixel[2] < 50;
                    }
                if (coloured < 20 || dark < 20)
                    throw std::runtime_error(std::string("Aim marker missing its colour or outline: ") + e.name);
            }
            if (argc >= 2)
                saveBmp(std::filesystem::path(argv[1]) / "aim-markers.bmp", image, width, height);
            std::cout << "PASS aim markers: anchor, squeezed anchor, open air, miss and catch drawn in colour "
                         "inside a dark outline.\n";
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
        {
            // The VR settings panel beside the game screen: painted on the CPU,
            // uploaded, drawn into the top-left of the right eye's (VDXR typeless
            // sRGB) image with its values unchanged; the rest of that image is
            // left alone. Drawn again without new pixels, as each later frame's
            // image is. Saved for inspection.
            using namespace vr_settings;
            vr_settings::Canvas canvas;
            Panel::Look look;
            look.open = true;
            const auto line = [](Item item) -> const Line& {
                return *std::find_if(lines().begin(), lines().end(), [&](const Line& l) { return l.item == item; });
            };
            const auto& speed = line(Item::swingSpeed);
            look.hover[1] = {Item::swingSpeed, 1};
            look.cursor[1] = look.held[1] = true;
            look.x[1] = controlBox(speed).x + 160;
            look.y[1] = speed.box.y + 33;
            Values values;
            values.punch = false;
            const float scale = std::min({1.5f, static_cast<float>(width) / panelPoints[0],
                                          static_cast<float>(height) / panelPoints[1]});
            canvas.draw(look, values, scale);
            if (canvas.width() != static_cast<unsigned>(std::lround(panelPoints[0] * scale)) ||
                canvas.height() != static_cast<unsigned>(std::lround(panelPoints[1] * scale)))
                throw std::runtime_error("Settings panel painted at the wrong size");
            const auto painted = [&](float x, float y) {
                const uint32_t p = canvas.pixels()[static_cast<size_t>(y * scale) * canvas.width() +
                                                   static_cast<size_t>(x * scale)];
                return std::array<int, 3>{static_cast<int>(p >> 16 & 0xff), static_cast<int>(p >> 8 & 0xff),
                                          static_cast<int>(p & 0xff)};
            };
            const auto is = [](std::array<int, 3> p, int r, int g, int b) {
                return std::abs(p[0] - r) <= 2 && std::abs(p[1] - g) <= 2 && std::abs(p[2] - b) <= 2;
            };
            // Punching off, the other switches on: each shows its own value.
            const auto markers = controlBox(line(Item::aimMarkers)), air = controlBox(line(Item::airWebs)),
                       punch = controlBox(line(Item::punch));
            if (!is(painted(100, 2), 227, 38, 47) || !is(painted(300, 85), 21, 25, 34) ||
                !is(painted(markers.x + 8, markers.y + markers.h / 2), 59, 130, 246) ||
                !is(painted(air.x + 8, air.y + air.h / 2), 59, 130, 246) ||
                !is(painted(punch.x + 30, punch.y + 4), 44, 50, 66) ||
                !is(painted(look.x[1], look.y[1]), 59, 130, 246))
                throw std::runtime_error("Settings panel colours: accent, panel, switches or cursor wrong");
            size_t bright{};
            for (float y = 22; y < 56; y += 1 / scale)
                for (float x = 28; x < 300; x += 1 / scale)
                    bright += painted(x, y)[0] > 200;
            if (bright < 200)
                throw std::runtime_error("Settings panel title not drawn");
            auto texture = td;
            texture.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            std::array<ComPtr<ID3D12Resource>, 2> eyes;
            for (auto& e : eyes)
                require(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
                                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                                   IID_PPV_ARGS(&e)));
            const auto scene = multiply(projection(-.8f, .8f, -.65f, .65f), viewMatrix({{0, 19.7f, 19}, {}}));
            std::array<std::vector<unsigned char>, 2> before;
            for (unsigned i = 0; i < 2; ++i) {
                renderer.render(eyes[i].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, scene, vertices);
                before[i] = renderer.readback(eyes[i].Get());
            }
            const unsigned cw = canvas.width(), ch = canvas.height();
            int worst{};
            for (unsigned i = 0; i < 2; ++i) {
                renderer.blitPixels(i ? nullptr : canvas.pixels(), cw, ch, canvas.rowPitch(),
                                    {eyes[i].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, cw, ch, {}});
                const auto after = renderer.readback(eyes[i].Get());
                for (unsigned y = 0; y < height; ++y)
                    for (unsigned x = 0; x < width; ++x) {
                        const size_t at = (static_cast<size_t>(y) * width + x) * 4;
                        if (x >= cw || y >= ch) {
                            if (std::memcmp(&after[at], &before[i][at], 4))
                                throw std::runtime_error("Settings panel drew outside its corner");
                            continue;
                        }
                        const uint32_t p = canvas.pixels()[static_cast<size_t>(y) * cw + x];
                        const int expected[] = {static_cast<int>(p >> 16 & 0xff), static_cast<int>(p >> 8 & 0xff),
                                                static_cast<int>(p & 0xff)};
                        for (int c = 0; c < 3; ++c)
                            worst = std::max(worst, std::abs(int(after[at + c]) - expected[c]));
                    }
            }
            if (worst > 1)
                throw std::runtime_error("Settings panel changed its pixels by " + std::to_string(worst) + " levels");
            auto rgba = [](const vr_settings::Canvas& c) {
                std::vector<unsigned char> out(static_cast<size_t>(c.width()) * c.height() * 4);
                for (size_t i = 0; i < out.size() / 4; ++i) {
                    const uint32_t p = c.pixels()[i];
                    out[i * 4] = static_cast<unsigned char>(p >> 16);
                    out[i * 4 + 1] = static_cast<unsigned char>(p >> 8);
                    out[i * 4 + 2] = static_cast<unsigned char>(p);
                    out[i * 4 + 3] = 255;
                }
                return out;
            };
            if (argc >= 2)
                saveBmp(std::filesystem::path(argv[1]) / "vr-settings.bmp", rgba(canvas), cw, ch);
            // Folded to its tab: a new size, uploaded again.
            Panel::Look tab;
            tab.hover[0] = {Item::tab};
            tab.cursor[0] = true;
            tab.x[0] = 150;
            tab.y[0] = 30;
            canvas.draw(tab, values, scale);
            if (canvas.width() != static_cast<unsigned>(std::lround(tabPoints[0] * scale)) ||
                !is(painted(2, 30), 227, 38, 47))
                throw std::runtime_error("Settings tab painted wrong");
            renderer.blitPixels(canvas.pixels(), canvas.width(), canvas.height(), canvas.rowPitch(),
                                {eyes[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, canvas.width(), canvas.height(), {}});
            const auto folded = renderer.readback(eyes[0].Get());
            const uint32_t corner = canvas.pixels()[static_cast<size_t>(canvas.height() / 2) * canvas.width() + 1];
            if (std::abs(int(folded[(static_cast<size_t>(canvas.height() / 2) * width + 1) * 4]) - int(corner >> 16 & 0xff)) > 1)
                throw std::runtime_error("Settings tab not drawn into the eye image");
            if (argc >= 2)
                saveBmp(std::filesystem::path(argv[1]) / "vr-settings-tab.bmp", rgba(canvas), canvas.width(),
                        canvas.height());
            std::cout << "PASS VR settings panel: painted " << cw << 'x' << ch
                      << ", drawn into the eye image's corner within " << worst
                      << " level, the rest untouched; drawn again without new pixels; the tab re-uploaded.\n";
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
