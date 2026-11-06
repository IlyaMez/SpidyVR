// Draws tracked-hand webs with the game's own rope system. Rope slots are
// created, aimed, and released only inside the hero rope manager's update,
// the same phase in which the game's hero states use them.
#include "spidy/native_appearance.hpp"
#include "spidy/native_eye_frame.hpp"
#include "spidy/native_webs.hpp"
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <windows.h>
using namespace spidy;
extern "C" {
__declspec(dllexport) spidy::native_webs::LineRopeData SpidyLineRopeData;
}
namespace {
using Update = void (*)(void*, float);
using Ensure = bool (*)(void*, uint32_t*, uint32_t, uint32_t, void*, uint32_t, uint32_t);
using Target = void (*)(void*, uint32_t, const float*);
using Release = void (*)(void*, uint32_t*, bool, bool);
using Get = uintptr_t (*)(void*, uint32_t);
// The hero rope manager's own hand points (95f9c0, its vtable's +0x78), which
// 679dc0 calls for every live rope after laying Start (and Grip, when it is
// not zero) as its first `*count` points and before the line from the last of
// them to the anchor: the place a rope can be given more points of its own.
using HandPoints = void (*)(void*, uint8_t*, float, int32_t*);
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
HandPoints originalHandPoints{};
void* handPointsHook{};
bool heldHooked{};
// What the update running on this thread lays at the start of each of
// Spidy's ropes (ropePoints): for a web in both hands the other wrist, and
// the edges a web bends round. Set and cleared around the game's update,
// which calls the hand points.
struct Laying {
    uintptr_t rope{};
    int count{};      // points laid
    int drawnAt = -1; // the point among them the rope leaves its wrist from
    Vec3 points[8]{};
};
thread_local std::array<Laying, 2> laying{};
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
native_webs::Bent requestBends[2]{};
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
// The web tie's ropes (submitTies): each made on a hand whose own web is not
// drawn, `hand`; hero rope manager update only.
struct TieOwned {
    uint32_t handle{}, hand{};
};
TieOwned tieOwned[native_webs::maxTieRopes];
native_webs::TieRope tieRequest[native_webs::maxTieRopes]{};
uint32_t tieRequestCount{};
uint64_t tieDeadline{};
std::atomic<uint32_t> tieMask{};
// Web lines' ropes (submitLines), each kept for its line (its id); hero
// rope manager update only.
struct LineOwned {
    uint32_t handle{}, hand{}, id{};
    uint32_t age{};            // updates since it was made
    uint32_t sinceGood = 1000; // updates since its start last lay on the line
    uint32_t badRun{};         // updates in a row off the line
    bool hidden{};             // let go: it was drawn from the hand; made again once a hand is free
};
LineOwned lineOwned[native_webs::maxLineRopes];
native_webs::LineRope lineRequest[native_webs::maxLineRopes]{};
uint32_t lineRequestCount{};
uint64_t lineDeadline{};
std::atomic<uint32_t> lineMask{};
// The hand points of each of Spidy's ropes in this update (Start, Grip,
// Grip2, End: 7d60, 7d78, 7d90, 7da8, +12 per hand), in the order the update
// lays them (by slot), for the hand points the game asks for each rope
// (handPoints): 679dc0 reads a rope's Start before it asks them (setting it
// there, November 3, drew a line from the hand), so after each rope its
// hand's points are set to those of the next rope on that hand, and before
// the update to those of the first.
struct RopeStart {
    uintptr_t rope{};
    uint32_t hand{};
    Vec3 points[4]{};
    bool line{};
    // A line's rope laid through these first (LineRope::via), then its start.
    uint32_t viaCount{};
    Vec3 via[2]{};
};
thread_local std::array<RopeStart, 16> ropeStarts{};
thread_local Vec3 handAfter[2][4]{}; // a hand's points once its last rope is laid
thread_local uint64_t startMoves{};
SRWLOCK lineLock = SRWLOCK_INIT;
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
void handPoints(void* manager, uint8_t* rope, float dt, int32_t* count) {
    // The rope's hand points were set for it before it was laid (ropeStarts);
    // a line's rope is laid its start and nothing more. Then its hand's points
    // go to the next of Spidy's ropes on that hand.
    const auto mgr = reinterpret_cast<uintptr_t>(manager);
    for (size_t i = 0; i < ropeStarts.size(); ++i) {
        const auto& r = ropeStarts[i];
        if (!r.rope || r.rope != reinterpret_cast<uintptr_t>(rope))
            continue;
        const Vec3* next = handAfter[r.hand];
        for (size_t j = i + 1; j < ropeStarts.size(); ++j)
            if (ropeStarts[j].rope && ropeStarts[j].hand == r.hand) {
                next = ropeStarts[j].points;
                break;
            }
        for (int p = 0; p < 4; ++p)
            std::memcpy(reinterpret_cast<void*>(mgr + 0x7d60 + p * 0x18 + r.hand * 12), &next[p],
                        sizeof(Vec3));
        ++startMoves;
        if (r.line && count) {
            const uint32_t via = std::min<uint32_t>(r.viaCount, 2);
            if (via)
                std::memcpy(rope + 0x1c, r.via, sizeof(Vec3) * via);
            std::memcpy(rope + 0x1c + via * 12, &r.points[0], sizeof(Vec3));
            *count = static_cast<int32_t>(via + 1);
            return;
        }
        break;
    }
    const Laying* h{};
    for (const auto& candidate : laying)
        if (candidate.rope && reinterpret_cast<uintptr_t>(rope) == candidate.rope)
            h = &candidate;
    uint32_t flags{}, type{};
    // Only a rope Spidy lays, attached (not released: +70c bit 2), a swing
    // rope (type 2).
    if (!h || !count || h->count < 2 || h->count > 8 || !read(h->rope + 0x70c, &flags, 4) || (flags & 4) ||
        !read(h->rope + 0x700, &type, 4) || type != 2) {
        originalHandPoints(manager, rope, dt, count);
        return;
    }
    // The game goes on from the rope's Start, which the update made the last
    // of them.
    std::memcpy(rope + 0x1c, h->points, sizeof(Vec3) * h->count);
    *count = h->count;
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
        for (auto& t : tieOwned)
            t = {};
        for (auto& l : lineOwned)
            l = {};
        ownedManager = mgr;
    }
    lastHeroUpdate = GetTickCount64();
    native_webs::Request wanted{};
    native_webs::Bent wantedBends[2]{};
    AcquireSRWLockShared(&requestLock);
    wanted = request;
    wantedBends[0] = requestBends[0];
    wantedBends[1] = requestBends[1];
    const bool live = !stopping && GetTickCount64() < requestDeadline;
    native_webs::TieRope tiesWanted[native_webs::maxTieRopes]{};
    const uint32_t tiesCount =
        live && GetTickCount64() < tieDeadline ? std::min(tieRequestCount, native_webs::maxTieRopes) : 0u;
    std::copy(tieRequest, tieRequest + native_webs::maxTieRopes, tiesWanted);
    native_webs::LineRope linesWanted[native_webs::maxLineRopes]{};
    const uint32_t linesCount =
        live && GetTickCount64() < lineDeadline ? std::min(lineRequestCount, native_webs::maxLineRopes) : 0u;
    std::copy(lineRequest, lineRequest + native_webs::maxLineRopes, linesWanted);
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
    // The tie's ropes: each on a hand whose own web is not drawn (a web let
    // go of drifts from its hand: it is not). Made after the hands' own, so a
    // web a hand shoots now takes its hand back in this same update.
    uint32_t tieDrawn{};
    bool tieHand[2]{};
    Vec3 tieAnchor[2]{};
    {
        const auto handBusy = [&](uint32_t h) { return owned[h].handle && !owned[h].trailing; };
        for (uint32_t k = 0; k < native_webs::maxTieRopes; ++k) {
            auto& rope = tieOwned[k];
            const auto& want = tiesWanted[k];
            const bool tieWanted = k < tiesCount && finite(want.anchor) && finite(want.end);
            if (rope.handle && !get(manager, rope.handle))
                rope.handle = 0;
            if (rope.handle && (!tieWanted || handBusy(rope.hand) || tieHand[rope.hand])) {
                release(manager, &rope.handle, false, false);
                rope.handle = 0;
                ++released;
            }
            if (tieWanted && !rope.handle)
                for (uint32_t h = 0; h < 2 && !rope.handle; ++h) {
                    if (handBusy(h) || tieHand[h])
                        continue;
                    uint32_t handle{};
                    if (ensure(manager, &handle, swingRope, h, nullptr, 0, 0) && get(manager, handle)) {
                        rope = {handle, h};
                        ++created;
                    } else {
                        ++failures;
                    }
                }
            if (!rope.handle)
                continue;
            target(manager, rope.handle, &want.end.x);
            if (const auto slot = get(manager, rope.handle))
                std::memcpy(reinterpret_cast<void*>(slot + 0x6c0), &leaseSeconds, sizeof(leaseSeconds));
            tieHand[rope.hand] = true;
            tieAnchor[rope.hand] = want.anchor;
            tieDrawn |= 1u << k;
            ++count;
        }
    }
    tieMask = tieDrawn;
    // The lines' ropes, each kept for its line (by id): made on a hand (a
    // free one when there is one). One the game drew from the hand is let
    // go (hidden) and made again once a hand is free; the overlay draws it
    // meanwhile.
    uint64_t linesMade{}, linesReleased{}, linesHidden{};
    const auto handBusy = [&](uint32_t h) { return (owned[h].handle && !owned[h].trailing) || tieHand[h]; };
    {
        for (auto& rope : lineOwned) {
            bool still = false;
            for (uint32_t k = 0; k < linesCount; ++k)
                still |= rope.id && linesWanted[k].id == rope.id;
            if (rope.handle && !get(manager, rope.handle))
                rope.handle = 0;
            if (!still) {
                if (rope.handle) {
                    release(manager, &rope.handle, false, false);
                    ++released;
                    ++linesReleased;
                }
                rope = {};
            }
        }
        for (uint32_t k = 0; k < linesCount; ++k) {
            const auto& want = linesWanted[k];
            if (!want.id || !finite(want.start) || !finite(want.end))
                continue;
            LineOwned* rope{};
            for (auto& r : lineOwned)
                if (r.id == want.id)
                    rope = &r;
            if (!rope)
                for (auto& r : lineOwned)
                    if (!rope && !r.id) {
                        rope = &r;
                        *rope = {};
                        rope->id = want.id;
                    }
            if (!rope)
                continue;
            if (!rope->handle) {
                // Hidden: only on a free hand. New: a free hand, else hand 0.
                int hand = -1;
                for (uint32_t h = 0; h < 2 && hand < 0; ++h)
                    if (!handBusy(h))
                        hand = static_cast<int>(h);
                if (hand < 0 && !rope->hidden)
                    hand = 0;
                uint32_t handle{};
                if (hand >= 0 &&
                    ensure(manager, &handle, swingRope, static_cast<uint32_t>(hand), nullptr, 0, 0) &&
                    get(manager, handle)) {
                    rope->handle = handle;
                    rope->hand = static_cast<uint32_t>(hand);
                    rope->age = 0;
                    rope->sinceGood = 1000;
                    rope->badRun = 0;
                    rope->hidden = false;
                    ++created;
                    ++linesMade;
                } else if (hand >= 0) {
                    ++failures;
                }
            }
            if (!rope->handle)
                continue;
            target(manager, rope->handle, &want.end.x);
            if (const auto slot = get(manager, rope->handle))
                std::memcpy(reinterpret_cast<void*>(slot + 0x6c0), &leaseSeconds, sizeof(leaseSeconds));
            ++rope->age;
            ++count;
        }
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
        // A hand that draws a tie's rope starts it where the tie's web sticks,
        // for this update; its points as they were again after it.
        Vec3 handPointsBefore[2][4]{};
        for (unsigned hand = 0; hand < 2; ++hand) {
            if (!tieHand[hand])
                continue;
            for (int p = 0; p < 4; ++p)
                std::memcpy(&handPointsBefore[hand][p],
                            reinterpret_cast<void*>(mgr + 0x7d60 + p * 0x18 + hand * 12), sizeof(Vec3));
            const Vec3 none{};
            const Vec3 at = tieAnchor[hand];
            for (const auto [offset, point] : {std::pair{0x7d60, at}, std::pair{0x7d78, none},
                                               std::pair{0x7d90, at}, std::pair{0x7da8, at}})
                std::memcpy(reinterpret_cast<void*>(mgr + offset + hand * 12), &point, sizeof(point));
        }
        // What each rope is laid with at its start (handPoints, which the
        // update calls): one web in both hands comes to the upper wrist from
        // the lower one, and a web that bends round edges goes round them.
        // The game's own line to the anchor leaves from the rope's Start, not
        // from the last point laid (679dc0 places every point after the laid
        // ones between Start and the anchor), so for this update Start is the
        // last point laid. Left at the wrist, a web round an edge came back
        // from that edge to the wrist and ran on from there through the
        // building to where it sticks (October 15: "a second line is drawn
        // from the original edge and the hand"), doubled back on itself as
        // it took the edge, which lay on the line from the wrist to the end;
        // the game closed 0.1 s after a web took one.
        laying = {};
        const auto& held = wanted.held;
        for (unsigned hand = 0; hand < 2; ++hand) {
            const auto& rope = owned[hand];
            if (!heldHooked || !wrote[hand] || !rope.handle || rope.trailing)
                continue;
            auto bent = wantedBends[hand];
            bent.count = std::min<uint32_t>(bent.count, native_webs::maxRopeBends);
            for (uint32_t k = 0; k < bent.count; ++k)
                if (!finite(bent.points[k]))
                    bent.count = 0;
            const Vec3 lower = held.wrist + travel;
            const bool both = held.rope == hand + 1 && finite(lower) && std::isfinite(held.slack);
            const auto slot = both || bent.count ? get(manager, rope.handle) : 0;
            uintptr_t typeData{};
            int16_t most{};
            // With room for the hand's points: the most points the rope's
            // type takes.
            if (!slot || !read(slot + 8, &typeData, 8) || !read(typeData + 0x28, &most, 2))
                continue;
            Laying lay{};
            lay.count = native_webs::ropePoints(starts[hand], both ? &lower : nullptr,
                                                both ? std::clamp(held.slack, 0.f, 3.f) : 0.f, bent,
                                                std::min<int>(8, std::min<int>(128, most) - 2), lay.points,
                                                &lay.drawnAt);
            if (lay.count < 2)
                continue;
            lay.rope = slot;
            laying[hand] = lay;
            const Vec3 from = lay.points[lay.count - 1];
            std::memcpy(reinterpret_cast<void*>(mgr + 0x7d60 + hand * 12), &from, sizeof(from));
        }
        // The hand points of each of Spidy's ropes, by slot (the order the
        // update lays them), for the hand points: the first rope's on each
        // hand set now, each next one's after the one before it.
        ropeStarts = {};
        startMoves = 0;
        bool anyLine = false;
        for (const auto& l : lineOwned)
            anyLine |= l.handle != 0;
        if (heldHooked && anyLine) {
            size_t n = 0;
            const Vec3 none{};
            for (unsigned hand = 0; hand < 2; ++hand) {
                for (int p = 0; p < 4; ++p)
                    read(mgr + 0x7d60 + p * 0x18 + hand * 12, &handAfter[hand][p], sizeof(Vec3));
                const auto& rope = owned[hand];
                if (rope.handle && !rope.trailing && wrote[hand])
                    if (const auto slot = get(manager, rope.handle); slot && n < ropeStarts.size()) {
                        RopeStart r{slot, hand, {}, false};
                        for (int p = 0; p < 4; ++p)
                            r.points[p] =
                                handAfter[hand][p]; // as set above: the wrist, or the last edge laid
                        ropeStarts[n++] = r;
                    }
            }
            for (const auto& t : tieOwned)
                if (t.handle && n < ropeStarts.size())
                    if (const auto slot = get(manager, t.handle)) {
                        const Vec3 at = tieAnchor[t.hand];
                        ropeStarts[n++] = {slot, t.hand, {at, none, at, at}, false};
                    }
            for (const auto& l : lineOwned)
                if (l.handle && n < ropeStarts.size())
                    if (const auto slot = get(manager, l.handle))
                        for (uint32_t k = 0; k < linesCount; ++k)
                            if (linesWanted[k].id == l.id) {
                                const Vec3 at = linesWanted[k].start;
                                RopeStart r{slot, l.hand, {at, none, at, at}, true};
                                r.viaCount = std::min<uint32_t>(linesWanted[k].viaCount, 2);
                                for (uint32_t v = 0; v < r.viaCount; ++v)
                                    r.via[v] = linesWanted[k].via[v];
                                ropeStarts[n++] = r;
                            }
            std::sort(ropeStarts.begin(), ropeStarts.begin() + n,
                      [](const RopeStart& a, const RopeStart& b) { return a.rope < b.rope; });
            for (unsigned hand = 0; hand < 2; ++hand)
                for (size_t j = 0; j < n; ++j)
                    if (ropeStarts[j].hand == hand) {
                        for (int p = 0; p < 4; ++p)
                            std::memcpy(reinterpret_cast<void*>(mgr + 0x7d60 + p * 0x18 + hand * 12),
                                        &ropeStarts[j].points[p], sizeof(Vec3));
                        break;
                    }
        }
        originalUpdate(manager, dt);
        ropeStarts = {};
        const auto laid = laying;
        laying = {};
        std::memcpy(reinterpret_cast<void*>(mgr + 0x7d40), hashes, sizeof(hashes));
        for (unsigned hand = 0; hand < 2; ++hand)
            if (tieHand[hand])
                for (int p = 0; p < 4; ++p)
                    std::memcpy(reinterpret_cast<void*>(mgr + 0x7d60 + p * 0x18 + hand * 12),
                                &handPointsBefore[hand][p], sizeof(Vec3));
        // Start is the wrist again between updates: the hero's own update
        // (95f6c0) takes the hand's velocity from how it moves.
        for (unsigned hand = 0; hand < 2; ++hand)
            if (laid[hand].rope)
                std::memcpy(reinterpret_cast<void*>(mgr + 0x7d60 + hand * 12), &starts[hand], sizeof(Vec3));
        // Each hand's points as they were before the update (the wrist, or as
        // the tie and the edges above left them, restored there).
        if (heldHooked && anyLine)
            for (unsigned hand = 0; hand < 2; ++hand) {
                bool used = false;
                for (const auto& l : lineOwned)
                    used |= l.handle && l.hand == hand;
                if (!used || tieHand[hand])
                    continue; // a tie's hand was set back above
                for (int p = 1; p < 4; ++p)
                    std::memcpy(reinterpret_cast<void*>(mgr + 0x7d60 + p * 0x18 + hand * 12),
                                &handAfter[hand][p], sizeof(Vec3));
                const Vec3 back = wrote[hand] ? starts[hand] : handAfter[hand][0];
                std::memcpy(reinterpret_cast<void*>(mgr + 0x7d60 + hand * 12), &back, sizeof(Vec3));
            }
        // Whether the game drew each line's rope from the line's start: its
        // third point near the line (the first is the start, as laid, and
        // a rope drawn from the hand leaves it from there).
        uint32_t linesGood{};
        uint64_t checks{}, good{}, bad{}, shared{};
        float lastOff{}, worstOff{};
        for (uint32_t k = 0; k < native_webs::maxLineRopes; ++k) {
            auto& rope = lineOwned[k];
            const auto slot = rope.handle ? get(manager, rope.handle) : 0;
            const native_webs::LineRope* want{};
            for (uint32_t w = 0; w < linesCount; ++w)
                if (linesWanted[w].id == rope.id)
                    want = &linesWanted[w];
            Vec3 third{};
            // Its second point: on the line both ways (the first of the line,
            // or of a strand's top, as laid).
            // (After the points laid through first: the first between its start and end.)
            const uint32_t past = want ? std::min<uint32_t>(want->viaCount, 2) + 1 : 1;
            if (!slot || !want || !read(slot + 0x1c + past * 12, &third, sizeof(third)) || !finite(third))
                continue;
            const Vec3 ab = want->end - want->start;
            const float span = dot(ab, ab);
            const Vec3 nearest =
                span > 1e-6f ? want->start + ab * std::clamp(dot(third - want->start, ab) / span, 0.f, 1.f)
                             : want->start;
            const float off = length(third - nearest);
            if (rope.age < native_webs::lineRopeSettle) {
                // Still being shot out: the game's web is there, so the
                // overlay draws no strand over it (November 3: "for a moment
                // it is a white line, then the right one").
                linesGood |= 1u << k;
                continue;
            }
            ++checks;
            lastOff = off;
            worstOff = std::max(worstOff, off);
            if (off <= native_webs::lineRopeTolerance) {
                ++good;
                rope.sinceGood = 0;
                rope.badRun = 0;
            } else {
                ++bad;
                shared += handBusy(rope.hand) ? 1u : 0u;
                rope.sinceGood = std::min<uint32_t>(rope.sinceGood + 1, 1000);
                // Drawn from the hand: let go of it rather than show that.
                if (++rope.badRun >= native_webs::lineRopeBadRun) {
                    release(manager, &rope.handle, false, false);
                    rope.handle = 0;
                    rope.hidden = true;
                    ++released;
                    ++linesReleased;
                    ++linesHidden;
                }
            }
            if (rope.handle && rope.sinceGood <= 20)
                linesGood |= 1u << k;
        }
        // The bit of each line (in submitLines' order) the game draws.
        uint32_t byLine{};
        for (uint32_t w = 0; w < linesCount; ++w)
            for (uint32_t k = 0; k < native_webs::maxLineRopes; ++k)
                if ((linesGood >> k) & 1u && lineOwned[k].id == linesWanted[w].id)
                    byLine |= 1u << w;
        lineMask = byLine;
        AcquireSRWLockExclusive(&lineLock);
        auto& t = SpidyLineRopeData;
        InterlockedIncrement64(&t.sequence);
        t.status = heldHooked ? 2u : 3u;
        t.made += linesMade;
        t.released += linesReleased;
        t.hidden += linesHidden;
        t.checks += checks;
        t.good += good;
        t.bad += bad;
        t.shared += shared;
        t.moves += startMoves;
        if (checks) {
            t.lastOff = lastOff;
            t.worstOff = std::max(t.worstOff, worstOff);
        }
        InterlockedIncrement64(&t.sequence);
        ReleaseSRWLockExclusive(&lineLock);
        // 679dc0 starts each attached rope's points (rope +1c) at Start. A
        // released one (+70c bit 2) drifts from it, and the tail above moved
        // point 0 away, so a gap here means the rope does not leave the wrist.
        // A web in both hands leaves the upper wrist where it comes to it.
        for (unsigned hand = 0; hand < 2; ++hand) {
            const bool ours = wrote[hand] && owned[hand].handle && !owned[hand].trailing;
            const auto slot = ours ? get(manager, owned[hand].handle) : 0;
            const auto& lay = laid[hand];
            const int first = slot && slot == lay.rope && lay.drawnAt > 0 ? lay.drawnAt : 0;
            if (slot && read(slot + 0x1c + first * 12, &built[hand], sizeof(Vec3)) && finite(built[hand])) {
                startError = std::max(startError, length(built[hand] - starts[hand]));
                builtMask |= 1u << hand;
            }
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
        // The hand points are optional: without them the overlay draws the
        // piece of a web held in both hands between them, as before.
        const unsigned char handPointsBytes[] = {0x48, 0x8b, 0xc4, 0xf3, 0x0f, 0x11, 0x50, 0x18,
                                                 0x55, 0x53, 0x56, 0x41, 0x56, 0x48, 0x8d, 0xa8};
        if (!heldHooked && entry(0x95f9c0, handPointsBytes, sizeof(handPointsBytes))) {
            handPointsHook = reinterpret_cast<void*>(base + 0x95f9c0);
            heldHooked = MH_CreateHook(handPointsHook, reinterpret_cast<void*>(handPoints),
                                       reinterpret_cast<void**>(&originalHandPoints)) == MH_OK;
        }
        if (heldHooked && MH_QueueEnableHook(handPointsHook) != MH_OK)
            heldHooked = false;
        stopping = false;
        AcquireSRWLockExclusive(&statusLock);
        current = {};
        current.state = waiting;
        ReleaseSRWLockExclusive(&statusLock);
        enabled = true;
        // Both under one freeze of the game's threads (stereo_probe.cpp says why).
        auto s = MH_QueueEnableHook(updateHook);
        if (s == MH_OK)
            s = MH_ApplyQueued();
        if (s != MH_OK) {
            enabled = false;
            result = 6300 + s;
            MH_QueueDisableHook(updateHook);
            if (heldHooked)
                MH_QueueDisableHook(handPointsHook);
            MH_ApplyQueued();
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
void native_webs::submit(const Request& next, uint32_t leaseMs, const Bent* bends) {
    AcquireSRWLockExclusive(&requestLock);
    request = next;
    requestBends[0] = bends ? bends[0] : Bent{};
    requestBends[1] = bends ? bends[1] : Bent{};
    requestDeadline = GetTickCount64() + leaseMs;
    ReleaseSRWLockExclusive(&requestLock);
}
void native_webs::submitTies(const TieRope* ropes, uint32_t count, uint32_t leaseMs) {
    AcquireSRWLockExclusive(&requestLock);
    tieRequestCount = ropes ? std::min(count, maxTieRopes) : 0u;
    for (uint32_t k = 0; k < maxTieRopes; ++k)
        tieRequest[k] = k < tieRequestCount ? ropes[k] : TieRope{};
    tieDeadline = GetTickCount64() + leaseMs;
    ReleaseSRWLockExclusive(&requestLock);
}
uint32_t native_webs::tiesDrawn() {
    return enabled && !stopping && GetTickCount64() - lastHeroUpdate.load() < 500 ? tieMask.load() : 0u;
}
void native_webs::submitLines(const LineRope* ropes, uint32_t count, uint32_t leaseMs) {
    AcquireSRWLockExclusive(&requestLock);
    lineRequestCount = ropes ? std::min(count, maxLineRopes) : 0u;
    for (uint32_t k = 0; k < maxLineRopes; ++k)
        lineRequest[k] = k < lineRequestCount ? ropes[k] : LineRope{};
    lineDeadline = GetTickCount64() + leaseMs;
    ReleaseSRWLockExclusive(&requestLock);
}
uint32_t native_webs::linesDrawn() {
    return enabled && !stopping && GetTickCount64() - lastHeroUpdate.load() < 500 ? lineMask.load() : 0u;
}
native_webs::Status native_webs::status() {
    AcquireSRWLockShared(&statusLock);
    auto out = current;
    ReleaseSRWLockShared(&statusLock);
    return out;
}
bool native_webs::drawsHeld() {
    return heldHooked && enabled && !stopping;
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
    // Version 2 ends before the bends.
    const auto from = reinterpret_cast<uintptr_t>(input);
    if (!read(from, &c, native_webs::probeCommandBytesV2) || c.magic != 0x5357424d ||
        (c.version == 3 ? c.bytes != sizeof(c) || !read(from, &c, sizeof(c))
                        : c.version != 2 || c.bytes != native_webs::probeCommandBytesV2) ||
        c.leaseMs > 500 || !finite(c.request.feet) || c.request.held.rope > 2 || !finite(c.request.held.wrist) ||
        !std::isfinite(c.request.held.slack))
        return 6502;
    for (const auto& hand : c.request.hands)
        if (hand.attached > 2 || hand.tracked > 1 || !finite(hand.anchor) || !finite(hand.wrist))
            return 6502;
    for (const auto& bent : c.bends) {
        if (bent.count > native_webs::maxRopeBends)
            return 6502;
        for (uint32_t k = 0; k < bent.count; ++k)
            if (!finite(bent.points[k]))
                return 6502;
    }
    native_webs::submit(c.request, c.leaseMs, c.bends);
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
    if (heldHooked)
        MH_DisableHook(handPointsHook);
    for (auto& part : webParts)
        part.store(0, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&statusLock);
    current.state = result ? failed : off;
    current.live = 0;
    ReleaseSRWLockExclusive(&statusLock);
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
