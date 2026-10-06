#pragma once
#include "math.hpp"
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
// Its bodies are new then; the web gives them its velocity with
// hknpWorld::setBodyLinearVelocity (2e48b80). Until the rebuilt prop has its
// keyframe record, Spidy sets the instance from the body itself
// (Instance::SetMatrix 191c0e0).
//
// Time. The game steps Havok once a frame, by the length at 609a560, which
// only its TimeScaleSystem changes, during its time effects; otherwise it
// stays 1/30 s. At 240 frames a second a freed prop fell 32 m in 0.3 s:
// physics ran 8 times faster than real time, and at a VR frame rate it runs
// fps/30 times. The web works in real time, so velocities given to a body are
// divided by that ratio (the physics step over the real time since the last
// one) and velocities read from one are multiplied by it.
//
// An actor here is what a component record's first field points to:
// transform at +0, handle +0x64, model override +0xb6 (s16), physics system
// +0xe0 (body ids +0x28, count +0x30), primary body +0xe8. Its handle names
// it in the table at 7a436e8, as a body's userData (+0x98) does.
namespace spidy::native_bodies {
constexpr unsigned slots = 4;
uint32_t start(uintptr_t base);
uint32_t stop();
// Frees the actor (once, unless it is free already) and gives its bodies
// `velocity` from the next step on. The slot ends with the lease: renew it
// every step the web acts. `component` is the actor's PhysicsComponent.
// False when no slot is free.
bool drive(uint64_t actor, uint64_t component, Vec3 velocity, uint32_t leaseMs);
// No more velocity for this actor; physics keeps it moving.
void release(uint64_t actor);
// Where a freed actor's primary body will be when the step in flight ends,
// and its velocity then: from the snapshot taken before that step on the
// main thread, carried through the step with the velocity it was given (or
// with gravity alone). `measured`: the velocity the body ended the step
// before with, which differs from what it was given after a collision. All
// in real time. False until the actor is freed and seen in a step.
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
struct Counters {
    // Physics steps seen; props freed and rebuilt; body velocities set;
    // instance poses Spidy set; bots flung and flights steered; requests the
    // game refused, and slots whose lease lapsed.
    uint64_t steps{}, frees{}, rebuilds{}, writes{}, follows{}, flings{}, steers{}, rejected{}, expired{};
};
Counters counters();
// Physics steps seen so far, and the real time the latest took (seconds).
uint64_t physicsSteps(float& dt);
// Physics time per real second, as the latest steps ran: 1 when the game
// steps its physics at the frame rate, more when it steps 1/30 s faster.
float timeScale();
} // namespace spidy::native_bodies
