#pragma once
#include "swing.hpp"
#include <array>
#include <cstdint>
#include <span>

// The web shooter with the controllers. Engine independent: a pull of a free
// hand's trigger shoots one web ball from that hand, where it points. A
// character close to that line takes it (aim assist); otherwise it goes to
// the first surface the line meets, or into open air at the shooter's range.
// The game fires its own web-shooter shot along it (game_shooter).
//
// A hand whose web holds something or swings the player is busy: its trigger
// reels that web in, and shoots nothing.
namespace spidy {
struct ShooterConfig {
    // A pull: the trigger past `press` after it was below `release` (the
    // swing's reel uses the same two).
    float press = .65f, release = .35f;
    // One hand shoots at most once in this long (seconds).
    float interval = .12f;
    // The ball leaves this far ahead of the aim pose (the knuckles), clear of
    // the hand.
    float muzzle = .08f;
    // Without a character to take it: the first surface on the hand's line
    // within `range`, aimed `beyond` it (a shot ends at its aim point, so it
    // must reach the surface to strike it), else open air at `range`. The
    // game's shot flies about 60 m (a second) before it ends.
    float range = 60, beyond = .4f;
    // A character's centre within `assistAngle` of the line (radians), or
    // within his own radius of it, nearer than `assistRange`, with nothing
    // between: the ball goes to him. The one nearest the line wins.
    float assistAngle = .12f, assistRange = 45;
};
struct ShooterHand {
    bool tracked{};
    Pose aim{}; // world, -Z forward
    float trigger{};
    bool busy{};
};
struct ShooterTarget {
    std::uint64_t id{};
    Vec3 centre{}; // where a ball aimed at him goes
    float radius = .45f;
};
struct ShotRequest {
    int hand{};
    Vec3 origin{}, direction{}, aimPoint{};
    std::uint64_t target{}; // the character it goes to, 0 for none
    bool surface{};         // the aim point is on a surface, not in open air
};
class Shooter {
  public:
    explicit Shooter(ShooterConfig config = {});
    // One sample, `seconds` after the previous one (0 repeats it): a bit per
    // hand whose pull shoots now.
    std::uint32_t update(float seconds, const std::array<ShooterHand, 2>& hands);
    // Where a shot along this aim goes (the choice a pull makes).
    ShotRequest aim(int hand, Pose aim, std::span<const ShooterTarget> targets, const WorldQueries& world) const;
    void reset();
    const ShooterConfig& config() const {
        return config_;
    }

  private:
    struct Hand {
        // Pulled, with hysteresis. A trigger already pulled when the shooter
        // starts, or when the hand is tracked again, is no pull.
        bool held = true;
        float since = 1e3f; // seconds since this hand's last shot
    };
    ShooterConfig config_;
    std::array<Hand, 2> hands_{};
};
} // namespace spidy
