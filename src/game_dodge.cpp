// The game's own dodge for a player a web carries (game_dodge.hpp). Addresses
// are image offsets in the supported Spider-Man.exe.
#include "spidy/game_dodge.hpp"
#include "spidy/game_sense.hpp"
#include "spidy/spider_sense.hpp"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_dodge;
extern "C" {
__declspec(dllexport) Data SpidyDodgeData;
__declspec(dllexport) Live SpidyDodgeLive;
}

namespace {
// The manager's update (manager, its game seconds), its dodge start
// (manager, kind, an attacker's handle, the way, whether a perfect one slows
// time) and its end (manager, whether the hero's HeroWAMUser hears so at
// once); the first entry of its table, its init.
constexpr uintptr_t updateRva = 0x92d640, startRva = 0x92a040, endRva = 0x929ef0, initRva = 0x925af0;
// Its dodge now: the kind (0 none), its answer, when it started.
constexpr uintptr_t kindAt = 0xcd8, answerAt = 0xcdc, startedAt = 0xcc0;
// An attack's state bits besides game_sense's warned: dodged (0x400000 once
// that dodge ended), or answered otherwise.
constexpr uint32_t dodgedState = 0x1, answeredState = 0x100, dodgeEndedState = 0x400000;
using UpdateFn = void(__fastcall*)(void*, float);
using StartFn = void(__fastcall*)(void*, uint32_t, uint32_t, const float*, uint8_t);
using EndFn = void(__fastcall*)(void*, uint8_t);
UpdateFn originalUpdate{};
uintptr_t base{};
void* hook{};
bool hooked{};
std::atomic<bool> enabled{}, ending{}, dodgingNow{};
std::atomic<int> active{};
SRWLOCK lifecycle = SRWLOCK_INIT;
// The latest carry sample, a probe's test and SpidyDodgeData, under this.
SRWLOCK lock = SRWLOCK_INIT;
struct Carry {
    uint64_t hero{};
    bool webbed{}, airborne{};
    Vec3 velocity{};
    ULONGLONG at{};
};
Carry carried;
// A probe's dodge (SpidyDodgeTest): asked (1), started and to be ended at the
// next frame (2), done (3).
struct Test {
    uint32_t magic = 0x53444754, version = 1, bytes = sizeof(Test), kind{};
    uint64_t hero{};
    // Out: the dodge's kind before, after the start and after the end; the
    // game's answer; 0, or what kept it from running.
    uint32_t before{}, started{}, after{}, answer{}, error{}, pad{};
};
static_assert(sizeof(Test) == 48);
Test test;
std::atomic<int> testStage{};
// The main thread's own, in the hook: the dodge on a web, whether the one
// the manager has on is the one it started (and when that started), the
// frame clock.
WebDodge web{WebDodgeConfig{}};
bool ours{};
double oursStarted{};
LARGE_INTEGER lastFrame{}, frequency{};
uint32_t cutShortSeen{};
// SpidyDodgeLive's last kind, for its starts.
uint32_t liveKind{};
// The hero's dodge now, for DODGE TUMBLE (SpidyDodgeLive). The main thread's
// alone writes it; lastKind goes out before the start it counts.
void publishLive(uint32_t kind, bool spidys) {
    volatile Live& live = SpidyDodgeLive;
    if (kind && !liveKind) {
        live.lastKind = kind;
        MemoryBarrier();
        live.starts = live.starts + 1;
    }
    liveKind = kind;
    live.kind = kind;
    live.ours = spidys ? 1u : 0u;
    live.frames = live.frames + 1;
}

template <class F> void publish(F change) {
    InterlockedIncrement64(&SpidyDodgeData.sequence);
    change(SpidyDodgeData);
    InterlockedIncrement64(&SpidyDodgeData.sequence);
}
bool read(uintptr_t p, void* out, size_t n) {
    __try {
        if (p < 0x10000)
            return false;
        std::memcpy(out, reinterpret_cast<const void*>(p), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class T> T value(uintptr_t p) {
    T v{};
    read(p, &v, sizeof(v));
    return v;
}
bool entry(uintptr_t rva, std::initializer_list<uint8_t> bytes) {
    uint8_t have[16]{};
    return bytes.size() <= sizeof(have) && read(base + rva, have, bytes.size()) &&
           std::memcmp(have, bytes.begin(), bytes.size()) == 0;
}
bool callStart(uintptr_t manager, uint32_t kind, Vec3 way) {
    __try {
        reinterpret_cast<StartFn>(base + startRva)(reinterpret_cast<void*>(manager), kind, 0, &way.x, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool callEnd(uintptr_t manager) {
    __try {
        reinterpret_cast<EndFn>(base + endRva)(reinterpret_cast<void*>(manager), 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// A probe's dodge, in the frame of the hero it names.
void runTest(uintptr_t manager) {
    const int stage = testStage.load();
    if (stage == 1) {
        test.before = value<uint32_t>(manager + kindAt);
        if (test.before) {
            test.error = 9205; // a dodge on already
            testStage = 3;
            return;
        }
        if (!callStart(manager, test.kind, {0, 0, 1})) {
            test.error = 9206;
            testStage = 3;
            return;
        }
        test.started = value<uint32_t>(manager + kindAt);
        test.answer = value<uint32_t>(manager + answerAt);
        testStage = 2;
    } else if (stage == 2) {
        if (value<uint32_t>(manager + kindAt) == test.started && !callEnd(manager))
            test.error = 9207;
        test.after = value<uint32_t>(manager + kindAt);
        testStage = 3;
    }
}
// One frame of the hero's manager (the main thread, after its own update).
void frame(uintptr_t manager) {
    AcquireSRWLockShared(&lock);
    const Carry c = carried;
    const uint64_t tested = testStage.load() == 1 || testStage.load() == 2 ? test.hero : 0;
    ReleaseSRWLockShared(&lock);
    const uint64_t record = value<uint64_t>(manager + 8);
    if (value<uintptr_t>(manager) != base + game_sense::managerTable || !record)
        return;
    if (tested && record == tested) {
        runTest(manager);
        return;
    }
    // The player's manager alone.
    if (!c.hero || record != c.hero)
        return;
    {
        const uint32_t kind = value<uint32_t>(manager + kindAt);
        publishLive(kind, ours && kind == webKind && value<double>(manager + startedAt) == oursStarted);
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const float seconds =
        lastFrame.QuadPart && frequency.QuadPart
            ? static_cast<float>(static_cast<double>(now.QuadPart - lastFrame.QuadPart) / frequency.QuadPart)
            : 0.f;
    lastFrame = now;
    // The warnings it holds, as the swing's Spider-Sense reads them.
    game_sense::Attack attacks[game_sense::attackSlots];
    uint32_t count{};
    double clock{};
    uint64_t found = manager;
    if (!game_sense::read(base, record, found, attacks, count, clock) || found != manager)
        return;
    std::array<WebDodgeThreat, game_sense::attackSlots> threats{};
    size_t warned{};
    for (uint32_t i = 0; i < count; ++i) {
        const auto& a = attacks[i];
        if (!(a.state & game_sense::warnedState) || !a.handle || !std::isfinite(a.warnAt))
            continue;
        auto& t = threats[warned++];
        t.attacker = a.handle;
        t.timed = std::isfinite(a.impactAt) && a.impactAt >= 0;
        t.toImpact = t.timed ? static_cast<float>(a.impactAt - clock) : 0.f;
        t.warned = static_cast<float>(clock - a.warnAt);
        t.answered = (a.state & (dodgedState | answeredState | dodgeEndedState)) != 0;
    }
    const uint32_t kind = value<uint32_t>(manager + kindAt);
    const GameDodge game = !kind ? GameDodge::none
                           : ours && kind == webKind && value<double>(manager + startedAt) == oursStarted
                               ? GameDodge::ours
                               : GameDodge::other;
    const bool fresh = GetTickCount64() - c.at < leaseMs && !ending.load();
    const bool webbed = fresh && c.webbed;
    auto ask = web.update(seconds, webbed, fresh && c.airborne, fresh ? c.velocity : Vec3{},
                          {threats.data(), warned}, game);
    if (game != GameDodge::ours)
        ours = false;
    if (ending.load() && game == GameDodge::ours)
        ask = WebDodge::Ask::end;
    uint32_t answer{}, error{};
    bool taken{}, endedNow{};
    if (ask == WebDodge::Ask::start) {
        if (callStart(manager, webKind, web.way()) && value<uint32_t>(manager + kindAt) == webKind) {
            ours = taken = true;
            oursStarted = value<double>(manager + startedAt);
            answer = value<uint32_t>(manager + answerAt) & 3;
        } else {
            web.refused();
            error = 9204;
        }
    } else if (ask == WebDodge::Ask::end && game == GameDodge::ours) {
        endedNow = callEnd(manager);
        ours = false;
        if (ending.load())
            web.reset();
    }
    dodgingNow = ours;
    const uint32_t cut = web.cutShort();
    const uint32_t newlyCut = cut - cutShortSeen;
    cutShortSeen = cut;
    AcquireSRWLockExclusive(&lock);
    publish([&](Data& d) {
        ++d.frames;
        d.carried = webbed;
        d.cutShort += newlyCut;
        d.ended += endedNow;
        if (ask == WebDodge::Ask::start) {
            ++d.asked;
            if (taken) {
                ++d.taken;
                ++d.answers[answer];
                d.lastAnswer = answer;
                d.lastWarnings = static_cast<uint32_t>(warned);
                d.lastWay = web.way();
                d.lastSpeed = finite(c.velocity) ? length(c.velocity) : 0.f;
            } else {
                d.error = error;
            }
        }
    });
    ReleaseSRWLockExclusive(&lock);
}
void __fastcall updateHook(void* manager, float seconds) {
    ++active;
    originalUpdate(manager, seconds);
    if (enabled.load(std::memory_order_relaxed))
        frame(reinterpret_cast<uintptr_t>(manager));
    --active;
}
} // namespace

uint32_t game_dodge::start(uintptr_t imageBase) {
    AcquireSRWLockExclusive(&lifecycle);
    uint32_t result{};
    do {
        if (enabled)
            break;
        base = imageBase;
        // The bytes the hook's jump covers (mov [rsp+20h], rbx; push rdi; sub
        // rsp, 40h) and what follows, the start's and the end's first, and the
        // manager's table.
        if (!entry(updateRva,
                   {0x48, 0x89, 0x5c, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x40, 0x80, 0xb9, 0xe0, 0x0c, 0x00, 0x00}) ||
            !entry(startRva,
                   {0x85, 0xd2, 0x0f, 0x84, 0x5a, 0x05, 0x00, 0x00, 0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48}) ||
            !entry(endRva,
                   {0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x83, 0xb9, 0xd8, 0x0c, 0x00, 0x00}) ||
            value<uintptr_t>(base + game_sense::managerTable) != base + initRva) {
            result = 9201;
            break;
        }
        if (!hooked) {
            const auto s = MH_Initialize();
            if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
                result = 9210 + s;
                break;
            }
            hook = reinterpret_cast<void*>(base + updateRva);
            if (const auto c = MH_CreateHook(hook, reinterpret_cast<void*>(updateHook),
                                             reinterpret_cast<void**>(&originalUpdate));
                c != MH_OK) {
                result = 9220 + c;
                break;
            }
            hooked = true;
        }
        // The hook is off and no frame is in it: the main thread's state is
        // this thread's to reset.
        web.reset();
        ours = false;
        dodgingNow = false;
        lastFrame = {};
        cutShortSeen = web.cutShort();
        QueryPerformanceFrequency(&frequency);
        AcquireSRWLockExclusive(&lock);
        carried = {};
        ReleaseSRWLockExclusive(&lock);
        enabled = true;
        if (const auto s = MH_EnableHook(hook); s != MH_OK && s != MH_ERROR_ENABLED) {
            enabled = false;
            result = 9240 + s;
        }
    } while (false);
    AcquireSRWLockExclusive(&lock);
    publish([&](Data& d) {
        d.status = result ? 3 : 1;
        d.error = result;
    });
    ReleaseSRWLockExclusive(&lock);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
void game_dodge::stop() {
    AcquireSRWLockExclusive(&lifecycle);
    if (enabled) {
        // A dodge of Spidy's ends at the next frame of the hero's: one left
        // on would keep the game's dodge (and its enemies' notice of it) on.
        ending = true;
        const auto until = GetTickCount64() + 250;
        while (dodgingNow.load() && GetTickCount64() < until)
            Sleep(1);
    }
    enabled = false;
    ending = false;
    if (hooked)
        MH_DisableHook(hook);
    // A frame in flight finishes first.
    const auto until = GetTickCount64() + 2000;
    while (active.load() && GetTickCount64() < until)
        Sleep(1);
    AcquireSRWLockExclusive(&lock);
    carried = {};
    publish([](Data& d) {
        d.status = 0;
        d.carried = 0;
    });
    ReleaseSRWLockExclusive(&lock);
    ReleaseSRWLockExclusive(&lifecycle);
}
void game_dodge::carry(uint64_t hero, bool webbed, bool airborne, Vec3 velocity) {
    if (!enabled.load(std::memory_order_relaxed))
        return;
    AcquireSRWLockExclusive(&lock);
    carried = {hero, webbed, airborne, finite(velocity) ? velocity : Vec3{}, GetTickCount64()};
    ReleaseSRWLockExclusive(&lock);
}
Data game_dodge::data() {
    AcquireSRWLockShared(&lock);
    const Data out = SpidyDodgeData;
    ReleaseSRWLockShared(&lock);
    return out;
}

// For a probe without the swing (which starts it in a session): the hook in,
// as game_dodge::start returns.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyDodgeStart(void*) {
    return game_dodge::start(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyDodgeStop(void*) {
    game_dodge::stop();
    return 0;
}
// A probe's dodge: the game's own dodge of a kind started for the hero whose
// record it names, at the next frame of his manager, and ended at the one
// after (the hook must be in). Writes the request back with what came of it.
// 0 done (its `error` says whether the game took it), 9202 a bad request,
// 9203 no frame of his manager within three seconds (paused, loading, not
// the hero), 9201 another build.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyDodgeTest(void* inout) {
    Test t{};
    const Test expected{};
    if (!read(reinterpret_cast<uintptr_t>(inout), &t, sizeof(t)) || t.magic != expected.magic ||
        t.version != expected.version || t.bytes != expected.bytes || !t.hero || !t.kind || t.kind > 14)
        return 9202;
    if (!enabled.load())
        if (const auto code = game_dodge::start(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))))
            return code;
    AcquireSRWLockExclusive(&lock);
    test = t;
    test.before = test.started = test.after = test.answer = test.error = 0;
    testStage = 1;
    ReleaseSRWLockExclusive(&lock);
    const auto limit = GetTickCount64() + 3000;
    while (testStage.load() != 3 && GetTickCount64() < limit)
        Sleep(5);
    AcquireSRWLockExclusive(&lock);
    const bool done = testStage.load() == 3;
    if (!done)
        testStage = 0;
    const Test out = test;
    ReleaseSRWLockExclusive(&lock);
    __try {
        std::memcpy(inout, &out, sizeof(out));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 9202;
    }
    return done ? 0 : 9203;
}
