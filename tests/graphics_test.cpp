#include "spidy/body_calibration.hpp"
#include "spidy/copy_eye_texture.hpp"
#include "spidy/d3d12_renderer.hpp"
#include "spidy/gadget_wheel.hpp"
#include "spidy/lab_world.hpp"
#include "spidy/phone.hpp"
#include "spidy/tracking.hpp"
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
        // Two hand-sized blocks, one either side, in the overlay.
        std::vector<Vertex> hands;
        const Vec3 red{.65f, .025f, .035f}, blue{.035f, .06f, .24f};
        addBox(hands, {-.19f, 19.48f, 18.4f}, {-.11f, 19.52f, 18.59f}, red);
        addBox(hands, {.11f, 19.48f, 18.4f}, {.19f, 19.52f, 18.59f}, red);
        addBeam(hands, {.15f, 19.5f, 18.5f}, {.15f, 19.47f, 18.35f}, .012f, blue);
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
            // The window's mirror: the image the headset is about to show goes to a
            // texture of the bridge's own right behind an asynchronous overlay, as
            // native_gpu::keepShownEye copies it, from a typed and a typeless image.
            // The copy rests as a copy source and is read as a shader resource; it
            // has the overlay's drawings exactly, and the headset's image stays.
            auto shownDesc = targets[0]->GetDesc();
            shownDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            ComPtr<ID3D12Resource> shown;
            require(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &shownDesc,
                                                               D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                                                               IID_PPV_ARGS(&shown)));
            auto barriers = [](ID3D12GraphicsCommandList* commands, UINT n, const D3D12_RESOURCE_BARRIER* b) {
                commands->ResourceBarrier(n, b);
            };
            for (unsigned eye = 0; eye < 2; ++eye) {
                const auto windowDesc = copied[eye]->GetDesc();
                ComPtr<ID3D12Resource> window;
                require(renderer.device()->CreateCommittedResource(
                    &heap, D3D12_HEAP_FLAG_NONE, &windowDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                    IID_PPV_ARGS(&window)));
                renderer.waitIdle();
                require(allocator->Reset());
                require(list->Reset(allocator.Get(), nullptr));
                overlay.renderViews(pairedViews, hands, true, false);
                copyEyeTexture(list.Get(), copied[eye].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, shown.Get(),
                               barriers, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_RESOURCE_BARRIER read{};
                read.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                read.Transition = {shown.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE,
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
                list->ResourceBarrier(1, &read);
                std::swap(read.Transition.StateBefore, read.Transition.StateAfter);
                list->ResourceBarrier(1, &read);
                copyEyeTexture(list.Get(), shown.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, window.Get(),
                               barriers);
                require(list->Close());
                ID3D12CommandList* commands = list.Get();
                renderer.queue()->ExecuteCommandLists(1, &commands);
                renderer.waitIdle();
                if (renderer.readback(window.Get()) != overlayPixels[eye])
                    throw std::runtime_error("The window's copy differs from the image the headset got");
                if (overlay.readback(copied[eye].Get()) != overlayPixels[eye])
                    throw std::runtime_error("The window's copy changed the image the headset got");
            }
            std::cout << "PASS the window's copy of the shown eye behind an asynchronous overlay: exact "
                         "pixels.\n";
        }
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
            line.start = webShooter(right, 1).position;
            line.end = {17, 30, -5};
            appendWeb(webs, line, camera.position, pixelAngle);
            line = {};
            line.start = webShooter(left, 0).position;
            line.end = {-12, 26, -20};
            line.slack = 3;
            line.seed = 1.7f;
            appendWeb(webs, line, camera.position, pixelAngle);
            line = {};
            line.start = webShooter(right, 1).position;
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
            // The gadget wheel 0.45 m ahead of the left eye, facing it: the web
            // shooter at the top and seven gadgets round it, the impact web lit
            // and the spider-drone equipped, the concussion blast out of
            // charges. Printed, as the combat marks are: laid down in order
            // with the translucent vertices. Saved for visual inspection.
            const Pose camera{{-.032f, 19.7f, 19}, {}};
            const auto vp = multiply(projection(-.8f, .8f, -.65f, .65f), viewMatrix(camera));
            GadgetWheelView wheel;
            wheel.open = true;
            wheel.age = 1;
            wheel.frame.centre = camera.position + Vec3{0, -.05f, -.45f};
            const Gadget all[] = {Gadget::webShooter,  Gadget::impactWeb,       Gadget::webBomb,
                                  Gadget::electricWeb, Gadget::tripMine,        Gadget::concussionBlast,
                                  Gadget::spiderDrone, Gadget::suspensionMatrix};
            for (int p = 0; p < gadgetPlaces; ++p) {
                wheel.places[static_cast<size_t>(p)] = all[p];
                wheel.most[static_cast<size_t>(p)] = p ? 4 : 0;
                wheel.ammo[static_cast<size_t>(p)] = static_cast<uint8_t>(p == 5 ? 0 : 3);
            }
            wheel.lit = 1;
            wheel.litAge = 1;
            wheel.equipped = 6;
            std::vector<Vertex> drawnWheel;
            appendGadgetWheel(drawnWheel, wheel, camera.position, 1.6f / static_cast<float>(width));
            const auto before = overlay.readback(copied[0].Get());
            overlay.render(copied[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, vp, {}, true,
                           drawnWheel);
            const auto image = overlay.readback(copied[0].Get());
            size_t drawn{};
            for (size_t i = 0; i < image.size(); i += 4)
                drawn += !std::equal(image.begin() + i, image.begin() + i + 4, before.begin() + i);
            if (drawn < 2000)
                throw std::runtime_error("Gadget wheel missing");
            if (argc >= 2)
                saveBmp(std::filesystem::path(argv[1]) / "gadget-wheel.bmp", image, width, height);
            std::cout << "PASS gadget wheel overlay: " << drawn << " pixels, " << drawnWheel.size()
                      << " vertices.\n";
        }
        {
            // Aim markers over the left eye, translucent and farthest first as
            // the headset draws them, some on the dark scene and some on a
            // bright sky and a lit wall drawn far behind: each shows its hand's
            // colour (the left's blue, the right's orange; a miss red), and on
            // the bright panels a soft shadow darkens the panel around it. Both
            // hands' rings on one target stay apart. Saved for visual inspection.
            const Pose camera{{-.032f, 19.7f, 19}, {}};
            const auto vp = multiply(projection(-.8f, .8f, -.65f, .65f), viewMatrix(camera));
            const float pixelAngle = 2 * std::tan(.8f) / width;
            // A panel 169 m ahead over the image's share u0..u1 across, v0..v1 down.
            std::vector<Vertex> panels;
            const auto panel = [&](float u0, float u1, float v0, float v1, Vec3 colour) {
                const float d = 169;
                const auto at = [&](float u, float v) {
                    return Vertex{camera.position + Vec3{(2 * u - 1) * std::tan(.8f) * d, (1 - 2 * v) * std::tan(.65f) * d, -d},
                                  colour};
                };
                panels.insert(panels.end(), {at(u0, v0), at(u1, v0), at(u1, v1), at(u0, v0), at(u1, v1), at(u0, v1)});
            };
            panel(0, 1, 0, .456f, {.5f, .68f, .92f});        // sky above
            panel(.586f, 1, .521f, 1, {.75f, .68f, .55f});   // a lit wall at the lower right
            overlay.render(copied[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, vp, panels, true);
            using Colour = bool (*)(const unsigned char*);
            const Colour blue = [](const unsigned char* p) { return p[0] < 150 && p[1] > 160 && p[1] < 230 && p[2] > 230; };
            const Colour orange = [](const unsigned char* p) { return p[0] > 230 && p[1] > 140 && p[1] < 200 && p[2] < 110; };
            const Colour faintBlue = [](const unsigned char* p) { return p[2] > 170 && p[2] > p[0] + 60; };
            const Colour faintOrange = [](const unsigned char* p) { return p[0] > 170 && p[0] > p[2] + 60; };
            const Colour red = [](const unsigned char* p) { return p[0] > 200 && p[1] < 110 && p[2] < 110; };
            struct Expected {
                AimMarker marker;
                Colour colour;
                const char* name;
                bool onPanel{};
            };
            const auto marker = [](AimMark kind, Vec3 point, unsigned hand, float radius = 0, float squeeze = 0) {
                AimMarker m;
                m.kind = kind;
                m.point = point;
                m.hand = hand;
                m.radius = radius;
                m.squeeze = squeeze;
                return m;
            };
            const Expected expected[] = {
                {marker(AimMark::anchor, {-6, 22, 0}, 0), blue, "left anchor on the sky", true},
                {marker(AimMark::anchor, {-2, 18.5f, 8}, 1, 0, .5f), orange, "right squeezed anchor"},
                {marker(AimMark::air, {8, 40, -79}, 0), faintBlue, "left open air on the sky", true},
                {marker(AimMark::air, {-14, 34, -60}, 1), faintOrange, "right open air on the sky", true},
                {marker(AimMark::blocked, {4, 18, 4}, 1), red, "miss on the wall", true},
                {marker(AimMark::target, {.8f, 18.6f, 11}, 0, .45f), blue, "left catch"},
                {marker(AimMark::target, {.8f, 18.6f, 11}, 1, .45f), orange, "right catch, same target"},
            };
            std::vector<AimMarker> order;
            for (const auto& e : expected)
                order.push_back(e.marker);
            std::sort(order.begin(), order.end(), [&](const AimMarker& a, const AimMarker& b) {
                return length(a.point - camera.position) > length(b.point - camera.position);
            });
            std::vector<Vertex> marks;
            for (const auto& m : order)
                appendAimMarker(marks, m, camera.position, pixelAngle);
            const auto before = overlay.readback(copied[0].Get());
            overlay.render(copied[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, vp, {}, true, marks);
            const auto image = overlay.readback(copied[0].Get());
            if (argc >= 2)
                saveBmp(std::filesystem::path(argv[1]) / "aim-markers.bmp", image, width, height);
            for (const auto& e : expected) {
                const auto& p = e.marker.point;
                const float clip[4] = {vp[0] * p.x + vp[1] * p.y + vp[2] * p.z + vp[3],
                                       vp[4] * p.x + vp[5] * p.y + vp[6] * p.z + vp[7], 0,
                                       vp[12] * p.x + vp[13] * p.y + vp[14] * p.z + vp[15]};
                const int cx = static_cast<int>((clip[0] / clip[3] * .5f + .5f) * width);
                const int cy = static_cast<int>((.5f - clip[1] / clip[3] * .5f) * height);
                size_t coloured{}, shaded{};
                // The catch target has a size in metres: the right hand's ring
                // lies about 55 pixels out per 1536 of width.
                const int reach = 60 * static_cast<int>(width) / 1536 + 30;
                for (int y = std::max(cy - reach, 0); y < std::min(cy + reach, static_cast<int>(height)); ++y)
                    for (int x = std::max(cx - reach, 0); x < std::min(cx + reach, static_cast<int>(width)); ++x) {
                        const size_t at = (static_cast<size_t>(y) * width + x) * 4;
                        coloured += e.colour(image.data() + at);
                        const int was = before[at] + before[at + 1] + before[at + 2];
                        const int now = image[at] + image[at + 1] + image[at + 2];
                        shaded += was > 60 && now < was * 85 / 100;
                    }
                if (coloured < 20 || (e.onPanel && shaded < 20))
                    throw std::runtime_error(std::string("Aim marker missing its colour or shadow: ") + e.name +
                                             " (" + std::to_string(coloured) + " coloured, " +
                                             std::to_string(shaded) + " shaded pixels)");
            }
            std::cout << "PASS aim markers: both hands' anchors, open air, a miss and two rings on one target, "
                         "each in its colour, shadowed on the bright sky and wall.\n";
        }
        {
            // The T-pose calibration's panel ahead of the left eye, the hold 60% full, and once measured: a
            // dark panel with white text, the hold's green bar and a ring at each controller. Saved for visual
            // inspection.
            const Pose camera{{-.032f, 19.7f, 19}, {}};
            const auto vp = multiply(projection(-.8f, .8f, -.65f, .65f), viewMatrix(camera));
            body_calibration::View view;
            view.phase = body_calibration::Phase::holding;
            view.progress = .6f;
            view.ready = {true, true};
            view.panel = body_calibration::panelPose(camera);
            view.handTracked = {true, true};
            view.grips[0] = {{-.42f, 19.38f, 18.45f}, {}};
            view.grips[1] = {{.38f, 19.38f, 18.45f}, {}};
            const auto pixelOf = [&](Vec3 p) {
                const float x = vp[0] * p.x + vp[1] * p.y + vp[2] * p.z + vp[3],
                            y = vp[4] * p.x + vp[5] * p.y + vp[6] * p.z + vp[7],
                            w = vp[12] * p.x + vp[13] * p.y + vp[14] * p.z + vp[15];
                return std::array<int, 2>{static_cast<int>((x / w * .5f + .5f) * width),
                                          static_cast<int>((.5f - y / w * .5f) * height)};
            };
            // The panel's inner part on the image (1.2 by 0.6 m, an edge left out).
            const auto low = pixelOf(view.panel.position + Vec3{-.55f, -.27f, 0}),
                       high = pixelOf(view.panel.position + Vec3{.55f, .27f, 0});
            for (const auto phase : {body_calibration::Phase::holding, body_calibration::Phase::done}) {
                view.phase = phase;
                view.result = {1.63f, .59f, {.59f, .585f}};
                std::vector<Vertex> panel;
                body_calibration::appendView(panel, view, camera.position);
                overlay.render(copied[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, vp, panel, true);
                const auto image = overlay.readback(copied[0].Get());
                size_t dark{}, white{}, green{}, area{};
                for (int y = std::max(high[1], 0); y < std::min(low[1], static_cast<int>(height)); ++y)
                    for (int x = std::max(low[0], 0); x < std::min(high[0], static_cast<int>(width)); ++x) {
                        const auto* p = image.data() + (static_cast<size_t>(y) * width + x) * 4;
                        ++area;
                        dark += p[0] < 45 && p[1] < 45 && p[2] < 55;
                        white += p[0] > 200 && p[1] > 200 && p[2] > 200;
                        green += p[1] > 150 && p[0] < 120 && p[2] < 140;
                    }
                if (area < 1000 || dark < area / 2 || white < 300 || green < 200)
                    throw std::runtime_error("Calibration panel missing its background, text or green: " +
                                             std::to_string(dark) + " dark, " + std::to_string(white) + " white, " +
                                             std::to_string(green) + " green of " + std::to_string(area));
                if (argc >= 2)
                    saveBmp(std::filesystem::path(argv[1]) /
                                (phase == body_calibration::Phase::done ? "calibration-done.bmp"
                                                                        : "calibration-panel.bmp"),
                            image, width, height);
            }
            std::cout << "PASS calibration panel: dark panel, white text, green hold bar and rings, and the "
                         "result.\n";
        }
        {
            // Slow motion: both eyes recoloured under the overlay (the typed sRGB
            // eye and the typeless one), round about each lens; the ring crossing
            // the view after a start; the focus meter over the left wrist. A
            // uniform orange wall 50 m ahead fills both images first.
            const Vec3 orange{.9f, .3f, .08f};
            const auto corner = [&](float x, float y) { return Vertex{{x, y, -31}, orange}; };
            const std::vector<Vertex> wall{corner(-120, -100), corner(120, -100), corner(120, 140),
                                           corner(-120, -100), corner(120, 140), corner(-120, 140)};
            overlay.renderViews(pairedViews, wall, true);
            std::array<std::vector<unsigned char>, 2> flat;
            for (unsigned eye = 0; eye < 2; ++eye)
                flat[eye] = overlay.readback(copied[eye].Get());
            const auto at = [&](const std::vector<unsigned char>& image, float u, float v) {
                const size_t x = std::min<size_t>(static_cast<size_t>(u * width), width - 1);
                const size_t y = std::min<size_t>(static_cast<size_t>(v * height), height - 1);
                const auto* p = image.data() + (y * width + x) * 4;
                return std::array<int, 3>{p[0], p[1], p[2]};
            };
            D3D12Renderer::Grade grade;
            for (unsigned eye = 0; eye < 2; ++eye)
                grade.tangents[eye] = {std::tan(-.8f), std::tan(.8f), std::tan(.65f), std::tan(-.65f)};
            overlay.renderViews(pairedViews, {}, true, true, {}, &grade);
            for (unsigned eye = 0; eye < 2; ++eye)
                if (overlay.readback(copied[eye].Get()) != flat[eye])
                    throw std::runtime_error("Slow motion at rest changed the image");
            grade.amount = 1;
            overlay.renderViews(pairedViews, {}, true, true, {}, &grade);
            for (unsigned eye = 0; eye < 2; ++eye) {
                const auto image = overlay.readback(copied[eye].Get());
                const auto was = at(flat[eye], .5f, .5f), now = at(image, .5f, .5f);
                const auto rim = at(image, .02f, .03f);
                if (at(flat[eye], .02f, .03f) != was)
                    throw std::runtime_error("The orange wall did not fill the image");
                // Drained toward grey and cooler in the middle; darker at the rim.
                if (!(now[0] - now[2] < (was[0] - was[2]) * 3 / 4 && now[2] > was[2] + 20))
                    throw std::runtime_error("Slow motion did not drain or cool the image (eye " +
                                             std::to_string(eye) + ")");
                if (!(rim[0] + rim[1] + rim[2] < (now[0] + now[1] + now[2]) * 92 / 100))
                    throw std::runtime_error("Slow motion's darker rim is missing (eye " + std::to_string(eye) +
                                             ")");
            }
            // The ring a fifth of its way out: a lit band about 0.75 of a tangent
            // from the lens, the middle of the view left alone.
            grade.amount = 0;
            grade.ripple = .2f;
            grade.rippleStrength = 1;
            grade.entering = true;
            overlay.renderViews(pairedViews, wall, true);
            overlay.renderViews(pairedViews, {}, true, true, {}, &grade);
            {
                const auto image = overlay.readback(copied[0].Get());
                const float ringU = (.752f + std::tan(.8f)) / (2 * std::tan(.8f));
                const auto band = at(image, ringU, .5f), wasBand = at(flat[0], ringU, .5f);
                const auto middle = at(image, .5f, .5f), wasMiddle = at(flat[0], .5f, .5f);
                if (!(band[1] > wasBand[1] + 15 && band[2] > wasBand[2] + 25))
                    throw std::runtime_error("Slow motion's ring is missing");
                for (int c = 0; c < 3; ++c)
                    if (std::abs(middle[c] - wasMiddle[c]) > 2)
                        throw std::runtime_error("Slow motion's ring changed the middle of the view");
            }
            // The comfort vignette nearly closed: the edges and corners of both views dark, their middles
            // as they were, nothing recoloured. Open, it changes nothing.
            grade.amount = 0;
            grade.ripple = -1;
            grade.vignette = .9f;
            overlay.renderViews(pairedViews, wall, true);
            overlay.renderViews(pairedViews, {}, true, true, {}, &grade);
            for (unsigned eye = 0; eye < 2; ++eye) {
                const auto image = overlay.readback(copied[eye].Get());
                const auto sum = [](const std::array<int, 3>& c) { return c[0] + c[1] + c[2]; };
                const auto corner = at(image, .02f, .03f), edge = at(image, .03f, .5f), top = at(image, .5f, .02f);
                const auto middle = at(image, .5f, .5f), was = at(flat[eye], .5f, .5f);
                if (sum(corner) > 12 || sum(edge) > 12 || sum(top) > 12)
                    throw std::runtime_error("The comfort vignette left the edge of the view light (eye " +
                                             std::to_string(eye) + ")");
                for (int c = 0; c < 3; ++c)
                    if (std::abs(middle[c] - was[c]) > 2)
                        throw std::runtime_error("The comfort vignette changed the middle of the view");
            }
            if (argc >= 2)
                saveBmp(std::filesystem::path(argv[1]) / "comfort-vignette.bmp", overlay.readback(copied[0].Get()),
                        width, height);
            grade.vignette = 0;
            overlay.renderViews(pairedViews, wall, true);
            overlay.renderViews(pairedViews, {}, true, true, {}, &grade);
            for (unsigned eye = 0; eye < 2; ++eye)
                if (overlay.readback(copied[eye].Get()) != flat[eye])
                    throw std::runtime_error("The comfort vignette open changed the image");
            std::cout << "PASS comfort vignette: the edge dark, the middle as it was, nothing when open.\n";
            // The meter over the left wrist, 62% full, in slow motion, drawn over
            // the recoloured image as the headset draws it. Saved for inspection.
            SlowMotionView view;
            view.meter = 1;
            view.focus = .62f;
            view.blend = .8f;
            view.active = view.entering = true;
            const Pose camera{{-.032f, 19.7f, 19}, {}};
            const Vec3 centre = camera.position + Vec3{-.09f, -.16f, -.42f};
            const Pose hand{centre - Vec3{0, .045f, .065f}, {}};
            std::vector<Vertex> meter;
            appendSlowMotionMeter(meter, view, hand, camera.position, 2 * std::tan(.8f) / width, 0);
            grade.amount = .8f;
            grade.ripple = .25f;
            overlay.renderViews(pairedViews, wall, true);
            overlay.renderViews(pairedViews, {}, true, true, meter, &grade);
            const auto image = overlay.readback(copied[0].Get());
            if (argc >= 2)
                saveBmp(std::filesystem::path(argv[1]) / "slow-motion.bmp", image, width, height);
            const auto& vp = pairedViews[0].viewProjection;
            const float x = vp[0] * centre.x + vp[1] * centre.y + vp[2] * centre.z + vp[3],
                        y = vp[4] * centre.x + vp[5] * centre.y + vp[6] * centre.z + vp[7],
                        w = vp[12] * centre.x + vp[13] * centre.y + vp[14] * centre.z + vp[15];
            const int cx = static_cast<int>((x / w * .5f + .5f) * width),
                      cy = static_cast<int>((.5f - y / w * .5f) * height);
            const int reach = 80 * static_cast<int>(width) / 1536 + 10;
            size_t cyan{}, glass{};
            for (int py = std::max(cy - reach, 0); py < std::min(cy + reach, static_cast<int>(height)); ++py)
                for (int px = std::max(cx - reach, 0); px < std::min(cx + reach, static_cast<int>(width)); ++px) {
                    const auto* p = image.data() + (static_cast<size_t>(py) * width + px) * 4;
                    cyan += p[2] > 215 && p[1] > 190 && p[0] + 30 < p[2];
                    glass += p[0] > 200 && p[1] > 215 && p[2] > 225;
                }
            if (cyan < 40 || glass < 10)
                throw std::runtime_error("Slow motion's meter is missing its ring or hourglass (" +
                                         std::to_string(cyan) + " cyan, " + std::to_string(glass) +
                                         " glass pixels)");
            std::cout << "PASS slow motion: both eyes drained, cooled and darker at the rim, the ring, and the "
                         "focus meter (" << cyan << " cyan pixels).\n";
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
            // The phone's screen as a picture (D3D12Renderer::Picture): drawn into
            // a texture of its own and shown on a quad 1 m ahead - its halves as
            // drawn, a layer further out over one further in whatever the order,
            // the overlay's own nearer geometry over the quad, and nothing of it
            // off the quad.
            auto d = td;
            d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            ComPtr<ID3D12Resource> eye;
            require(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                                               D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                               IID_PPV_ARGS(&eye)));
            const Vec3 redHalf{.8f, .02f, .02f}, greenHalf{.02f, .7f, .05f}, blueLayer{.03f, .05f, .9f}, greyBlock{.4f, .4f, .4f};
            constexpr float hw = .04f, hh = .08f;
            std::vector<Vertex> picture;
            const auto rect = [&](float x0, float y0, float x1, float y1, float z, Vec3 c) {
                picture.insert(picture.end(), {Vertex{{x0, y0, z}, c}, Vertex{{x1, y0, z}, c}, Vertex{{x1, y1, z}, c},
                                               Vertex{{x0, y0, z}, c}, Vertex{{x1, y1, z}, c}, Vertex{{x0, y1, z}, c}});
            };
            rect(-.01f, -.01f, .01f, .01f, .003f, blueLayer); // first, and further out
            rect(-hw, -hh, 0, hh, .001f, redHalf);
            rect(0, -hh, hw, hh, .001f, greenHalf);
            constexpr float qx = .1f, qy = .2f;
            const D3D12Renderer::Picture shown{
                picture, hw, hh, {Vec3{-qx, -qy, -1}, Vec3{qx, -qy, -1}, Vec3{qx, qy, -1}, Vec3{-qx, qy, -1}}, {}};
            std::vector<Vertex> nearer;
            addBox(nearer, {-.2f, .14f, -.9f}, {.2f, .3f, -.85f}, greyBlock);
            constexpr float tangent = .3f;
            const auto vp = multiply(projection(-tangent, tangent, -tangent, tangent), viewMatrix({{0, 0, 0}, {}}));
            const D3D12Renderer::ViewTarget view{eye.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, vp};
            for (int frame = 0; frame < 3; ++frame)
                overlay.renderViews(std::span(&view, 1), nearer, false, true, {}, nullptr, &shown);
            const auto image = overlay.readback(eye.Get());
            // A point's pixel, from the camera at the origin looking down -z.
            const auto at = [&](float x, float y, float z) {
                const auto px = static_cast<size_t>((x / -z / tangent * .5f + .5f) * static_cast<float>(width));
                const auto py = static_cast<size_t>((.5f - y / -z / tangent * .5f) * static_cast<float>(height));
                return &image[(py * width + px) * 4];
            };
            const unsigned char* left = at(-.05f, -.1f, -1);
            const unsigned char* right = at(.05f, -.1f, -1);
            const unsigned char* middle = at(0, 0, -1);
            const unsigned char* covered = at(.05f, .17f, -1);
            const unsigned char* off = at(.15f, -.1f, -1);
            if (left[0] < 150 || left[1] > 80 || right[1] < 150 || right[0] > 80)
                throw std::runtime_error("Picture's halves are not where they were drawn");
            if (middle[2] < 150 || middle[0] > 80)
                throw std::runtime_error("Picture's outer layer was covered by one drawn later further in");
            if (covered[0] < 110 || covered[0] > 210 || std::abs(int(covered[0]) - int(covered[1])) > 8)
                throw std::runtime_error("Overlay geometry nearer than the picture did not cover it");
            if (off[0] > 60 && off[1] > 60)
                throw std::runtime_error("Picture drawn off its quad");
            std::cout << "PASS phone screen picture: its own texture with mips on a quad, its layers by depth, "
                         "under nearer geometry.\n";
        }
        {
            // The phone far out in the city, where the game has it: its body
            // (appendPhone) with a screen of one green on its glass, 35 cm before
            // an eye at (1158, 9, -360) that looks off the world's axes (so all
            // three of its coordinates reach the depth), the phone turned 25
            // degrees. In the world's own coordinates single floats round the
            // screen and the glass under it each their own way, and the glass
            // shows through; about the eye, as game_xr.cpp draws the overlay, it
            // never does.
            auto d = td;
            d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            ComPtr<ID3D12Resource> eye;
            require(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                                               D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                               IID_PPV_ARGS(&eye)));
            const Vec3 city{1158.37f, 9.21f, -360.53f}, ahead{.02f, -.03f, -.35f}, green{.02f, .7f, .05f};
            constexpr float tangent = .3f, turn = .44f;
            const Quat look = Quat::yaw(.65f) * Quat::around({1, 0, 0}, -.35f);
            const Vec3 across = look.rotate({std::cos(turn), 0, -std::sin(turn)}), upward = look.rotate({0, 1, 0}),
                       facing = look.rotate({std::sin(turn), 0, std::cos(turn)});
            const float hw = phoneScreenHalfWidth, hh = phoneScreenHalfHeight;
            std::vector<Vertex> screen;
            for (const Vec3 corner : {Vec3{-hw, -hh, .001f}, Vec3{hw, -hh, .001f}, Vec3{hw, hh, .001f},
                                      Vec3{-hw, -hh, .001f}, Vec3{hw, hh, .001f}, Vec3{-hw, hh, .001f}})
                screen.push_back({corner, green});
            // How many points of the screen's inner nine tenths show anything but
            // its green, the phone and the eye drawn about `origin`.
            const auto glassShown = [&](Vec3 origin, unsigned& points) {
                PhoneView phone;
                phone.open = true;
                phone.age = 1;
                phone.frame = {city - origin + look.rotate(ahead), across, upward, facing};
                std::vector<Vertex> body;
                appendPhone(body, phone, city - origin);
                const D3D12Renderer::Picture shown{screen, hw, hh, phoneScreenCorners(phone), green};
                const auto vp = multiply(projection(-tangent, tangent, -tangent, tangent),
                                         viewMatrix({city - origin, look}));
                const D3D12Renderer::ViewTarget seen{eye.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height,
                                                     vp};
                overlay.renderViews(std::span(&seen, 1), body, false, true, {}, nullptr, &shown);
                const auto image = overlay.readback(eye.Get());
                unsigned shows = 0;
                points = 0;
                // Its corners: lower left, lower right, upper right, upper left.
                const auto corners = phoneScreenCorners(phone);
                for (int i = -45; i <= 45; ++i)
                    for (int j = -45; j <= 45; ++j) {
                        // The point on the screen, from the eye, in the eye's axes.
                        const Vec3 p = look.conjugate().rotate(
                            (corners[0] + corners[2]) * .5f + (corners[1] - corners[0]) * (.01f * i) +
                            (corners[2] - corners[1]) * (.01f * j) - (city - origin));
                        const float fx = (p.x / -p.z / tangent * .5f + .5f) * static_cast<float>(width),
                                    fy = (.5f - p.y / -p.z / tangent * .5f) * static_cast<float>(height);
                        if (!(fx >= 0 && fy >= 0 && fx < static_cast<float>(width) &&
                              fy < static_cast<float>(height)))
                            continue;
                        const unsigned char* c =
                            &image[(static_cast<size_t>(fy) * width + static_cast<size_t>(fx)) * 4];
                        ++points;
                        shows += c[1] < 150 || c[0] > 80;
                    }
                return shows;
            };
            unsigned inWorld{}, aboutEye{};
            const unsigned worldShows = glassShown({}, inWorld);
            const unsigned eyeShows = glassShown(city, aboutEye);
            if (aboutEye < 4000)
                throw std::runtime_error("Phone far out: its screen was not in view");
            if (eyeShows)
                throw std::runtime_error("Phone far out, drawn about the eye: its glass showed through its "
                                         "screen at " +
                                         std::to_string(eyeShows) + " of " + std::to_string(aboutEye) + " points");
            std::cout << "PASS phone 1158 m out, drawn about the eye: its screen over its glass at all "
                      << aboutEye << " points (in the world's coordinates the glass showed at " << worldShows
                      << " of " << inWorld << ").\n";
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
