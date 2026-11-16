// Draws the combat marks (web_visual.hpp's appendCue, appendImpact,
// appendKnockout, and Spider-Sense's appendSense and appendSenseSight), the
// gadget wheel (appendGadgetWheel), the aim markers, slow motion's meter and
// the calibration panel over a
// headset's eye captures, without a headset: the overlay's own vertices, one
// sample a pixel with its blending, at about the headset's pixel density.
// It writes sheets of the marks' states and poses as BMP files; the wheel's,
// the meter's and the panel's are the near capture's, at a hand's distance
// (the panel's 1.4 m) where its pixel is.
// tools/preview-marks.ps1 feeds it captures and turns the sheets into PNG.
//
//   spidy_mark_preview <out folder> <scale> <far.bmp|-> <u> <v> <metres> <near.bmp|-> <u> <v> <metres>
//                      [comic|tech]
//
// Each backdrop is a 24-bit BMP of an eye capture enlarged `scale` times
// (the captures are 512 pixels wide, about 235 pixels a unit of tangent),
// with the capture pixel (u, v) of the enemy's chest and how far off he is;
// "-" for a plain backdrop, dark to the left and pale to the right. The last
// argument is the SPIDY VR tab's UI STYLE (ui_style.hpp), comic if left out.
#include "spidy/body_calibration.hpp"
#include "spidy/gadget_wheel.hpp"
#include "spidy/overlay_text.hpp"
#include "spidy/web_visual.hpp"
#include "preview_image.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace spidy;
using namespace spidy::preview;

namespace {
Image plain(int w, int h) {
    Image image(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool pale = x >= w / 2;
            float* p = image.at(x, y);
            p[0] = pale ? .55f : .012f;
            p[1] = pale ? .5f : .012f;
            p[2] = pale ? .42f : .02f;
        }
    return image;
}
void label(Image& image, const std::string& text, int x, int y) {
    constexpr float height = 15;
    const Camera flat{1, 0, 0};
    for (const bool shadow : {true, false}) {
        TextStyle style;
        style.height = height;
        style.weight = shadow ? .4f : .14f;
        style.color = shadow ? Vec3{0, 0, 0} : Vec3{1, .9f, .2f};
        std::vector<Vertex> v;
        appendText(v, text, {static_cast<float>(x), -static_cast<float>(y) - height, -1}, {1, 0, 0}, {0, 1, 0}, style);
        draw(image, v, flat, 0, 0, 0, 0, image.w, image.h);
    }
}
struct Cell {
    std::string name;
    std::vector<Vertex> vertices;
};
// A sheet of cells, each a crop of `backdrop` `side` pixels square about
// camera pixel (u, v), with its marks and its name.
bool sheet(const std::string& path, const Image& backdrop, const Camera& camera, float u, float v, int side,
           int columns, const std::vector<Cell>& cells) {
    constexpr int gap = 6;
    const int rows = (static_cast<int>(cells.size()) + columns - 1) / columns;
    Image image(columns * (side + gap) + gap, rows * (side + gap) + gap);
    const int u0 = static_cast<int>(u) - side / 2, v0 = static_cast<int>(v) - side / 2;
    for (size_t i = 0; i < cells.size(); ++i) {
        const int x0 = gap + static_cast<int>(i % columns) * (side + gap);
        const int y0 = gap + static_cast<int>(i / columns) * (side + gap);
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                const int su = u0 + x, sv = v0 + y;
                const bool inside = su >= 0 && sv >= 0 && su < backdrop.w && sv < backdrop.h;
                for (int k = 0; k < 3; ++k)
                    image.at(x0 + x, y0 + y)[k] = inside ? backdrop.at(su, sv)[k] : .01f;
            }
        draw(image, cells[i].vertices, camera, static_cast<float>(u0), static_cast<float>(v0), x0, y0, side, side);
        label(image, cells[i].name, x0 + 6, y0 + 6);
    }
    return save(image, path);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 11 && argc != 12) {
        std::printf("spidy_mark_preview <out folder> <scale> <far.bmp|-> <u> <v> <metres> "
                    "<near.bmp|-> <u> <v> <metres> [comic|tech]\n");
        return 1;
    }
    const UiStyle style = argc == 12 && std::string(argv[11]) == "tech" ? UiStyle::tech : UiStyle::comic;
    const std::string out = argv[1];
    const float scale = std::clamp(static_cast<float>(std::atof(argv[2])), 1.f, 8.f);
    struct Scene {
        Image backdrop;
        float u{}, v{}, metres{};
    } scenes[2];
    for (int i = 0; i < 2; ++i) {
        auto& scene = scenes[i];
        const std::string path = argv[3 + i * 4];
        if (path == "-")
            scene.backdrop = plain(static_cast<int>(512 * scale), static_cast<int>(544 * scale));
        else if (!load(path, scene.backdrop)) {
            std::printf("%s is not a 24-bit BMP\n", path.c_str());
            return 2;
        }
        scene.u = static_cast<float>(std::atof(argv[4 + i * 4])) * scale;
        scene.v = static_cast<float>(std::atof(argv[5 + i * 4])) * scale;
        scene.metres = std::clamp(static_cast<float>(std::atof(argv[6 + i * 4])), .2f, 50.f);
    }
    const Vec3 viewer{};
    constexpr float pose = 1.f / 12;
    const auto at = [&](int i) { return pose * (static_cast<float>(i) + .5f); };
    bool ok = true;
    for (int i = 0; i < 2; ++i) {
        const auto& scene = scenes[i];
        const Camera camera{235 * scale, static_cast<float>(scene.backdrop.w) / 2, static_cast<float>(scene.backdrop.h) / 2};
        const float pixelAngle = 1 / camera.f;
        const Vec3 p = camera.ray(scene.u, scene.v) * scene.metres;
        const auto cue = [&](CueWay way, bool finisher, float left, float age, bool locked = false) {
            std::vector<Vertex> v;
            appendCue(v, {p, way, finisher, left, age, locked}, viewer, pixelAngle, style);
            return v;
        };
        const auto knockout = [&](float age) {
            std::vector<Vertex> v;
            appendKnockout(v, {p, age}, viewer, pixelAngle, style);
            return v;
        };
        const auto impact = [&](BlowKind kind, float strength, int count, float age, bool called = false) {
            std::vector<Vertex> v;
            appendImpact(v, {p, strength, kind, count, age, called}, viewer, pixelAngle, style);
            return v;
        };
        const std::string name = out + (i ? "/near-" : "/far-");
        // A cell holds the mark with room around it at either distance.
        const int side = static_cast<int>((i ? 150 : 120) * scale);
        ok &= sheet(name + "marks.bmp", scene.backdrop, camera, scene.u, scene.v, side, 5,
                    {{"LOOKED AT", cue(CueWay::None, false, 1, 1)},
                     {"LOCKED", cue(CueWay::None, false, 1, 1, true)},
                     {"CALL LEFT", cue(CueWay::Left, false, .85f, 1, true)},
                     {"CALL UP 50%", cue(CueWay::Up, false, .5f, 1.17f, true)},
                     {"FINISHER RIGHT", cue(CueWay::Right, true, .9f, 1, true)},
                     {"LOCKED POSE 1", cue(CueWay::None, false, 1, at(0), true)},
                     {"CALL DOWN 20%", cue(CueWay::Down, false, .2f, 1.34f, true)},
                     {"CALL POSE 1", cue(CueWay::Right, false, 1, at(0), true)},
                     {"CALL POSE 2", cue(CueWay::Right, false, 1, at(1), true)},
                     {"FINISHER POSE 2", cue(CueWay::Up, true, 1, at(1), true)}});
        std::vector<Cell> blows;
        for (int k = 0; k < 5; ++k)
            blows.push_back({"JAB X2 POSE " + std::to_string(k + 1), impact(BlowKind::Hit, .5f, 2, at(k))});
        for (int k = 0; k < 5; ++k)
            blows.push_back({"CALLED X3 POSE " + std::to_string(k + 1), impact(BlowKind::Heavy, 1, 3, at(k), true)});
        for (int k = 0; k < 5; ++k)
            blows.push_back({"FINISHER X5 POSE " + std::to_string(k + 1), impact(BlowKind::Finisher, 1, 5, at(k))});
        blows.push_back({"FIRST JAB POSE 2", impact(BlowKind::Hit, .2f, 1, at(1))});
        blows.push_back({"HEAVY POSE 2", impact(BlowKind::Heavy, 1, 1, at(1))});
        blows.push_back({"COUNT HELD", impact(BlowKind::Hit, .5f, 4, at(5))});
        blows.push_back({"COUNT LEAVING 1", impact(BlowKind::Hit, .5f, 4, at(7))});
        blows.push_back({"COUNT LEAVING 2", impact(BlowKind::Hit, .5f, 4, at(8))});
        blows.push_back({"KNOCKOUT POSE 1", knockout(at(0))});
        blows.push_back({"KNOCKOUT POSE 2", knockout(at(1))});
        blows.push_back({"KNOCKOUT HELD", knockout(at(4))});
        blows.push_back({"KNOCKOUT LEAVING 1", knockout(at(6))});
        blows.push_back({"KNOCKOUT LEAVING 2", knockout(at(7))});
        ok &= sheet(name + "blows.bmp", scene.backdrop, camera, scene.u, scene.v, side, 5, blows);
        // A blow a guard stopped: the poses of the first block of three before he swings back, the second,
        // the one that sets off his counter, a weak and a full blow, no count told, and the mark leaving.
        const auto block = [&](float strength, int streak, int counterAt, float age) {
            std::vector<Vertex> v;
            appendBlock(v, {p, strength, streak, counterAt, age}, viewer, pixelAngle, style);
            return v;
        };
        std::vector<Cell> blocked;
        for (int k = 0; k < 5; ++k)
            blocked.push_back({"BLOCK 1 OF 3 POSE " + std::to_string(k + 1), block(.5f, 1, 3, at(k))});
        blocked.push_back({"BLOCK 2 OF 3 HELD", block(.5f, 2, 3, at(4))});
        blocked.push_back({"BLOCK 3 OF 3 COUNTER", block(.5f, 3, 3, at(4))});
        blocked.push_back({"WEAK BLOW", block(.1f, 1, 3, at(4))});
        blocked.push_back({"FULL BLOW", block(1, 1, 3, at(4))});
        blocked.push_back({"NO COUNT TOLD", block(.5f, 0, 0, at(4))});
        blocked.push_back({"1 OF 5", block(.5f, 1, 5, at(5))});
        blocked.push_back({"4 OF 5", block(.5f, 4, 5, at(5))});
        blocked.push_back({"POP OUT", block(.5f, 2, 3, at(6))});
        blocked.push_back({"LEAVING", block(.5f, 2, 3, at(7))});
        blocked.push_back({"GONE", block(.5f, 2, 3, at(8))});
        ok &= sheet(name + "block.bmp", scene.backdrop, camera, scene.u, scene.v, side, 5, blocked);
        // Spider-Sense (spider_sense.hpp): the warning over his head as it lands, as its ring closes on the
        // blow, as each answer, with no time set, and as a counter breaks it. The cells are about the mark's
        // own centre, which stands over his head.
        const Vec3 headTop = p + Vec3{0, .55f, 0};
        const auto sense = [&](SenseAnswer answer, float left, float age, float broken = -1,
                               bool timed = true) {
            std::vector<Vertex> v;
            appendSense(v, {headTop, answer, left, timed, age, broken}, viewer, pixelAngle, style);
            return v;
        };
        const float markSize = .055f * std::clamp(length(headTop) / .9f, 1.4f, 9.f);
        const Vec3 markCentre = headTop + Vec3{0, .14f + .5f * markSize, 0};
        const float markU = camera.cx + camera.f * markCentre.x / -markCentre.z;
        const float markV = camera.cy - camera.f * markCentre.y / -markCentre.z;
        const int senseSide = static_cast<int>((i ? 230 : 150) * scale);
        ok &= sheet(name + "sense.bmp", scene.backdrop, camera, markU, markV, senseSide, 5,
                    {{"COUNTER POSE 1", sense(SenseAnswer::Counter, 1, at(0))},
                     {"COUNTER POSE 2", sense(SenseAnswer::Counter, 1, at(1))},
                     {"COUNTER", sense(SenseAnswer::Counter, 1, 1)},
                     {"COUNTER 50%", sense(SenseAnswer::Counter, .5f, 1.17f)},
                     {"COUNTER NOW", sense(SenseAnswer::Counter, .125f, 1.34f)},
                     {"DODGE", sense(SenseAnswer::Dodge, .75f, 1)},
                     {"DODGE NOW", sense(SenseAnswer::Dodge, .125f, 1.17f)},
                     {"JUMP", sense(SenseAnswer::Jump, .625f, 1)},
                     {"NO TIME SET", sense(SenseAnswer::Counter, 1, 1, -1, false)},
                     {"BOILING", sense(SenseAnswer::Counter, .75f, at(5))},
                     {"COUNTERED 1", sense(SenseAnswer::Counter, .5f, 1, at(0))},
                     {"COUNTERED 2", sense(SenseAnswer::Counter, .5f, 1, at(1))},
                     {"COUNTERED 3", sense(SenseAnswer::Counter, .5f, 1, at(2))}});
        // The whole eye image: a counter's mark over him, and in the sightline a dodge for an attacker to
        // the right and a counter for one up to the left, both out of view.
        {
            Image senseEye = scene.backdrop;
            std::vector<Vertex> v = sense(SenseAnswer::Counter, .5f, 1.17f);
            const Pose eyes{viewer, {}};
            SenseSightView right, upLeft;
            right.answer = SenseAnswer::Dodge;
            right.sight = {true, 1, 0};
            right.timed = true;
            right.left = .75f;
            right.age = 1;
            upLeft.answer = SenseAnswer::Counter;
            upLeft.sight = {true, -.8f, .6f};
            upLeft.timed = true;
            upLeft.left = .375f;
            upLeft.age = 1.17f;
            appendSenseSight(v, right, eyes, pixelAngle, {}, style);
            appendSenseSight(v, upLeft, eyes, pixelAngle, {}, style);
            draw(senseEye, v, camera, 0, 0, 0, 0, senseEye.w, senseEye.h);
            ok &= save(senseEye, name + "sense-eye.bmp");
        }
        // The gadget wheel 0.45 m off, facing the eyes, every gadget on it as the game's wheel has them: the
        // spider-drone equipped, the concussion blast out of charges. As it lands, at rest, and as one
        // lights.
        if (i) {
            GadgetWheelView wheel;
            wheel.open = true;
            wheel.frame.centre = camera.ray(scene.u, scene.v) * .45f;
            wheel.frame.out = normalized(viewer - wheel.frame.centre);
            wheel.frame.up = normalized(Vec3{0, 1, 0} - wheel.frame.out * wheel.frame.out.y);
            wheel.frame.right = cross(wheel.frame.up, wheel.frame.out);
            const Gadget all[] = {Gadget::webShooter,  Gadget::impactWeb,       Gadget::webBomb,
                                  Gadget::electricWeb, Gadget::tripMine,        Gadget::concussionBlast,
                                  Gadget::spiderDrone, Gadget::suspensionMatrix};
            for (int g = 0; g < gadgetPlaces; ++g) {
                wheel.places[static_cast<size_t>(g)] = all[g];
                wheel.most[static_cast<size_t>(g)] = g ? 4 : 0;
                wheel.ammo[static_cast<size_t>(g)] = static_cast<uint8_t>(g == 5 ? 0 : 3);
            }
            wheel.equipped = 6;
            const auto drawn = [&](float age, int lit, float litAge) {
                auto view = wheel;
                view.age = age;
                view.lit = lit;
                view.litAge = litAge;
                std::vector<Vertex> v;
                appendGadgetWheel(v, view, viewer, pixelAngle, style);
                return v;
            };
            const int wide = static_cast<int>(210 * scale);
            ok &= sheet(name + "wheel.bmp", scene.backdrop, camera, scene.u, scene.v, wide, 5,
                        {{"OPENING POSE 1", drawn(at(0), -1, 0)},
                         {"OPENING POSE 2", drawn(at(1), -1, 0)},
                         {"OPENING POSE 3", drawn(at(2), -1, 0)},
                         {"OPEN", drawn(1, -1, 0)},
                         {"LIT POSE 1", drawn(1, 1, at(0))},
                         {"LIT POSE 2", drawn(1, 1, at(1))},
                         {"LIT", drawn(1, 1, 1)},
                         {"LIT, BOILING", drawn(1, 1, at(3))},
                         {"EQUIPPED, LIT", drawn(1, 6, 1)},
                         {"EMPTY, LIT", drawn(1, 5, 1)}});
            // Slow motion's meter 0.4 m off, over the back of a left hand held with no turn: at rest, being
            // spent, in slow motion and as a refused press flashes it.
            const Vec3 face = camera.ray(scene.u, scene.v) * .4f;
            const auto meter = [&](float focus, bool active, float blend, float warning, float clock) {
                SlowMotionView slow;
                slow.meter = 1;
                slow.focus = focus;
                slow.active = active;
                slow.blend = blend;
                slow.warning = warning;
                std::vector<Vertex> v;
                const Pose hand{face - Vec3{0, .045f, .065f}, {}};
                appendSlowMotionMeter(v, slow, hand, viewer, pixelAngle, clock, style);
                return v;
            };
            const int small = static_cast<int>(60 * scale);
            ok &= sheet(name + "meter.bmp", scene.backdrop, camera, scene.u, scene.v, small, 6,
                        {{"FULL", meter(1, false, 0, 0, 0)},
                         {"60%", meter(.6f, false, 0, 0, 0)},
                         {"SLOW 35%", meter(.35f, true, 1, 0, .1f)},
                         {"SLOW 35% LATER", meter(.35f, true, 1, 0, .6f)},
                         {"REFUSED", meter(.1f, false, 0, 1, 0)},
                         {"EMPTY", meter(0, false, 0, 0, 0)}});
            // The pad layer's sign over the back of a right hand held with no turn, 0.4 m and 0.6 m off.
            const auto sign = [&](float metres) {
                std::vector<Vertex> v;
                const Pose hand{camera.ray(scene.u, scene.v) * metres - Vec3{0, .07f, .065f}, {}};
                appendPadLayerSign(v, hand, viewer, pixelAngle, style);
                return v;
            };
            ok &= sheet(name + "padlayer.bmp", scene.backdrop, camera, scene.u, scene.v,
                        static_cast<int>(130 * scale), 2, {{"0.4 M", sign(.4f)}, {"0.6 M", sign(.6f)}});
            // The calibration panel 1.4 m ahead, as the XR worker places it, waiting, holding and done; and a
            // controller's ring.
            body_calibration::View calibration;
            calibration.panel = body_calibration::panelPose({viewer, {}});
            calibration.handTracked = {true, true};
            calibration.grips[0].position = calibration.panel.position + Vec3{-.25f, -.05f, .9f};
            calibration.grips[1].position = calibration.panel.position + Vec3{.7f, 0, .9f};
            const auto panel = [&](body_calibration::Phase phase, float progress, bool ready) {
                auto view = calibration;
                view.phase = phase;
                view.hint = body_calibration::Hint::armsOut;
                view.progress = progress;
                view.ready = {ready, ready};
                view.result = {1.63f, .59f, {.59f, .58f}};
                std::vector<Vertex> v;
                body_calibration::appendView(v, view, viewer, style);
                return v;
            };
            const Vec3 middle = calibration.panel.position, grip = calibration.grips[0].position;
            const auto pixelOf = [&](Vec3 at) {
                return std::array<float, 2>{camera.cx + camera.f * at.x / -at.z,
                                            camera.cy - camera.f * at.y / -at.z};
            };
            const auto [pu, pv] = pixelOf(middle);
            const int panelSide = static_cast<int>(250 * scale);
            ok &= sheet(name + "panel.bmp", scene.backdrop, camera, pu, pv, panelSide, 2,
                        {{"WAITING", panel(body_calibration::Phase::waiting, 0, false)},
                         {"HOLDING", panel(body_calibration::Phase::holding, .6f, true)},
                         {"DONE", panel(body_calibration::Phase::done, 1, true)}});
            const auto [gu, gv] = pixelOf(grip);
            const int ringSide = static_cast<int>(110 * scale);
            ok &= sheet(name + "panel-ring.bmp", scene.backdrop, camera, gu, gv, ringSide, 3,
                        {{"WAITING", panel(body_calibration::Phase::waiting, 0, false)},
                         {"HOLDING", panel(body_calibration::Phase::holding, .6f, true)},
                         {"DONE", panel(body_calibration::Phase::done, 1, true)}});
        }
        // The aim markers where the enemy is, for each hand: as each kind, squeezed, and a target's ring as
        // it locks on.
        {
            const auto aim = [&](AimMark kind, unsigned hand, float squeeze = 0, float lock = 1,
                                 float spin = .3f, float radius = .3f) {
                AimMarker marker;
                marker.kind = kind;
                marker.point = p;
                marker.hand = hand;
                marker.squeeze = squeeze;
                marker.lock = lock;
                marker.spin = spin;
                marker.radius = radius;
                std::vector<Vertex> v;
                appendAimMarker(v, marker, viewer, pixelAngle, style);
                return v;
            };
            const int cell = static_cast<int>((i ? 150 : 100) * scale);
            ok &= sheet(name + "aim.bmp", scene.backdrop, camera, scene.u, scene.v, cell, 5,
                        {{"ANCHOR LEFT", aim(AimMark::anchor, 0)},
                         {"ANCHOR RIGHT", aim(AimMark::anchor, 1)},
                         {"SQUEEZED", aim(AimMark::anchor, 0, .6f)},
                         {"AIR", aim(AimMark::air, 0)},
                         {"BLOCKED", aim(AimMark::blocked, 1)},
                         {"TARGET LEFT", aim(AimMark::target, 0)},
                         {"TARGET RIGHT", aim(AimMark::target, 1)},
                         {"LOCKING", aim(AimMark::target, 0, 0, .3f)},
                         {"SMALL TARGET", aim(AimMark::target, 1, 0, 1, 1.2f, .02f)},
                         {"TARGET SQUEEZED", aim(AimMark::target, 0, .6f, 1, 2)}});
        }
        // The whole eye image with a call on him.
        Image whole = scene.backdrop;
        draw(whole, cue(CueWay::Left, false, .7f, 1), camera, 0, 0, 0, 0, whole.w, whole.h);
        ok &= save(whole, name + "eye.bmp");
    }
    return ok ? 0 : 3;
}
