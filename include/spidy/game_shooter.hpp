#pragma once
#include "shooter.hpp"
#include "swing.hpp"
#include <cstdint>

// The web shooter in the game: a pull of a free hand's trigger fires the
// game's own web-shooter shot (the web ball its R1 gadget fires) from that
// hand. Pulls come from the swing's input samples (Shooter), thugs to aim at
// from a watch of the game's bots (game_targets); the shot itself is the
// game's, fired on the main thread through the hero's gadget.
//
// The gadget. The hero's web shooter is a WeaponWebShooter component (vtable
// 391c950, 0x6b8 bytes) on a weapon actor of its own, beside the hero; one is
// registered while Spider-Man is played. Its R1 shot, measured October 7 by
// tracing the game's own fire with the Impact Web: the hero's weapon state
// (HeroWeaponStateFiringLocal) hands the weapon a fire event (vtable +0x108,
// WeaponGame 0xe3bd80), on the main thread. That stores the event's aim in
// the weapon (+0x670...) and fires (0x2150460): SpawnShot (vtable +0x110,
// 0x2150830) takes the muzzle (vtable +0x160, 0x2150c40: the emitter
// component at +0x1b8 + 0x38 * index, the hero's wrists) and spawns the shot
// actor (+0x2f8, 0x215c190), a ShotWebShooter (vtable 3907d30) whose handle
// the weapon keeps at +0x518 + 4 * index. Fired that way with Spidy's own
// event and muzzle, the shot left the scripted hand and flew straight to the
// aim point at 53-61 m/s, ending there or at the first surface it met.
//
// The event (0x44 bytes): the game's header (+0..+9); +0xa the emitter (0
// right wrist, 1 left); +0xb 1, a shot; +0xc the shot's id, from the game's
// own counter (0x215f040 on the shot table 656d9f0, which registers it in a
// ring of 1024 slots a player, clearing what an old shot left in its slot);
// +0x10 the target actor's reference (0x1f7b8e0 makes it from the actor);
// +0x18 which of its aim points; +0x20 the aim point; +0x2c the facing,
// level; +0x38 1: no target, the aim point counts; +0x39 1: resolve the
// target, whose aim point then replaces it; +0x3b 1, as the game sets it;
// +0x3c and +0x40 the web shooter's own options, which it fills itself
// (vtable +0x100, 0xe55310). Spidy's shots take no gadget ammo.
//
// The hit. A shot fired this way does nothing where it lands (measured
// October 8 at a street crime): its damage action, from the shot's own
// DamageData, carries no damage, type and status, and its handler
// (ShotWebShooter vtable +0xd8, 0xd2a570) webs a limb only for the hero's
// melee web shots (the anim event HeroAnimWebShooterFireEvent sets the
// weapon's options, +0x6ac/+0x6a8, around its fire at 0xe55800). So Spidy
// hooks that handler: when one of its own shots (the ShotWebShooter keeps
// its registry handle at +0x14 and its shot id at +0x104) collides
// (event 0xd930bcb2), the event's HitActor (0x21eb297b), HitPosition
// (0x568973e3) and HitNormal (0xc3272bc7) say what it struck, read as the
// handler reads them (1bcf3e0, 1f9db60, 1f7b760). A thug the game webs up
// takes the webbing the game's web hits deal: a kWebImpact blow with
// webbingPerHit of kWebEncase (native_bodies::Damage), enough at the third
// hit in quick succession to web a street thug up (his threshold is 30).
// A throwable prop that is no breakable is knocked along the shot
// (native_bodies, as a throw launches it).
namespace spidy::game_shooter {
// What a hit does.
constexpr float webbingPerHit = 11, webDamage = .5f, pushSpeed = 3.5f, pushLift = 1;
struct Hand {
    uint64_t shots{};      // fired by this hand
    uint64_t lastTarget{}; // the actor record its latest shot went to, 0 for none
};
struct Data {
    uint32_t magic = 0x53484f44, version = 2, bytes = sizeof(Data), status{};
    int64_t sequence{};
    // Input samples seen; pulls that asked for a shot; shots the game fired;
    // requests dropped (no web shooter, no player, stale, refused); shots
    // aimed at a thug, and those whose target the game took.
    uint64_t samples{}, requested{}, fired{}, dropped{}, targeted{}, resolved{};
    // The hero's WeaponWebShooter now (0: none found), and the main-thread
    // frames the module has seen.
    uint64_t weapon{}, frames{};
    uint32_t bots{}, error{};
    Hand hands[2];
    // The latest shot: where it left from and went to, and the ShotWebShooter
    // component the game spawned for it.
    Vec3 lastOrigin{}, lastAimPoint{};
    uint64_t lastShot{};
    // Spidy's shots that struck something, the webbing blows they dealt
    // thugs, the props they knocked, and the actor record the latest struck
    // (0: the world).
    uint64_t collisions{}, webbed{}, pushed{}, lastHit{};
};
static_assert(sizeof(Hand) == 16 && sizeof(Data) == 192);
// Starts the bot and gadget watches and the main-thread hooks.
uint32_t start(uintptr_t base);
uint32_t stop();
bool running();
// One input sample in the swing's callback (seconds since the previous one,
// 0: the same sample again). busy: a bit per hand whose web holds something
// or swings the player. record and feet: the player's actor and where it
// stood for this sample; a shot leaves from where the hand was relative to
// it when the main thread fires it.
void update(float seconds, const Input&, uint32_t busy, uint64_t record, Vec3 feet, const WorldQueries&);
// Input lost focus (a menu, the flat screen, a lost headset): a trigger
// held through it is no pull when play resumes.
void cancel();
Data data();
// Wire formats: SpidyShooterStart's configuration, and SpidyShooterTest's
// shot, which a probe fires from any point (hand 0 left, 1 right) toward an
// aim point, or at a target's actor record.
struct Config {
    uint32_t magic = 0x53484f43, version = 1, bytes = sizeof(Config), pid{};
    uint64_t base{};
};
struct Test {
    uint32_t magic = 0x53484f54, version = 1, bytes = sizeof(Test), hand = 1;
    Vec3 origin{}, aimPoint{};
    uint64_t target{};
};
static_assert(sizeof(Config) == 24 && sizeof(Test) == 48);
} // namespace spidy::game_shooter
