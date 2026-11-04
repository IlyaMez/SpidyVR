#pragma once
#include "math.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

// Things in the game a web can catch, found in the component registry (the
// table game_player searches): throwable props (ThrowableHelper), bots
// (enemies and scripted civilians), pedestrians (PedestrianMover) and the
// things that are no bots and that the game webs up all the same (a gun
// turret). A registry scan takes milliseconds, far too long for a physics
// step, so a thread keeps the list and a pick only reads transforms.
//
// What a component is comes from the game's own type information (RTTI):
// the class of a component, or any class it derives from, marks its actor.
// A bot is an actor with a component derived from BotMoverManager, whatever
// moves it: BotMoverManagerGame walks the bots on foot (Hammerhead's and
// Mecha-Hammerhead's derive from it), HoverMoverManager flies the ones that
// hover (Doc Ock's derives from it). Until October 9 Spidy looked for
// BotMoverManagerGame alone, so the webs and the fists passed through flying
// enemies (a player's report: the flying Sable agents) and web balls did
// nothing to them.
namespace spidy::game_targets {
// device: an actor that is no bot and has the component the game webs
// things up with, a StatusEffectTrackerWebbed, whatever its class: the web
// shooter's balls web it up as the game's own shots do. Its marker component
// is that tracker. The user, October 12, at the two gun turrets of Fisk's
// office (EnemyTurret: 3 hit points, a tracker with threshold 30 that the
// game's own web hit adds 10 to, a PhysicsComponent, the transform 2.1 m
// above the floor): "the game tells me to press rb repeatedly to web but it
// doesnt work", and then "can you implement a global solution so we wont
// need to redo every enemy?" So no class is named for it.
enum class Kind : uint8_t { throwable = 1, bot = 2, pedestrian = 3, device = 4 };
// What else an actor is, from its other components: a thug (ThugBot and the
// bosses derived from it), a civilian (CivilianBot; scripted crime victims
// are bots too), one the game webs up (StatusEffectTrackerWebbed), a
// breakable prop (BreakableSystemComponent: its base stays held when it is
// freed), one on the hero's side (AllyBot, the police at a crime;
// MissionFollowBot, a mission's companion), a bot that flies (its mover
// manager is a HoverMoverManager), one nobody fights (BirdBot, a bird;
// Helicopter; SilverSableCraftBot, Silver Sable's aircraft).
enum Trait : uint32_t {
    thug = 1,
    civilian = 2,
    webbable = 4,
    breakable = 8,
    ally = 16,
    hover = 32,
    neutral = 64
};
// A bot's size: the capsule his mover collides with, above his actor's
// transform, as the game builds it (1fbba90) from his mover manager's body
// (MoverManager +0xdb8, a MoverBodySize: the lower and upper spheres'
// centres at +0xdc0 and +0xdc4, the upper one scaled by +0xdf8, their radius
// at +0xdc8). A street thug's (0.85, 1.15, 0.45) spans 0.4 m to 1.6 m above
// his feet; the hero's (0.86, 1.3, 0.4) 0.46 m to 1.7 m.
struct Size {
    float low = .4f, high = 1.6f, radius = .45f;
    // A height above a street thug's feet, or a width, tuned on him, carried
    // over to this bot: heights stretch between the capsules' ends, widths
    // with their radii. A street thug's own come back unchanged; a drone's
    // shrink, a heavy's grow.
    float height(float thug) const {
        return low + (thug - .4f) * (high - low) / 1.2f;
    }
    float width(float thug) const {
        return thug * radius / .45f;
    }
};
struct Candidate {
    uint64_t component{}, record{}; // the marker component and its actor record
    uint32_t handle{};              // the component's registry handle
    Kind kind{};
    uint64_t vtable{}; // the marker component's class
    // Components of the same actor the web needs: its PhysicsComponent (a
    // prop is freed through it) and its SyncStaticStateMachine (a bot is
    // flung through it). 0 when the actor has none.
    uint64_t physics{}, machine{};
    uint32_t traits{}; // Trait bits
    Size size{};       // a bot's, as his mover manager says; a street thug's otherwise
    // The actor's health component (BotHealth, else Health), 0 for none.
    uint64_t health{};
};
// The two components the web moves an actor through, by their own classes
// (image offsets of their vtables), and the class of a mover's body.
constexpr uint64_t physicsComponent = 0x3d0a460, syncStaticStateMachine = 0x4f843d0,
                   moverBodySize = 0x500e870;
// The classes that keep an actor's hit points (a float at +0x88): a bot's own
// and the plain one (tools/probe_combat.py: a street thug's 50 went to 19.34
// with one punch, October 9).
constexpr uint64_t botHealth = 0x3839e98, plainHealth = 0x3839e08;
// A bot the hero fights: every bot but the civilians, his side and the
// neutral ones, so every enemy class there is (ThugBot and the bosses derived
// from it, GrenadierBot, SlayerBot, SwordsmanBot, CorruptedBot, DroneBot and
// the others) without naming any. The webs pull, shoot and hurt only these.
inline bool enemy(const Candidate& c) {
    return c.kind == Kind::bot && !(c.traits & (civilian | ally | neutral));
}
// A bot the game has in play. The thugs of a fight's later waves and of
// other rooms exist long before they appear: unseen where they will spawn,
// with their hit points and every component, but with no state in their
// state machine (its first layer's state at +0x70, in use at +0x80) and a
// BotAttackResponseManager not set up yet (a blow to one goes out as
// nobody's: native_bodies). In the hall of the first mission's shield fight,
// October 12: 22 enemy bots within 60 m of the player, 5 of them in play and
// 17 waiting, three of those 1.4 m from where he stood; a fist, a web or a
// punch across the room took each of them for an enemy. A bot without a state
// machine counts as in play.
bool inPlay(const Candidate&);
// A bot who is up: in play, with hit points left where his health can be
// read, and in none of the game's states for a bot who is down
// (BotStateDeathAnim, then BotStateRagdoll, as a thug knocked out went that
// day with 0 hit points; BotStateWallStuck; the classes derived from them).
// A fist lands on such a bot alone: a body on the floor is nobody to hit.
bool up(uintptr_t base, const Candidate&);
// A bot whom something else moves, not his own legs: in one of the game's
// states for that (BotStateAttachToActor, a car's seat among them;
// BotStateCarried; a cinematic's; BotStateNull; the guest of another's
// synced animation; a slotted interaction;
// BotStateFrozen; BotStateRequestTakedownYank). Pulled out of that, the scene
// he was in breaks: on October 18 a web took the thugs out of a car in a
// chase, the game's own takedown never finished, the crime never ended and
// the car drove on by itself; on October 15 one pulled hurt police officers
// sitting in a burning car around the street.
bool held(uintptr_t base, const Candidate&);
// An enemy in the fight: an enemy who is up and free to fight - his own AI
// moves him (not held). What a punch across the room, a web, a web ball and
// the blow of a thrown thug take. The user, October 12, with webs still
// taking the knocked out: "bodies are still targetable by hands (for webbing
// i guess), they shouldnt be"; and since October 18 one rule for the car and
// the police: only enemies who are free to fight.
inline bool fighting(uintptr_t base, const Candidate& c) {
    return enemy(c) && up(base, c) && !held(base, c);
}
// A bot the game has webbed up: in its BotStateWebStruggle (a class derived
// from it included), the state a bot's StatusEffectTrackerWebbed puts him in
// once the webbing reaches his threshold (docs/REFERENCE.md: kWebImpact with
// kWebEncase at 25-100 webbed thugs up). What the web tie asks of its target.
bool cocooned(uintptr_t base, const Candidate&);
// A bot the game has stuck to a surface in his cocoon: BotStateWallStuck (a
// down state, so not up()). What the web tie ends in.
bool wallStuck(uintptr_t base, const Candidate&);
// A device that still stands: one with hit points left, where it has any
// (a turret torn off and thrown has none).
bool standing(const Candidate& device);
// Every live candidate of the kinds in `kinds` (bit 1 << Kind).
bool scan(uintptr_t base, uint32_t kinds, std::vector<Candidate>& out);
// What a component of the class whose vtable this is makes its actor, in the
// image at `base`: the kinds it marks (bit 1 << Kind) and, from bit 8 up, the
// traits it gives. 0 for a vtable without type information.
uint32_t classOf(uintptr_t base, uintptr_t vtable);
// A bot's size from his mover manager (the marker component): false, and
// `out` left as it was, when it holds no body or an implausible one.
bool sizeOf(uintptr_t base, uint64_t moverManager, Size& out);
// The candidate is still registered under its handle, on its actor.
bool live(uintptr_t base, const Candidate&);
// The registered component a handle names, 0 for none.
uint64_t resolve(uintptr_t base, uint32_t handle);
// A bot's MoverStandard: its mover manager names it at +0xdb4 (a field of
// MoverManager's, so every bot mover manager has it), as HeroMoverManager
// does the player's. 0 when there is none.
uint64_t botMover(uintptr_t base, const Candidate&);
// Position of the candidate's actor (its transform's translation).
bool position(const Candidate&, Vec3& out);
// The candidate's hit points: false, and `out` left as it was, for an actor
// without a health component or with one that cannot be read.
bool health(const Candidate&, float& out);

class Watch {
  public:
    ~Watch() {
        stop();
    }
    void start(uintptr_t base, uint32_t kinds);
    void stop();
    // The latest list, and how many scans have completed.
    uint64_t current(std::vector<Candidate>& out) const;

  private:
    void run();
    uintptr_t base_{};
    uint32_t kinds_{};
    mutable std::mutex lock_;
    std::condition_variable wake_;
    std::thread thread_;
    bool stopping_{};
    std::vector<Candidate> list_;
    uint64_t scans_{};
};
} // namespace spidy::game_targets
