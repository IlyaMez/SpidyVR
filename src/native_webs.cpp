// Draws tracked-hand webs with the game's own rope system. Rope slots are
// created, aimed, and released only inside the hero rope manager's update,
// the same phase in which the game's hero states use them.
#include "spidy/native_appearance.hpp"
#include "spidy/native_eye_frame.hpp"
#include "spidy/native_webs.hpp"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <windows.h>
using namespace spidy;
namespace {
using Update = void (*)(void*, float);
using Ensure = bool (*)(void*, uint32_t*, uint32_t, uint32_t, void*, uint32_t, uint32_t);
using Target = void (*)(void*, uint32_t, const float*);
using Release = void (*)(void*, uint32_t*, bool, bool);
using Get = uintptr_t (*)(void*, uint32_t);
constexpr uint32_t swingRope = 2; // rope type table: 2 = Swing
// Rope +6c0 is a lifetime the update counts down, then dissolves the rope
// (676dd0 -> 6795b0). EnsureRope sets it to -1. Refreshing it every update
// lets the game remove Spidy's webs by itself if Spidy stops running.
constexpr float leaseSeconds = .3f;
uintptr_t base{};
// The local player's actor record; retarget() follows it to a new actor.
std::atomic<uintptr_t> record{};
Update originalUpdate{};
void* updateHook{};
bool hooked{};
std::atomic<bool> enabled{}, stopping{};
std::atomic<uint32_t> ownedRopes{}, failedHands{};
std::atomic<uint64_t> lastHeroUpdate{};
SRWLOCK lifecycle = SRWLOCK_INIT, requestLock = SRWLOCK_INIT, statusLock = SRWLOCK_INIT, heroLock = SRWLOCK_INIT;
Vec3 heroPosition{};
uint64_t heroSamples{};
// First point of each owned rope as built in the update that took hero sample
// `startSample` (heroLock).
Vec3 builtStarts[2]{};
uint32_t builtHands{};
uint64_t startSample{};
native_webs::Request request{};
uint64_t requestDeadline{};
native_webs::Status current{};
std::array<std::atomic<uintptr_t>, 48> webParts{};
struct Owned {
    uint32_t handle{};
    int64_t attachedAt{};
    bool failed{};
    // Released and dissolving; its end follows the request's anchor.
    bool trailing{};
};
Owned owned[2]; // hero rope manager update only
// The rope manager the handles above belong to. A new player brings a new
// one, and an old handle there would name one of the game's own ropes.
uintptr_t ownedManager{};
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
uintptr_t pointer(uintptr_t p) {
    uintptr_t v{};
    read(p, &v, sizeof(v));
    return v;
}
bool entry(uintptr_t rva, const unsigned char* expected, size_t n) {
    unsigned char b[32]{};
    return n <= sizeof(b) && read(base + rva, b, n) && !std::memcmp(b, expected, n);
}
// Instance handles resolve through the 16-byte table at 7a436e8 (191ba30).
uintptr_t instance(uint32_t handle) {
    const auto table = pointer(base + 0x7a436e8);
    int32_t count{};
    uint8_t generation{};
    const uint64_t index = handle & 0xffffff;
    if (!(handle >> 24) || !table || !read(base + 0x7a43704, &count, 4) || static_cast<int64_t>(index) >= count ||
        !read(table + index * 16 + 8, &generation, 1) || generation != handle >> 24)
        return 0;
    return pointer(table + index * 16);
}
void publishParts(uintptr_t manager) {
    size_t next{};
    for (unsigned slot = 0; slot < 16; ++slot) {
        uint8_t active{};
        const auto rope = manager + 0x50 + slot * 0x7c8;
        if (!read(rope + 0x70c, &active, 1) || !(active & 1))
            continue;
        uint32_t handles[3]{};
        read(rope, handles, 8);          // tube, camera-facing effect
        read(rope + 0x71c, handles + 2, 4); // end cone
        for (auto h : handles)
            if (const auto p = h ? instance(h) : 0; p && next < webParts.size())
                webParts[next++].store(p, std::memory_order_relaxed);
    }
    while (next < webParts.size())
        webParts[next++].store(0, std::memory_order_relaxed);
}
void update(void* manager, float dt) {
    const auto mgr = reinterpret_cast<uintptr_t>(manager);
    const auto heroRecord = record.load();
    if (!enabled || !heroRecord || pointer(mgr) != base + 0x38b3df8 || pointer(mgr + 8) != heroRecord) {
        originalUpdate(manager, dt);
        return;
    }
    if (mgr != ownedManager) {
        owned[0] = owned[1] = {};
        ownedManager = mgr;
    }
    lastHeroUpdate = GetTickCount64();
    native_webs::Request wanted{};
    AcquireSRWLockShared(&requestLock);
    wanted = request;
    const bool live = !stopping && GetTickCount64() < requestDeadline;
    ReleaseSRWLockShared(&requestLock);
    // Tracked wrists were placed from the player position sampled for that
    // pose. Move them with the player to the frame being updated. The eyes are
    // moved from this same sample, even while no web is requested, so the
    // camera never steps between two hero positions when a web attaches.
    Vec3 travel{};
    float hero[16]{};
    const bool placed = read(pointer(heroRecord), hero, sizeof(hero));
    uint64_t sample{};
    if (placed) {
        AcquireSRWLockExclusive(&heroLock);
        heroPosition = {hero[12], hero[13], hero[14]};
        sample = ++heroSamples;
        ReleaseSRWLockExclusive(&heroLock);
    }
    if (live && placed)
        native_eyes::reanchorOffset(wanted.feet, {hero[12], hero[13], hero[14]}, travel);
    const auto ensure = reinterpret_cast<Ensure>(base + 0x677d20);
    const auto target = reinterpret_cast<Target>(base + 0x67d7c0);
    const auto release = reinterpret_cast<Release>(base + 0x67b610);
    const auto get = reinterpret_cast<Get>(base + 0x67b7b0);
    uint64_t created{}, released{}, failures{};
    uint32_t failed{}, count{}, drawn{}, builtMask{};
    Vec3 built[2]{};
    float startError = -1;
    for (unsigned hand = 0; hand < 2; ++hand) {
        auto& rope = owned[hand];
        const auto& want = wanted.hands[hand];
        const bool attached = live && want.attached == 1 && want.tracked && finite(want.anchor);
        // This hand's web let go of a target it still trails.
        const bool trail = live && want.attached == 2 && want.attachedAt == rope.attachedAt && finite(want.anchor);
        // Engine events can clear every slot; a stale handle is simply gone,
        // and so is a released rope once it has dissolved.
        if (rope.handle && !get(manager, rope.handle))
            rope.handle = 0;
        if (rope.handle && rope.trailing) {
            if (trail)
                target(manager, rope.handle, &want.anchor.x);
            else
                rope.handle = 0; // it dissolves where it is
        } else if (rope.handle && (!attached || want.attachedAt != rope.attachedAt)) {
            // Dissolve; a trailing web keeps its handle so its end can follow.
            release(manager, &rope.handle, false, trail);
            rope.trailing = trail && rope.handle;
            if (rope.trailing)
                target(manager, rope.handle, &want.anchor.x);
            ++released;
        }
        if (!rope.handle)
            rope.trailing = false;
        if (attached && !rope.handle && !(rope.failed && rope.attachedAt == want.attachedAt)) {
            uint32_t handle{};
            if (ensure(manager, &handle, swingRope, hand, nullptr, 0, 0) && get(manager, handle)) {
                rope = {handle, want.attachedAt, false, false};
                ++created;
            } else {
                rope = {0, want.attachedAt, true, false};
                ++failures;
            }
        }
        if (!attached)
            rope.failed = false;
        if (rope.handle && !rope.trailing) {
            target(manager, rope.handle, &want.anchor.x);
            if (const auto slot = get(manager, rope.handle))
                std::memcpy(reinterpret_cast<void*>(slot + 0x6c0), &leaseSeconds, sizeof(leaseSeconds));
            ++count;
            drawn |= 1u << hand;
        }
        failed |= rope.failed ? 1u << hand : 0;
    }
    if (live) {
        // 676dd0 rewrites Start/Grip/Grip2/End (7d60/7d78/7d90/7da8, +12 per
        // hand) from skeleton joints whose hashes (7d40..7d5f) are non-zero.
        // Supply tracked positions for this update only and restore the hashes.
        uint32_t hashes[8]{};
        std::memcpy(hashes, reinterpret_cast<void*>(mgr + 0x7d40), sizeof(hashes));
        std::memset(reinterpret_cast<void*>(mgr + 0x7d40), 0, sizeof(hashes));
        Vec3 starts[2]{};
        bool wrote[2]{};
        for (unsigned hand = 0; hand < 2; ++hand) {
            const auto& want = wanted.hands[hand];
            if (!want.tracked)
                continue; // keep the previous positions for a web that is dissolving
            const Vec3 start = want.wrist + travel;
            if (!finite(start))
                continue;
            // A non-zero Grip makes 679dc0 pass two hand points to the hero
            // override 95f9c0, which rebuilds rope points 0-7 as a 0.45 m tail
            // hanging from the hand and trailing opposite the hand's world
            // velocity (7df8). At swing speed that tail streamed behind the
            // wrist as a second strand. Grip (0,0,0) keeps one hand point, so
            // the rope runs straight from Start to the anchor.
            const Vec3 none{};
            for (const auto [offset, point] : {std::pair{0x7d60, start}, std::pair{0x7d78, none},
                                               std::pair{0x7d90, start}, std::pair{0x7da8, start}})
                std::memcpy(reinterpret_cast<void*>(mgr + offset + hand * 12), &point, sizeof(point));
            starts[hand] = start;
            wrote[hand] = true;
        }
        originalUpdate(manager, dt);
        std::memcpy(reinterpret_cast<void*>(mgr + 0x7d40), hashes, sizeof(hashes));
        // 679dc0 starts each attached rope's points (rope +1c) at Start. A
        // released one (+70c bit 2) drifts from it, and the tail above moved
        // point 0 away, so a gap here means the rope does not leave the wrist.
        for (unsigned hand = 0; hand < 2; ++hand)
            if (const auto slot = wrote[hand] && owned[hand].handle && !owned[hand].trailing
                                      ? get(manager, owned[hand].handle)
                                      : 0;
                slot && read(slot + 0x1c, &built[hand], sizeof(Vec3)) && finite(built[hand])) {
                startError = std::max(startError, length(built[hand] - starts[hand]));
                builtMask |= 1u << hand;
            }
    } else {
        originalUpdate(manager, dt);
    }
    if (sample) {
        AcquireSRWLockExclusive(&heroLock);
        builtStarts[0] = built[0];
        builtStarts[1] = built[1];
        builtHands = builtMask;
        startSample = sample;
        ReleaseSRWLockExclusive(&heroLock);
    }
    publishParts(mgr);
    ownedRopes = count;
    failedHands = failed;
    AcquireSRWLockExclusive(&statusLock);
    current.state = native_webs::active;
    current.live = drawn;
    current.creates += created;
    current.releases += released;
    current.failures += failures;
    ++current.updates;
    if (startError >= 0) {
        current.startError = startError;
        current.startErrorMax = std::max(current.startErrorMax, startError);
    }
    ReleaseSRWLockExclusive(&statusLock);
}
} // namespace

uint32_t native_webs::start(uintptr_t gameBase, uintptr_t heroRecord) {
    AcquireSRWLockExclusive(&lifecycle);
    uint32_t result{};
    do {
        if (enabled)
            break;
        base = gameBase;
        record = heroRecord;
        const unsigned char updateBytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48,
                                             0x89, 0x70, 0x20, 0x55, 0x57, 0x41, 0x54, 0x41};
        const unsigned char ensureBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83,
                                             0xec, 0x20, 0x48, 0x8b, 0xf9, 0x45, 0x8b, 0xd8};
        const unsigned char targetBytes[] = {0x0f, 0xb7, 0xc2, 0x83, 0xf8, 0x10, 0x73, 0x56,
                                             0x4c, 0x69, 0xc8, 0xc8, 0x07, 0x00, 0x00, 0x41};
        const unsigned char releaseBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
                                              0xec, 0x20, 0x44, 0x8b, 0x1a, 0x41, 0x0f, 0xb6};
        const unsigned char getBytes[] = {0x0f, 0xb7, 0xc2, 0x83, 0xf8, 0x10, 0x73, 0x30,
                                          0x4c, 0x69, 0xc0, 0xc8, 0x07, 0x00, 0x00, 0x41};
        if (!base || !record.load() || !entry(0x676dd0, updateBytes, sizeof(updateBytes)) ||
            !entry(0x677d20, ensureBytes, sizeof(ensureBytes)) ||
            !entry(0x67d7c0, targetBytes, sizeof(targetBytes)) ||
            !entry(0x67b610, releaseBytes, sizeof(releaseBytes)) || !entry(0x67b7b0, getBytes, sizeof(getBytes))) {
            result = 6001;
            break;
        }
        if (!hooked) {
            const auto s = MH_Initialize();
            if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
                result = 6100 + s;
                break;
            }
            updateHook = reinterpret_cast<void*>(base + 0x676dd0);
            if (const auto c = MH_CreateHook(updateHook, reinterpret_cast<void*>(update),
                                             reinterpret_cast<void**>(&originalUpdate));
                c != MH_OK) {
                result = 6200 + c;
                break;
            }
            hooked = true;
        }
        stopping = false;
        AcquireSRWLockExclusive(&statusLock);
        current = {};
        current.state = waiting;
        ReleaseSRWLockExclusive(&statusLock);
        enabled = true;
        if (const auto s = MH_EnableHook(updateHook); s != MH_OK) {
            enabled = false;
            result = 6300 + s;
        }
    } while (false);
    if (result) {
        AcquireSRWLockExclusive(&statusLock);
        current.state = failed;
        ReleaseSRWLockExclusive(&statusLock);
    }
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
void native_webs::retarget(uintptr_t heroRecord) {
    AcquireSRWLockExclusive(&lifecycle);
    record = heroRecord;
    ReleaseSRWLockExclusive(&lifecycle);
}
void native_webs::submit(const Request& next, uint32_t leaseMs) {
    AcquireSRWLockExclusive(&requestLock);
    request = next;
    requestDeadline = GetTickCount64() + leaseMs;
    ReleaseSRWLockExclusive(&requestLock);
}
native_webs::Status native_webs::status() {
    AcquireSRWLockShared(&statusLock);
    auto out = current;
    ReleaseSRWLockShared(&statusLock);
    return out;
}
bool native_webs::drawing(unsigned hand) {
    // The rope manager must still be updating; a respawned hero gets a new one.
    return enabled && !stopping && hand < 2 && GetTickCount64() - lastHeroUpdate.load() < 500 &&
           status().state == active && !(failedHands.load() & (1u << hand));
}
bool native_webs::heroSample(uint64_t& samples, Vec3& position) {
    AcquireSRWLockShared(&heroLock);
    samples = heroSamples;
    position = heroPosition;
    ReleaseSRWLockShared(&heroLock);
    return samples != 0;
}
uint32_t native_webs::ropeStarts(uint64_t sample, Vec3 starts[2]) {
    AcquireSRWLockShared(&heroLock);
    const uint32_t hands = sample && sample == startSample ? builtHands : 0;
    starts[0] = builtStarts[0];
    starts[1] = builtStarts[1];
    ReleaseSRWLockShared(&heroLock);
    return hands;
}
bool native_webs::webInstance(uintptr_t candidate) {
    if (!candidate)
        return false;
    for (const auto& part : webParts)
        if (part.load(std::memory_order_relaxed) == candidate)
            return true;
    return false;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyWebsStart(void* input) {
    native_webs::ProbeConfig c{};
    if (!read(reinterpret_cast<uintptr_t>(input), &c, sizeof(c)) || c.magic != 0x53574243 || c.version != 1 ||
        c.bytes != sizeof(c) || c.pid != GetCurrentProcessId() ||
        c.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) || !c.record)
        return 6501;
    // The eye views hide this hero and anchor to it, as in a VR session.
    native_appearance::setPlayerRecord(c.record);
    return native_webs::start(c.base, c.record);
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyWebsSubmit(void* input) {
    native_webs::ProbeCommand c{};
    if (!read(reinterpret_cast<uintptr_t>(input), &c, sizeof(c)) || c.magic != 0x5357424d || c.version != 1 ||
        c.bytes != sizeof(c) || c.leaseMs > 500 || !finite(c.request.feet))
        return 6502;
    for (const auto& hand : c.request.hands)
        if (hand.attached > 2 || hand.tracked > 1 || !finite(hand.anchor) || !finite(hand.wrist))
            return 6502;
    native_webs::submit(c.request, c.leaseMs);
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyWebsStop(void*) {
    return native_webs::stop();
}
uint32_t native_webs::stop() {
    AcquireSRWLockExclusive(&lifecycle);
    stopping = true;
    // The hook dissolves owned ropes on its next call. If the game is not
    // updating the hero, their lifetimes dissolve them once it resumes.
    const auto limit = GetTickCount64() + 500;
    while (enabled && ownedRopes.load() && GetTickCount64() < limit)
        Sleep(10);
    uint32_t result{};
    enabled = false;
    if (hooked)
        if (const auto s = MH_DisableHook(updateHook); s != MH_OK && s != MH_ERROR_DISABLED)
            result = 6400 + s;
    for (auto& part : webParts)
        part.store(0, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&statusLock);
    current.state = result ? failed : off;
    current.live = 0;
    ReleaseSRWLockExclusive(&statusLock);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
