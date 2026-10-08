// Punches in the game: fists from the swing's input samples, bots from the
// target watch, blows as the game's own melee damage (game_punch.hpp).
#include "spidy/game_punch.hpp"
#include "spidy/game_targets.hpp"
#include "spidy/native_bodies.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_punch;
extern "C" {
__declspec(dllexport) Data SpidyPunchData;
}
namespace {
// A bot from the feet up, and how far from a fist one may stand to be
// worth testing this sample.
constexpr float botHeight = 1.8f, botRadius = .3f, reach = 3;
uintptr_t base{};
std::atomic<bool> enabled{};
SRWLOCK lifecycle = SRWLOCK_INIT, updating = SRWLOCK_INIT, output = SRWLOCK_INIT;
game_targets::Watch watch;
std::vector<game_targets::Candidate> bots;
std::vector<PunchTarget> inReach;
std::vector<PunchEvent> events;
Punches punches{PunchConfig{}};
// The tracking space's orientation at the last sample: the right stick turns
// it, a flip tilts it.
Quat lastTurn{};
bool turnSeen{};
bool read(uintptr_t p, void* out, size_t n) {
    __try {
        if (p < 0x10000)
            return false;
        std::memcpy(out, reinterpret_cast<void*>(p), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class F> void publish(F change) {
    AcquireSRWLockExclusive(&output);
    InterlockedIncrement64(&SpidyPunchData.sequence);
    change(SpidyPunchData);
    const auto bodies = native_bodies::counters();
    SpidyPunchData.issued = bodies.damages;
    SpidyPunchData.dropped = bodies.damageDropped;
    InterlockedIncrement64(&SpidyPunchData.sequence);
    ReleaseSRWLockExclusive(&output);
}
} // namespace

uint32_t game_punch::start(uintptr_t gameBase) {
    AcquireSRWLockExclusive(&lifecycle);
    uint32_t result{};
    if (!enabled) {
        base = gameBase;
        // The damage requests' main-thread hook; the web grab may have
        // started it already.
        result = native_bodies::start(base);
        if (!result) {
            AcquireSRWLockExclusive(&updating);
            punches.reset();
            turnSeen = false;
            ReleaseSRWLockExclusive(&updating);
            watch.start(base, 1u << static_cast<unsigned>(game_targets::Kind::bot));
            enabled = true;
        }
        publish([&](Data& d) {
            const auto sequence = d.sequence;
            d = {};
            d.sequence = sequence;
            d.status = result ? 3 : 1;
            d.error = result;
        });
    }
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
uint32_t game_punch::stop() {
    AcquireSRWLockExclusive(&lifecycle);
    enabled = false;
    // An update in flight finishes first.
    AcquireSRWLockExclusive(&updating);
    punches.reset();
    turnSeen = false;
    ReleaseSRWLockExclusive(&updating);
    watch.stop();
    publish([](Data& d) { d.status = 0; });
    ReleaseSRWLockExclusive(&lifecycle);
    return 0;
}
bool game_punch::running() {
    return enabled.load(std::memory_order_relaxed);
}
void game_punch::update(float seconds, const Input& in, uint64_t hero, uint32_t busy, const WorldQueries&) {
    if (!enabled.load(std::memory_order_relaxed) || seconds <= 0 || !TryAcquireSRWLockShared(&updating))
        return;
    struct Release {
        ~Release() {
            ReleaseSRWLockShared(&updating);
        }
    } release;
    if (!enabled)
        return;
    watch.current(bots);
    // The fist at the aim pose (the knuckles); its motion relative to the
    // player is the grip relative to the head, turned from tracking space.
    std::array<PunchHand, 2> hands{};
    const Quat toWorld = trackingTurn(in);
    // A snap or smooth turn, or a flip, since the last sample turned the
    // hands with the player: no punch in it.
    if (std::isfinite(in.trackingYaw)) {
        if (turnSeen)
            punches.turn(toWorld * lastTurn.conjugate());
        lastTurn = toWorld;
        turnSeen = true;
    }
    for (int i = 0; i < 2; ++i) {
        const auto& h = in.hands[i];
        hands[i] = {in.focused && h.tracked, h.aim.position, toWorld.rotate(h.gripRelativeToHead),
                    ((busy >> i) & 1) != 0};
    }
    inReach.clear();
    for (const auto& b : bots) {
        Vec3 feet{};
        if (!game_targets::position(b, feet))
            continue;
        bool close{};
        for (const auto& h : hands)
            close |= h.tracked && length(h.fist - (feet + Vec3{0, 1, 0})) < reach;
        if (close && game_targets::live(base, b))
            inReach.push_back({b.record, feet, botHeight, botRadius});
    }
    events.clear();
    punches.update(seconds, hands, inReach, events);
    for (const auto& e : events) {
        native_bodies::Damage d;
        d.victim = e.target;
        d.damager = hero;
        d.point = e.point;
        d.direction = e.direction;
        d.normal = e.direction * -1.f;
        d.amount = e.damage;
        d.type = 1; // kMelee
        d.knockback = static_cast<int32_t>(e.knockback);
        d.knockbackAmount = 2 + 6 * e.strength;
        native_bodies::damage(d);
    }
    publish([&](Data& d) {
        d.status = 2;
        ++d.samples;
        d.punches += events.size();
        d.bots = static_cast<uint32_t>(inReach.size());
        for (int i = 0; i < 2; ++i) {
            d.hands[i].speed = punches.speed(i);
            d.hands[i].busy = (busy >> i) & 1;
        }
        for (const auto& e : events) {
            auto& h = d.hands[e.hand];
            ++h.punches;
            h.lastTarget = e.target;
            h.lastStrength = e.strength;
            h.lastKnockback = static_cast<uint32_t>(e.knockback);
            d.lastPoint = e.point;
            d.lastDirection = e.direction;
            d.lastDamage = e.damage;
            d.lastSpeed = e.speed;
        }
    });
}
Data game_punch::data() {
    AcquireSRWLockShared(&output);
    auto out = SpidyPunchData;
    ReleaseSRWLockShared(&output);
    return out;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyPunchStart(void* input) {
    Config c{};
    if (!read(reinterpret_cast<uintptr_t>(input), &c, sizeof(c)) || c.magic != 0x53505543 || c.version != 1 ||
        c.bytes != sizeof(c) || c.pid != GetCurrentProcessId() ||
        c.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)))
        return 9501;
    return game_punch::start(c.base);
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyPunchStop(void*) {
    return game_punch::stop();
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyPunchSample(void* out) {
    const auto sample = game_punch::data();
    __try {
        std::memcpy(out, &sample, sizeof(sample));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 9503;
    }
    return 0;
}
// One blow straight to the game's DamageSystem, issued at the next physics
// step: 0 once the main thread has issued it.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyPunchTest(void* input) {
    Test t{};
    if (!read(reinterpret_cast<uintptr_t>(input), &t, sizeof(t)) || t.magic != 0x53505554 || t.version != 1 ||
        t.bytes != sizeof(t) || !t.victim || !finite(t.point) || !finite(t.direction))
        return 9502;
    if (!game_punch::running())
        return 9504;
    native_bodies::Damage d;
    d.victim = t.victim;
    d.damager = t.damager;
    d.point = t.point;
    d.direction = t.direction;
    d.normal = t.direction * -1.f;
    d.amount = t.amount;
    d.type = t.type;
    d.knockback = t.knockback;
    d.knockbackAmount = t.knockbackAmount;
    d.impulse = t.impulse;
    d.hash = t.hash;
    const auto before = native_bodies::counters();
    const auto ticket = native_bodies::damage(d);
    if (!ticket)
        return 9505;
    const auto limit = GetTickCount64() + 1000;
    while (native_bodies::damageIssued() < ticket && GetTickCount64() < limit)
        Sleep(5);
    publish([](Data&) {});
    if (native_bodies::damageIssued() < ticket)
        return 9506; // the game ran no physics step: paused, or loading
    // Issued, or dropped: the victim was gone, or the system's pool full.
    return native_bodies::counters().damages > before.damages ? 0 : 9507;
}
