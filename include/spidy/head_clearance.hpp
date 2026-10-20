#pragma once
#include "math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace spidy {
// The eyes kept out of what the world is built of.
//
// The eyes go where the headset is, over the player's feet as the game has
// them: a head that leans or walks in the room goes with it, through
// whatever is there. Nothing kept it out (the game's crawl had its stand-off
// and a vent its roof, and nothing else did): on Spidy's own wall the body
// runs 0.9 m off the facade, so leaning 0.7 m toward it, or a cornice or a
// bay window at head height, put the eyes inside the building ("I do a wall
// run on some buildings and i get a full pov of the inside of the building",
// October 15), and on the ground a step toward a wall did too.
//
// So every surface the rays find (rays the session casts from inside the
// body to past the eyes, and from the eyes ahead, to each side and up) and
// the wall that holds the player hold the eyes `margin` off themselves, at
// once: the eyes, and the hands and the body placed with them, move out of
// it, along it as the head slides. Once the head is clear again the push
// lets go over releaseSeconds, never so fast that the eyes go back in. It
// never moves the eyes behind the point in the body the rays start from.
//
// A surface is a plane where it was found, and a plane goes on past the
// surface's end: round an outside corner the wall left behind would push
// the eyes back out sideways through the open air. So only a surface the
// body itself is in front of holds the eyes off it (round the corner the old
// wall is behind him), and the wall that holds the player counts only while
// it holds him.
class HeadClearance {
  public:
    // How far the eyes keep from a surface. The game's near plane is 0.1 m
    // from the eye, and at a headset's widest its corners reach 0.13 m out.
    static constexpr float margin = .2f;
    // A surface is kept this long after the rays last found it: their batch
    // comes back a frame or two late, and each ray finds one surface at most.
    // The wall that holds the player is given again every frame it does.
    static constexpr float keepSeconds = .3f;
    // Only a surface found this near the eyes pushes them: its plane goes on
    // past where the ray met it. The wall that holds the player, found under
    // his feet, reaches as far as his height on it.
    static constexpr float reach = 1.5f, heldReach = 3.f;
    // The push lets go over this long (63%).
    static constexpr float releaseSeconds = .15f;
    // Surfaces kept at most, and the rays a frame casts.
    static constexpr unsigned kept = 8, rays = 5;
    struct Ray {
        Vec3 origin{}, direction{};
        float distance{};
    };
    // A surface a ray met, facing it (its normal back toward where the ray
    // came from), or the wall that holds the player.
    struct Surface {
        Vec3 point{}, normal{};
    };
    // A surface found this frame; held: the wall that holds the player. A
    // normal that is not unit, or a point that is not a place, is nothing.
    void found(Surface s, bool held = false) {
        const float n = length(s.normal);
        if (!finite(s.point) || !finite(s.normal) || std::abs(n - 1) > .01f)
            return;
        const Plane fresh{s.point, s.normal, 0, held ? heldReach : reach, held ? 0.f : keepSeconds};
        // The same surface found again: kept, where it was found now.
        for (auto& p : planes_)
            if (p.age <= p.keep && dot(p.normal, s.normal) > .985f &&
                std::abs(dot(s.point - p.point, p.normal)) < .05f) {
                p = {s.point, s.normal, 0, std::max(p.reach, fresh.reach), std::max(p.keep, fresh.keep)};
                return;
            }
        auto* oldest = &planes_[0];
        for (auto& p : planes_)
            if (p.age > oldest->age)
                oldest = &p;
        *oldest = fresh;
    }
    // seconds: since the previous update. body: a point inside the player's
    // body, the rays' start, where no wall is. eyes: the eyes' middle where
    // the headset puts it. Returns how far the eyes (and all placed with
    // them) move.
    Vec3 update(float seconds, Vec3 body, Vec3 eyes, bool tightCorners = false) {
        const float dt = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .1f) : 0.f;
        // Older by this update once it is done: found this frame, a surface
        // counts now whatever it keeps.
        struct Age {
            std::array<Plane, kept>& planes;
            float dt;
            ~Age() {
                for (auto& p : planes)
                    p.age += dt;
            }
        } age{planes_, dt};
        if (!finite(eyes)) {
            push_ = {};
            return push_;
        }
        body_ = finite(body) ? body : eyes;
        eyes_ = eyes;
        // The push the surfaces ask of the eyes as the headset puts them, and
        // the push now let go toward it.
        const unsigned passes = tightCorners ? 32u : 2u;
        const Vec3 wanted = clear(eyes, {}, passes);
        push_ += (wanted - push_) * (1 - std::exp(-dt / releaseSeconds));
        // Never inside: each surface holds the eyes off itself at once.
        push_ = clear(eyes, push_, passes);
        // Nor behind the body's middle, along the way from it to the eyes.
        const Vec3 out = eyes - body_;
        if (length(out) > 1e-3f && dot(eyes + push_ - body_, out) < 0)
            push_ = body_ - eyes;
        if (length(push_) < 1e-4f)
            push_ = {};
        frames_ += length(push_) > .01f;
        most_ = std::max(most_, length(push_));
        return push_;
    }
    // The rays for the next batch: from the body to past the eyes where the
    // headset puts them (so a push never hides the surface that asked for
    // it), and from the eyes as pushed along `look`, to each side of it and
    // `up`.
    std::array<Ray, rays> next(Vec3 look, Vec3 side, Vec3 up, float anticipation = 0.f) const {
        std::array<Ray, rays> out{};
        const Vec3 at = eyes_ + push_;
        const Vec3 way = eyes_ - body_;
        if (length(way) > .05f)
            out[0] = {body_, normalized(way), length(way) + margin};
        const Vec3 ways[]{look, side, side * -1.f, up};
        const float extra = std::isfinite(anticipation) ? std::clamp(anticipation, 0.f, 1.f) : 0.f;
        for (unsigned i = 0; i < 4; ++i)
            if (finite(ways[i]) && length(ways[i]) > .5f)
                out[i + 1] = {at, normalized(ways[i]), margin + (i == 0 ? .1f : .02f) + extra};
        return out;
    }
    // The push now, frames pushed more than a centimetre, the most so far.
    Vec3 push() const {
        return push_;
    }
    uint32_t pushedFrames() const {
        return frames_;
    }
    float most() const {
        return most_;
    }
    // Nothing known any more (another player, a menu's long break).
    void reset() {
        planes_ = {};
        push_ = body_ = eyes_ = {};
    }

  private:
    struct Plane {
        Vec3 point{}, normal{};
        float age = 1e9f, reach{}, keep{};
    };
    // The least further push past `from` that holds eyes + push `margin` off
    // every surface near them that the body is in front of, along their
    // normals. Close climbing needs more passes where angled walls oppose
    // one another: clearing one can put the eyes back into the other.
    Vec3 clear(Vec3 eyes, Vec3 from, unsigned passes) const {
        Vec3 push = from;
        for (unsigned pass = 0; pass < passes; ++pass) {
            const Vec3 before = push;
            for (const auto& p : planes_) {
                if (p.age > p.keep || length(p.point - (eyes + push)) > p.reach ||
                    dot(body_ - p.point, p.normal) <= 0)
                    continue;
                const float d = dot(eyes + push - p.point, p.normal);
                if (d < margin)
                    push += p.normal * (margin - d);
            }
            if (length(push - before) < 1e-5f)
                break;
        }
        return push;
    }
    std::array<Plane, kept> planes_{};
    Vec3 push_{}, body_{}, eyes_{};
    uint32_t frames_{};
    float most_{};
};
} // namespace spidy
