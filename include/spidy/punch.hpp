#pragma once
#include "math.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Punching with the controllers. Engine independent: a fist that meets a
// character fast enough lands a punch, as hard as the fist was moving toward
// it. The game turns a punch into its own melee damage (game_punch), the lab
// knocks its thugs over with it.
//
// Speed is the hand's own, relative to the player's body: what the arm does,
// not the swing or the run the player is on. A fist moving with the player at
// 30 m/s punches nobody; one that hits a thug while the player stands is
// measured by how fast it moved toward him.
namespace spidy {
struct PunchTarget {
    std::uint64_t id{};
    Vec3 feet{}; // the character's base on the ground
    float height = 1.8f, radius = .35f;
};
// One hand in one sample.
struct PunchHand {
    bool tracked{};
    // The fist in the world; and the same hand relative to the player (the
    // head, in tracking space turned to the world), whose change is the arm's
    // own motion.
    Vec3 fist{}, relative{};
    // The hand's web holds something: its fist does not punch.
    bool busy{};
};
struct PunchConfig {
    float fistRadius = .08f;
    // Slower than this, a touch is not a punch; at fullSpeed it is as hard
    // as a punch gets.
    float minSpeed = 2.2f, fullSpeed = 7.5f;
    // A blow must go into the character: the cosine between its direction
    // and the line to his middle (0.6 of his height) or to his head (0.2 m
    // below the top) is at least this. Grazes across his front, and fists
    // pulled back out of him, are no punches.
    float minInward = .3f;
    // A fist lands one punch per swing: it may punch again after it slowed
    // below rearmSpeed or pulled back, or after rearmTime.
    float rearmSpeed = 1.2f, rearmTime = .45f;
    // The hand's speed is measured over about this long (seconds): long
    // enough to steady tracking noise (millimetres at 72-120 Hz, a tenth of
    // a metre per second), short enough not to lag a fist already at him.
    float smoothing = .008f;
    // Damage at minSpeed and at fullSpeed (the game's hit points).
    float minDamage = 10, maxDamage = 40;
    // An upward blow this steep (the sine of its pitch) and this strong
    // is an uppercut.
    float uppercutRise = .6f, uppercutStrength = .4f;
};
// The game's reaction levels (DamageRequest Knockback, decoded from its
// reflection names): what a blow does to the one it hits.
enum class Knockback : std::uint8_t {
    None = 0,
    Twitch = 1,
    Stagger = 2,
    Knockback = 3,
    Knockdown = 4,
    FlyBack = 5,
    Airborne = 6,
    PopUp = 7,
    AirJuggle = 8,
    SuperFlyBack = 9
};
struct PunchEvent {
    int hand{};
    std::uint64_t target{};
    Vec3 point{};     // where the fist met the character
    Vec3 direction{}; // the blow's direction (world, unit)
    float speed{};    // the fist toward the character, metres per second
    float strength{}; // 0 at minSpeed, 1 at fullSpeed and above
    float damage{};
    Knockback knockback{};
};
class Punches {
  public:
    explicit Punches(PunchConfig config = {});
    // One sample, `seconds` after the previous one (0 repeats it). Appends
    // the punches it lands.
    void update(float seconds, const std::array<PunchHand, 2>& hands, std::span<const PunchTarget> targets,
                std::vector<PunchEvent>& out);
    void reset();
    // A hand's speed relative to the player, metres per second.
    float speed(int hand) const {
        return length(hands_[static_cast<size_t>(hand)].velocity);
    }
    const PunchConfig& config() const {
        return config_;
    }
    // The blow a fist moving at `speed` along `direction` lands.
    PunchEvent blow(Vec3 direction, float speed) const;

  private:
    struct Hand {
        bool seen{}, armed = true;
        Vec3 fist{}, relative{}, velocity{};
        float sinceHit{};
        std::uint64_t lastTarget{};
    };
    PunchConfig config_;
    std::array<Hand, 2> hands_{};
};
// Where a moving sphere first touches an upright capsule (feet, height,
// radius), as a share of its travel; false when it does not.
bool sweepCapsule(Vec3 from, Vec3 to, float sphereRadius, const PunchTarget& target, float& share, Vec3& point);
} // namespace spidy
