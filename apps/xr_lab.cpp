#include "spidy/game_tracking.hpp"
#include "spidy/lab_props.hpp"
#include "spidy/lab_world.hpp"
#include "spidy/web_visual.hpp"
#include "spidy/xr_session.hpp"
#include <iostream>
#include <string>
using namespace spidy;
namespace {
// Vertices added since `begin` were built around the origin: place them.
void place(std::vector<Vertex>& out, size_t begin, Vec3 position, Quat orientation) {
    for (size_t i = begin; i < out.size(); ++i)
        out[i].position = position + orientation.rotate(out[i].position);
}
// A thug: legs, torso, arms and a masked head around its centre of mass.
void addThug(std::vector<Vertex>& out, const LabProp& p) {
    const size_t begin = out.size();
    const Vec3 pants{.08f, .09f, .12f}, skin{.55f, .4f, .3f}, mask{.03f, .03f, .035f};
    for (float side : {-1.f, 1.f}) {
        addBeam(out, {side * .1f, -.1f, 0}, {side * .11f, -.93f, 0}, .075f, pants);
        addBeam(out, {side * .24f, .38f, 0}, {side * .29f, -.12f, -.05f}, .06f, p.color);
    }
    addBeam(out, {0, -.12f, 0}, {0, .42f, 0}, .18f, p.color);
    addBox(out, {-.11f, .48f, -.11f}, {.11f, .72f, .11f}, skin);
    addBox(out, {-.115f, .58f, -.115f}, {.115f, .64f, -.09f}, mask);
    place(out, begin, p.position, p.orientation);
}
void addProp(std::vector<Vertex>& out, const LabProp& p) {
    if (p.kind == TargetKind::Character) {
        addThug(out, p);
        return;
    }
    const size_t begin = out.size();
    addBox(out, -p.half, p.half, p.color);
    // Darker bands make a crate's or barrel's tumbling readable.
    addBox(out, Vec3{-p.half.x - .005f, -p.half.y * .15f, -p.half.z - .005f},
           Vec3{p.half.x + .005f, p.half.y * .15f, p.half.z + .005f}, p.color * .55f);
    place(out, begin, p.position, p.orientation);
}
} // namespace
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
        LabProps props;
        Swing swing;
        WebGrab grab;
        Rig rig;
        std::vector<Vertex> geometry;
        bool snapHeld = false, resetHeld = false, haveHead = false;
        Pose lastHead{};
        unsigned knockdowns = 0;
        auto reset = [&]() {
            swing.reset({{0, 18.3501f, 19}, {}, true});
            grab.releaseAll();
            props.reset();
            knockdowns = 0;
            rig = {{0, 18, 19}, 0};
        };
        reset();
        std::cout << "Spidy Swing Lab - synthetic city; Spider-Man integration is not connected.\n"
                  << "Squeeze grip: fire and swing. Release grip: let go.\n"
                  << "Trigger while swinging: reel. Pull hand back sharply: zip.\n"
                  << "Grip on a crate, barrel or thug (it lights up): web it. Trigger: reel it in.\n"
                  << "Pull hand back sharply: yank it to you. Swing your arm and let go: throw.\n"
                  << "Left stick: move. Right stick: snap turn. A: jump. Y: reset rooftop. Esc: exit.\n";
        while (!(GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
            bool running = xr.frame(
                [&](const XrFrame& f) {
                    if (f.recentered && haveHead) {
                        rig.preserveHead(lastHead, f.head);
                        swing.releaseAll();
                        grab.releaseAll();
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
                    // A press aimed at a prop belongs to the grab, never to the swing.
                    const auto forSwing = grab.claim(f.seconds, in, world, props, swing.body());
                    const Vec3 previous = swing.body().position;
                    swing.update(f.seconds, forSwing, world);
                    rig.origin += swing.body().position - previous;
                    props.advance(f.seconds, grab, world);
                    for (auto event : swing.events()) {
                        xr.haptic(event.hand, event.strength);
                        if (event.kind == EventKind::Zip)
                            std::cout << "Zip\n";
                        if (event.kind == EventKind::PointLaunch)
                            std::cout << "Point launch\n";
                    }
                    for (const auto& event : grab.events()) {
                        xr.haptic(event.hand, event.strength);
                        if (event.kind == GrabEventKind::Throw)
                            std::cout << "Throw at " << static_cast<int>(length(event.velocity) + .5f)
                                      << " m/s\n";
                    }
                    if (props.knockdowns() != knockdowns) {
                        knockdowns = props.knockdowns();
                        std::cout << "Thugs knocked down: " << knockdowns << '\n';
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
                    for (const auto& p : props.props())
                        addProp(geometry, p);
                    const Vec3 viewer = haveHead ? lastHead.position : Vec3{};
                    for (int i = 0; i < 2; ++i)
                        if (f.hands[i].valid) {
                            const auto hand = rig.toWorld(f.hands[i].grip);
                            const Vec3 r{.045f, .045f, .065f};
                            const Vec3 color = i ? Vec3{.2f, .65f, 1} : Vec3{1, .12f, .18f};
                            // Controller axes show the tracked hand orientation.
                            const auto end = hand.position + hand.orientation.rotate({0, 0, -.17f});
                            addBeam(geometry, hand.position, end, .035f, color);
                            const auto& web = swing.webs()[i];
                            const auto& held = grab.grabs()[i];
                            const auto target =
                                held.phase != GrabPhase::None ? props.find(held.target) : std::nullopt;
                            if (web.attached || target) {
                                WebLine line;
                                line.start = webWrist(hand);
                                line.end = target ? target->position : web.anchor;
                                const float rope = target ? held.length : web.length;
                                const bool taut = target ? held.taut : web.tension > 0;
                                line.slack = taut ? 0.f : std::max(rope - length(line.end - line.start), 0.f);
                                line.seed = static_cast<float>(i) * 1.7f;
                                // Both eyes share this geometry; Quest 3 lenses resolve
                                // roughly one milliradian per recommended pixel.
                                appendWeb(geometry, line, haveHead ? viewer : hand.position, .001f);
                            } else {
                                const auto aim = rig.toWorld(f.hands[i].aim);
                                // What a grip press would web: a prop lights up above it.
                                if (const auto next = grab.preview(aim, world, props)) {
                                    const Vec3 top = next->position + Vec3{0, next->radius + .25f, 0};
                                    addBox(geometry, top - Vec3{.06f, .06f, .06f},
                                           top + Vec3{.06f, .06f, .06f}, {1, .85f, .2f});
                                    addBeam(geometry, top, next->position + Vec3{0, next->radius, 0}, .012f,
                                            {1, .85f, .2f});
                                } else if (auto hit = world.raycast(
                                               aim.position, aim.orientation.rotate({0, 0, -1}), 100)) {
                                    addBox(geometry, hit->point - r, hit->point + r, color);
                                }
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
