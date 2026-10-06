#pragma once
#include "math.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

// Things in the game a web can catch, found in the component registry (the
// table game_player searches): throwable props (ThrowableHelper), bots
// (enemies and scripted civilians, BotMoverManagerGame) and pedestrians
// (PedestrianMover). A registry scan takes milliseconds, far too long for a
// physics step, so a thread keeps the list and a pick only reads transforms.
namespace spidy::game_targets {
enum class Kind : uint8_t { throwable = 1, bot = 2, pedestrian = 3 };
struct Candidate {
    uint64_t component{}, record{}; // the marker component and its actor record
    uint32_t handle{};              // the component's registry handle
    Kind kind{};
    // Components of the same actor the web needs: its PhysicsComponent (a
    // prop is freed through it) and its SyncStaticStateMachine (a bot is
    // flung through it). 0 when the actor has none.
    uint64_t physics{}, machine{};
};
// The marker component class of each kind (image offsets of their vtables).
constexpr uint64_t throwableHelper = 0x38489c0, botMoverManager = 0x38533e0, pedestrianMover = 0x3911c10;
constexpr uint64_t physicsComponent = 0x3d0a460, syncStaticStateMachine = 0x4f843d0;
// Every live candidate of the kinds in `kinds` (bit 1 << Kind).
bool scan(uintptr_t base, uint32_t kinds, std::vector<Candidate>& out);
// The candidate is still registered under its handle, on its actor.
bool live(uintptr_t base, const Candidate&);
// The registered component a handle names, 0 for none.
uint64_t resolve(uintptr_t base, uint32_t handle);
// A bot's MoverStandard: its BotMoverManagerGame names it at +0xdb4, as
// HeroMoverManager does the player's. 0 when there is none.
uint64_t botMover(uintptr_t base, const Candidate&);
// Position of the candidate's actor (its transform's translation).
bool position(const Candidate&, Vec3& out);

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
