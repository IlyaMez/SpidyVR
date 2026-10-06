#pragma once
#include "math.hpp"
#include "web_grab.hpp"
#include <cstdint>

// What the web moves on the game's main thread, inside hknpWorld::preCollide
// (2e54300): there the world is idle, before the step that uses the change.
// The web grab itself runs in a ray callback that may be on a worker thread;
// it queues commands here, each with a lease.
//
// Props. A prop rests on static or keyframed Havok bodies that the game
// places itself. The game's own pick-up-and-throw (6a1620) frees one with
// PhysicsComponent::SetFreebody (1810530) and Physics::SetMode debris
// (1835780, mode 3) in the physics record table at [609a570] + 1ea98. A prop
// built static has no keyframe record, the only thing through which the
// game's PostStep (182d230) moves an actor's instance after its body, so
// Spidy also asks for a physics rebuild (1830930, or 1834b70 with the
// actor's model override), which builds it again in debris mode with one.
// Its bodies are new then. Until the rebuilt prop has its keyframe record,
// Spidy sets the instance from the body itself (Instance::SetMatrix 191c0e0).
// The game draws a rebuilt prop from its keyframe record's root body (+0x9c,
// an s16 index into the physics system's bodies) by the offset its first
// sync takes (+0x40). Some throwables are breakables of two bodies: a base
// that stays held on its unbroken stage, whatever velocity it is given, and
// a free top piece the whole prop is drawn from (measured October 6: see
// docs/WEB-GRAB.md). Only the top piece flies.
//
// The web. Its command (web_grab's TargetCommand) is a law, evaluated here
// each step against the prop's actual motion, after the contacts of the step
// before: a towed crate drags on the ground, a held one stops at a wall, a
// yanked one that strikes a railing bounces off it. Every body of the prop
// gets the same change of velocity (hknpWorld::setBodyLinearVelocity
// 2e48b80) and, where the command turns it, of angular velocity
// (setBodyAngularVelocity 2e48830, world axes; a motion keeps it in body
// axes at +0x50, its orientation at +0x10).
//
// Time. The game steps Havok once a frame, by the length at 609a560, which
// only its TimeScaleSystem changes, during its time effects; otherwise it
// stays 1/30 s. At 240 frames a second a freed prop fell 32 m in 0.3 s:
// physics ran 8 times faster than real time, and at a VR frame rate it runs
// fps/30 times. Spidy keeps the props it moves in real time: their
// velocities are real ones divided by that ratio (the physics step over the
// real time it stands for), and Havok's gravity is replaced by real gravity
// for the real time of the step. Contacts, friction and bounce are ratios of
// velocities, so the game's own collision response stays right in real time.
// A prop the web lets go of stays in real time this way, flying, bouncing
// and sliding by the game's physics, until it comes to rest.
//
// An actor here is what a component record's first field points to:
// transform at +0, handle +0x64, model override +0xb6 (s16), physics system
// +0xe0 (body ids +0x28, count +0x30), primary body +0xe8. Its handle names
// it in the table at 7a436e8, as a body's userData (+0x98) does.
namespace spidy::native_bodies {
constexpr unsigned slots = 4;
uint32_t start(uintptr_t base);
uint32_t stop();
// Frees the actor (once, unless it is free already) and moves its bodies by
// the web's command from the next step on. A Rope or Follow command lasts
// until its lease ends: renew it every step the web acts. A Launch applies
// once; a thrown one ends the web. `component` is the actor's
// PhysicsComponent. False when no slot is free.
bool drive(uint64_t actor, uint64_t component, const TargetCommand& command, uint32_t leaseMs);
// No more web on this actor (a launch not yet applied still is): it flies on
// by physics, in real time, until it rests.
void release(uint64_t actor);
// Where a freed actor's root body will be when the step in flight ends, and
// its velocity then: from the snapshot taken before that step on the main
// thread, carried through that step as the web or gravity moves it, contacts
// aside. `measured`: its velocity as that step began, after the step before
// had struck whatever it struck. All in real time. False until the actor is
// freed and seen in a step.
bool predicted(uint64_t actor, Vec3& centre, Vec3& velocity, Vec3& measured);
// Bots on a web. BotStateFlung is the game's own launched reaction: the bot
// flails through the air at a velocity, then lands. It is requested through
// the bot's SyncStaticStateMachine (RequestState, vtable slot 13, 20e51c0)
// with the launch velocity in its parameters (+0x44; constructor 556040,
// state type 300440), on the main thread at the next step. While the bot
// flies, the mover drive steers it; asked again while it flies, this only
// replaces the flight's velocity (BotStateFlungLocal +0x94), which is how a
// throw hands the bot back to the game. machine: the bot's
// SyncStaticStateMachine; record: its actor record.
bool fling(uint64_t machine, uint64_t record, Vec3 velocity);
// Whether the machine's state is BotStateFlung now.
bool flung(uint64_t machine);
// Damage dealt through the game's own DamageSystem (the static object at
// 62a4ec0), as its melee and its shockwaves deal it: a direct request
// against one actor (1eb6d60: target handle, hit direction, hit point, hit
// normal; the system keeps them for the DamageEvent's HitDirection,
// HitPosition and HitNormal), whose DamageRequest (0x1a8 bytes) is then
// filled by its reflected fields (Damager +138, Type +13c, Amount +144,
// Knockback +150, KnockbackAmount +154, ImpactImpulse +190, DamageHash
// +19c), each with its field's bit in the masks at +8 and +18 (the bit is the
// field's index in the reflection: 8 Damager ... 26 DamageHash). The system's
// pool has no lock: requests are issued on the main thread at the next step.
// The victim reacts as the game has it react to such a blow (its hit
// reaction, knockdown, flight; it loses health and can be knocked out).
// An actor's handle is (generation (+10, 11 bits) << 20) | index (+c, 20
// bits) of its record in the actor table [7a44380] (0xc0 bytes a record).
struct Damage {
    uint64_t victim{}, damager{}; // actor records; no damager: nobody's
    Vec3 point{}, direction{}, normal{};
    float amount{};
    uint32_t type = 1; // DamageType: 1 kMelee, 7 kKinetic
    // Knockback level (0 kNone ... 9 kSuperFlyBack, punch.hpp's Knockback);
    // -1 and negative amounts leave a field as the system defaults it.
    int32_t knockback = -1;
    float knockbackAmount = -1, impulse = -1;
    uint32_t hash{}; // DamageHash; 0 leaves it unset
};
// Queues one for the next physics step. Returns its ticket, 0 when the
// module is off or the queue is full.
uint64_t damage(const Damage&);
// The newest ticket issued: requests are issued in the order queued.
uint64_t damageIssued();
struct Counters {
    // Physics steps seen; props freed and rebuilt; body velocities set;
    // instance poses Spidy set; bots flung and flights steered; requests the
    // game refused, and slots whose lease lapsed; props let go that came to
    // rest; and props flying free in real time now. Damage requests issued,
    // and those dropped (an actor gone, the system's pool full).
    uint64_t steps{}, frees{}, rebuilds{}, writes{}, follows{}, flings{}, steers{}, rejected{}, expired{},
        rested{}, flying{}, damages{}, damageDropped{};
};
Counters counters();
// Physics steps seen so far, and the real time the latest took (seconds).
uint64_t physicsSteps(float& dt);
// Physics time per real second, as the latest steps ran: 1 when the game
// steps its physics at the frame rate, more when it steps 1/30 s faster.
float timeScale();
} // namespace spidy::native_bodies
