#pragma once
#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace spidy::native_hud {
// The game's HUD in VR.
//
// Its panel (health, gadgets, minimap, prompts) is ui/export/ModelHudFull.gfx
// drawn into a texture (ScaleformRTTStream, 1920 wide times the window's aspect
// over 16:9, 1080 high) that a 16:9 model carries ("modelHudFull", 2.25 units
// tall). PlayerModelHudFollower (update 73ab80) places that model 20 m in
// front of the camera manager's camera, at a size larger than that camera's
// view: the texture covers the part the view shows (on the October 9 save, the
// middle 56%). In VR the camera manager's camera is still the third-person
// camera behind the player: the panel hung far ahead of the player, mostly out
// of view and small. While immersive Spidy places it in front of the head,
// scaled so the part the game's view showed spans the width asked for.
//
// The rest of the HUD (world markers, subtitles, interaction and QTE prompts,
// pause menus) is a second Scaleform movie (global 7be3e00), laid out in the
// window's pixels and drawn by the "Scaleform" render command (a8, 1d2b6c0)
// straight into the game's own view, which the eyes never get. For the VR
// session the panel's texture is made at the window's size; while the headset
// shows the eye views, that movie is drawn onto it after the panel's own
// movie, so it shows on the panel, and while immersive the game projects its
// world markers (1f10ad0, 1f10b60) from the head onto the panel instead of
// onto its view.
//
// The panel does not turn with every movement of the head: it stays where it
// is in the room while the head looks around it a little, and glides back in
// front of the head once it looks further (Follow, in the XR worker). A
// head-locked panel shook: a quarter of the frames the headset showed in the
// October 9 session were earlier frames turned to the newer head, which turns
// the panel with them.
struct Shape {
    // Metres from the head, and half the width the game's view of the HUD
    // spans, as a tangent (0.577: 30 degrees each side).
    float distance = 2.f, halfWidth = .577f;
};
// The HUD's sizes (the SPIDY VR tab's HUD: 0 off, 1 small, 2 medium, 3 large):
// the degrees the panel spans across.
inline constexpr float widths[] = {0, 50, 60, 70};
// Half a size's width as a tangent (medium for one out of range); 0 off.
inline float halfWidth(int size) {
    if (size == 0)
        return 0;
    const float degrees = widths[size >= 1 && size <= 3 ? size : 2];
    return std::tan(degrees / 2 * 3.14159265f / 180);
}
// Where the panel faces in the room (the tracking space), from the head's
// orientation there (OpenXR axes: x right, y up, z back). The panel stays
// upright and holds still while the head looks within `hold` degrees of its
// centre; beyond that it glides back in front of the head, covering 63% of
// the way every `glide` seconds, and holds again within `settle` degrees.
class Follow {
public:
    float hold = 2, settle = .25f, glide = .15f;
    // The panel's orientation after `seconds` more with the head at `head`.
    Quat update(const Quat& head, float seconds) {
        const Vec3 f = head.rotate({0, 0, -1}), u = head.rotate({0, 1, 0});
        // Within 64 degrees of level the panel turns the way the face does.
        // Nearer straight up or down that way is lost, but the top of the
        // head still points the way the face turns.
        const float pole = std::clamp((std::abs(f.y) - .9f) / .1f, 0.f, 1.f);
        const Vec3 h{f.x - u.x * f.y * pole, 0, f.z - u.z * f.y * pole};
        const float yaw = length(h) > 1e-4f ? std::atan2(-h.x, -h.z) : yaw_,
                    pitch = std::asin(std::clamp(f.y, -1.f, 1.f));
        if (!std::isfinite(yaw) || !std::isfinite(pitch))
            return orientation();
        if (!started_) {
            yaw_ = yaw;
            pitch_ = pitch;
            started_ = true;
            gliding_ = false;
        }
        angle_ = degreesTo(f);
        if (!gliding_ && angle_ > hold)
            gliding_ = true;
        if (gliding_) {
            const float step = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .25f) : 0.f;
            const float k = 1 - std::exp(-step / glide);
            float turn = yaw - yaw_;
            turn -= 6.2831853f * std::round(turn / 6.2831853f);
            yaw_ += turn * k;
            pitch_ += (pitch - pitch_) * k;
            angle_ = degreesTo(f);
            if (angle_ < settle)
                gliding_ = false;
        }
        return orientation();
    }
    // The next update puts the panel straight in front of the head.
    void reset() {
        started_ = false;
    }
    // Degrees between where the head and the panel faced at the last update.
    float angle() const {
        return angle_;
    }
    bool gliding() const {
        return gliding_;
    }

private:
    Quat orientation() const {
        return Quat::yaw(yaw_) * Quat::around({1, 0, 0}, pitch_);
    }
    float degreesTo(Vec3 forward) const {
        const Vec3 p = orientation().rotate({0, 0, -1});
        return std::acos(std::clamp(dot(p, forward), -1.f, 1.f)) * 180 / 3.14159265f;
    }
    float yaw_{}, pitch_{}, angle_{};
    bool started_{}, gliding_{};
};
// A native view pose (rows right, down, forward, position) at `head`'s position,
// turned by `relative`: an orientation in the head's own OpenXR axes (x right,
// y up, z back), such as the panel's from Follow, seen from the head.
inline Mat4 turned(const Mat4& head, const Quat& relative) {
    const Vec3 right{head[0], head[1], head[2]}, down{head[4], head[5], head[6]}, forward{head[8], head[9], head[10]};
    const auto world = [&](Vec3 v) { return right * v.x - down * v.y - forward * v.z; };
    const Vec3 r = world(relative.rotate({1, 0, 0})), d = world(relative.rotate({0, -1, 0})),
               f = world(relative.rotate({0, 0, -1}));
    return {r.x, r.y, r.z, 0, d.x, d.y, d.z, 0, f.x, f.y, f.z, 0, head[12], head[13], head[14], 1};
}
// The panel's transform in front of `head` (native view pose: rows right, down,
// forward, position). `game`: the scales the game gave the model this frame
// (x, y, z), at `gameDistance` metres from its camera, whose view's half width
// is `viewHalfWidth` (a tangent). Every scale changes alike, so the panel keeps
// its shape. Rows of `out`, as the game's own placement has them: the panel's x
// (right), y (up), z (toward the viewer), position.
inline bool panel(const Mat4& head, Vec3 game, float gameDistance, float viewHalfWidth, const Shape& shape,
                  Mat4& out) {
    if (!(game.x > 0 && game.y > 0 && game.z > 0 && gameDistance > 0 && viewHalfWidth > .05f &&
          viewHalfWidth < 20 && shape.distance > 0 && shape.halfWidth > 0) ||
        !std::isfinite(game.x + game.y + game.z + gameDistance + viewHalfWidth + shape.distance + shape.halfWidth))
        return false;
    // What the game's view showed of the panel, half wide: viewHalfWidth *
    // gameDistance metres; in front of the head: halfWidth * distance.
    const float scale = shape.halfWidth * shape.distance / (viewHalfWidth * gameDistance);
    const float sx = game.x * scale, sy = game.y * scale, sz = game.z * scale;
    const Vec3 right{head[0], head[1], head[2]}, down{head[4], head[5], head[6]}, forward{head[8], head[9], head[10]};
    const Vec3 p = Vec3{head[12], head[13], head[14]} + forward * shape.distance;
    out = {right.x * sx,    right.y * sx,    right.z * sx,    0, -down.x * sy, -down.y * sy, -down.z * sy, 0,
           -forward.x * sz, -forward.y * sz, -forward.z * sz, 0, p.x,         p.y,         p.z,         1};
    for (float f : out)
        if (!std::isfinite(f) || std::abs(f) > 1e7f)
            return false;
    return true;
}
// Where a world point shows on the panel, as the game's marker projection
// returns it: x right and y down, 0 to 1 across the panel's texture, seen from
// `head` (native view pose) through a panel `halfWidth` wide (tangent) and
// `aspect` (texture width over height) wide per high. Like the game's own
// projection, a point behind the head divides by a negative depth; `front`
// says whether it is ahead of the head (beyond `nearZ`).
inline bool project(const Mat4& head, float halfWidth, float aspect, Vec3 world, float nearZ, float& x, float& y,
                    bool& front) {
    const Vec3 right{head[0], head[1], head[2]}, down{head[4], head[5], head[6]}, forward{head[8], head[9], head[10]};
    const Vec3 d = world - Vec3{head[12], head[13], head[14]};
    const float depth = dot(d, forward);
    if (!(halfWidth > 0 && aspect > 0) || !std::isfinite(depth) || std::abs(depth) < 1e-6f)
        return false;
    const float halfHeight = halfWidth / aspect;
    x = (dot(d, right) / depth / halfWidth + 1) / 2;
    y = (dot(d, down) / depth / halfHeight + 1) / 2;
    front = depth > nearZ;
    return std::isfinite(x) && std::isfinite(y);
}
// The panel texture's base width that the game's stream turns into `width`
// pixels: it makes the texture int(base * factor) wide (factor: the window's
// aspect over 16:9, at least 1).
inline uint32_t textureBase(uint32_t width, float factor) {
    if (!(factor >= 1) || !std::isfinite(factor) || !width)
        return width;
    auto base = static_cast<uint32_t>(std::lround(width / factor));
    for (uint32_t candidate : {base, base + 1, base - 1})
        if (static_cast<uint32_t>(static_cast<float>(candidate) * factor) == width)
            return candidate;
    return base;
}
// SpidyHudData: where the HUD went.
struct Data {
    uint32_t magic = 0x53485544, version = 3, bytes = sizeof(Data), state{};
    int64_t sequence{};
    // The follower and the panel's render instance at its last placement.
    uint64_t follower{}, instance{};
    // The game's placements seen; immersive frames that put the panel in front
    // of the head, and those that could not (no placement this frame, the
    // instance gone, no head view).
    uint64_t gameFrames{}, placedFrames{}, rejectedFrames{};
    // The game's last placement: its scales, its distance from its camera (m),
    // and the half width of the game camera's view (tangent).
    float gameScale[3]{}, gameDistance{}, viewHalfWidth{};
    // The panel as placed last: metres from the head, half width (tangent);
    // the largest difference so far between the player travel the panel and
    // the eyes of one frame were moved by (m), which should stay 0.
    float distance{}, halfWidth{}, offsetMismatch{};
    // The panel's texture: its size when last made, the game's own base size
    // (1920 x 1080), and how often Spidy made it at the window's size for VR
    // and back.
    uint32_t textureSize[2]{}, gameTextureSize[2]{};
    uint64_t textureResizes{}, textureRestores{};
    // Frames whose second movie (markers, subtitles, prompts) was drawn on the
    // panel, and world markers projected from the head onto it.
    uint64_t layerFrames{}, markerProjections{};
    // native_hud::start's result (0, or why the second movie stays off the
    // panel), and whether the game's own texture size is back.
    uint32_t layerStatus{}, restored{};
    // The HUD row now (0 off, 1-3 small to large). Frames that left the panel
    // out of the eyes because it is off, and the eye draws of it left out.
    // Frames placed where the XR worker's Follow put the panel (the rest face
    // the way the head does); the degrees between the head's direction and the
    // panel's at the last placement, and the most so far.
    uint32_t size{}, spare{};
    uint64_t hiddenFrames{}, hiddenDraws{}, followedFrames{};
    float followAngle{}, followAngleMax{};
};
static_assert(sizeof(Data) == 192);
// SpidyHudData (native_hud.cpp): writers change it between beginEdit and
// endEdit, which bracket the change with the sequence under its lock.
Data& beginEdit();
void endEdit();
template <class F> void publish(F&& change) {
    change(beginEdit());
    endEdit();
}
// This frame's head for the HUD (main thread): set by the frame latch in
// stereo_probe.cpp.
struct Head {
    Mat4 pose{};  // native view pose, moved with the player to the frame
    Mat4 panel{}; // the same at the head, turned the way the panel faces
    float halfWidth{};
};
// Implemented by the eye views (stereo_probe.cpp): what the eye views show
// this frame (0 off the game's main thread). 0: none (the headset shows the game screen), 1:
// immersive, with the head the HUD goes in front of, 2: the flat screen (the
// eyes copy the game's camera; the HUD stays where the game put it).
uint32_t eyes(Head& out);
// The HUD row (the XR worker sets it every frame; probes through SpidyHudSet):
// 0 off, 1-3 its size.
void setSize(int size);
int size();
// The panel's orientation seen from the head (relative: Follow's orientation
// in the head's axes) for the eye command `serial`, as the XR worker sends the
// command; and the one for the command a frame latched (main thread). Without
// one the panel faces the way the head does.
void follow(uint64_t serial, const Quat& relative);
bool followed(uint64_t serial, Quat& relative);
// An eye's draw of the panel left out while the HUD is off (stereo_probe).
void hiddenDraw();
// Installs the texture, second-movie and marker hooks; 0 or an error (95xx).
uint32_t start(uintptr_t base);
// The game's HUD follower (main thread), whose texture binding a new panel
// texture needs.
void follower(uintptr_t follower);
// Each frame whose eye views the headset shows (immersive or flat): its second
// movie goes on the panel until 100 ms after the last one.
void eyesShown();
// Puts the game's own texture size back (waiting for the game's frames, up to
// `waitMs`), then removes the hooks.
uint32_t stop(uint32_t waitMs);
} // namespace spidy::native_hud
