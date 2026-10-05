#include "spidy/lab_world.hpp"
#include "spidy/game_tracking.hpp"
#include "spidy/web_visual.hpp"
#include "spidy/xr_session.hpp"
#include <iostream>
#include <string>
using namespace spidy;
int main(int argc, char** argv) {
    try {
        const bool probe = argc == 2 && std::string(argv[1]) == "--probe";
        if (argc > 1 && !probe) {
            std::cerr << "Usage: spidy_xr_lab [--probe]\n";
            return 2;
        }
        D3D12Renderer renderer;
        XrRuntime xr;
        xr.initialize(renderer, probe);
        if (probe)
            return 0;
        LabWorld world;
        Swing swing;
        Rig rig;
        std::vector<Vertex> geometry;
        bool snapHeld = false, resetHeld = false, haveHead = false;
        Pose lastHead{};
        auto reset = [&]() {
            swing.reset({{0, 18.3501f, 19}, {}, true});
            rig = {{0, 18, 19}, 0};
        };
        reset();
        std::cout << "Spidy Swing Lab - synthetic city; Spider-Man integration is not connected.\n"
                  << "Trigger + grip: fire. Hold grip: swing. Release grip: let go.\n"
                  << "Release then squeeze trigger: reel. Pull hand back sharply: zip.\n"
                  << "Left stick: move. Right stick: snap turn. A: jump. Y: reset rooftop. Esc: exit.\n";
        while (!(GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
            bool running = xr.frame(
                [&](const XrFrame& f) {
                    if (f.recentered && haveHead) {
                        rig.preserveHead(lastHead, f.head);
                        swing.releaseAll();
                    }
                    if (f.focused && f.valid && f.reset && !resetHeld)
                        reset();
                    resetHeld = f.reset;
                    const float turn = f.hands[1].stickX;
                    if (f.focused && f.valid && std::abs(turn) > .7f && !snapHeld) {
                        rig.turn(turn > 0 ? -.5235988f : .5235988f, f.head.position);
                        snapHeld = true;
                    }
                    if (std::abs(turn) < .3f)
                        snapHeld = false;
                    const auto in = trackedSwingInput(f, rig);
                    const Vec3 previous = swing.body().position;
                    swing.update(f.seconds, in, world);
                    rig.origin += swing.body().position - previous;
                    for (auto event : swing.events()) {
                        xr.haptic(event.hand, event.strength);
                        if (event.kind == EventKind::Zip)
                            std::cout << "Zip\n";
                        if (event.kind == EventKind::PointLaunch)
                            std::cout << "Point launch\n";
                    }
                    if (swing.body().position.y < -50)
                        reset();
                    if (f.valid) {
                        lastHead = rig.toWorld(f.head);
                        haveHead = true;
                    }
                    geometry.clear();
                    for (const auto& b : world.boxes())
                        addBox(geometry, b.min, b.max, b.color);
                    // Street markings give speed and scale references.
                    for (int z = -350; z < 50; z += 10)
                        addBox(geometry, {-.08f, .002f, static_cast<float>(z)},
                               {.08f, .008f, static_cast<float>(z + 4)}, {.65f, .62f, .4f});
                    for (int i = 0; i < 2; ++i)
                        if (f.hands[i].valid) {
                            const auto hand = rig.toWorld(f.hands[i].grip);
                            const Vec3 r{.045f, .045f, .065f};
                            const Vec3 color = i ? Vec3{.2f, .65f, 1} : Vec3{1, .12f, .18f};
                            // Controller axes show the tracked hand orientation.
                            const auto end = hand.position + hand.orientation.rotate({0, 0, -.17f});
                            addBeam(geometry, hand.position, end, .035f, color);
                            const auto& web = swing.webs()[i];
                            if (web.attached) {
                                WebLine line;
                                line.start = webWrist(hand);
                                line.end = web.anchor;
                                line.slack = web.tension > 0 ? 0.f
                                                             : std::max(web.length - length(web.anchor - line.start), 0.f);
                                line.seed = static_cast<float>(i) * 1.7f;
                                // Both eyes share this geometry; Quest 3 lenses resolve
                                // roughly one milliradian per recommended pixel.
                                appendWeb(geometry, line, haveHead ? lastHead.position : hand.position, .001f);
                            } else {
                                const auto aim = rig.toWorld(f.hands[i].aim);
                                if (auto hit =
                                        world.raycast(aim.position, aim.orientation.rotate({0, 0, -1}), 100))
                                    addBox(geometry, hit->point - r, hit->point + r, color);
                            }
                        }
                },
                [&](unsigned, const XrView& eye, ID3D12Resource* target, DXGI_FORMAT format, unsigned width,
                    unsigned height) {
                    const Pose tracked{{eye.pose.position.x, eye.pose.position.y, eye.pose.position.z},
                                       {eye.pose.orientation.x, eye.pose.orientation.y,
                                        eye.pose.orientation.z, eye.pose.orientation.w}};
                    auto vp = multiply(
                        projection(eye.fov.angleLeft, eye.fov.angleRight, eye.fov.angleDown, eye.fov.angleUp),
                        viewMatrix(rig.toWorld(tracked)));
                    renderer.render(target, format, width, height, vp, geometry);
                });
            if (!running)
                break;
        }
        renderer.waitIdle();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Spidy: " << e.what() << "\nConnect Quest 3 in Virtual Desktop, then launch again.\n";
        return 1;
    }
}
