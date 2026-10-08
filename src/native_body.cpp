// Spider-Man's own body in VR: the hero's joints, turned right after the
// game's animation writes them (native_body.hpp, body_ik.hpp).
#include "spidy/native_body.hpp"
#include "spidy/body_ik.hpp"
#include "spidy/native_appearance.hpp"
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include <windows.h>
using namespace spidy;
using native_body::Command;
using native_body::Status;
extern "C" {
__declspec(dllexport) Status SpidyBodyData;
__declspec(dllexport) native_body::Poses SpidyBodyPoses;
}
namespace {
// The pose job's writer and the local-to-model conversion it calls
// (out, local pose, joints, count), which also builds the rest pose here.
using PoseWriter = float (*)(float*, void*);
using ToModel = float (*)(float*, const void*, const void*, uint32_t);
uintptr_t base{};
std::atomic<uintptr_t> record{};
PoseWriter originalWriter{};
void* writerHook{};
bool hooked{};
std::atomic<bool> enabled{};
SRWLOCK lifecycle = SRWLOCK_INIT, commandLock = SRWLOCK_INIT, solveLock = SRWLOCK_INIT,
        statusLock = SRWLOCK_INIT;
Command command{};
uint64_t commandDeadline{};
// The probe's joint roles for a rig of `overrideJoints` joints (0: none).
struct Roles {
    uint32_t magic, version, bytes, joints;
    int16_t pelvis, head, eyes[2];
    int16_t spineCount, neckCount, spine[8], neck[8];
    int16_t arms[2][6]; // clavicle, upper, lower, hand, finger, thumb
    int16_t legs[2][3]; // upper, lower, foot
};
static_assert(sizeof(Roles) == 96);
Roles roles{};
// The hero's rigs as the body uses them (solveLock). Pose jobs for the hero's
// joints may name more than one rig: in a headset session (October 6) the
// body's blend restarted from nothing every frame or two, for seconds at a
// time, during the game's own landings, ledge climbs and jumps, and only a
// change of rig did that. Each rig keeps an entry, read again when it names
// other tables than when it was read (freed and reused) or the probe's roles
// change; the body's state is the player's and stays across rigs.
struct Cache {
    uintptr_t rig{}, table{}, restTable{}; // the rig, its joints and rest pose when read
    uint32_t count{}, rolesSerial{};
    uint64_t used{};
    uint32_t joints{}; // as read, 0 if it could not be
    body::Rig ik;
    std::vector<float> rest; // for the probe's capture
    uint32_t problem = native_body::noRig;
};
std::array<Cache, 4> caches;
uint64_t cacheUses{};
std::atomic<uint32_t> rolesSerial{1};
body::State state;
body::Targets lastTargets;
// The hero's instance and rig at the previous hero job (solveLock): another
// instance is another actor, and the body starts over on it.
uintptr_t lastInstance{}, lastRig{};
uint32_t rigSwitches{};
// The hero actor's heading in the world at the last solve (solveLock): the
// body's yaw is the model's, and turns back by as much as the actor turns.
float actorHeading{};
bool actorHeadingSet{};
// The proportions of the rig the body last used (solveLock), for the T-pose
// calibration.
body_calibration::Proportions heroProportions{};
bool heroProportionsSet{};
// The tracking space's yaw at the last command the body followed.
float trackingYaw{};
bool trackingYawSet{};
LARGE_INTEGER lastSolve{}, frequency{};
std::atomic<bool> captureWanted{};
// The hero's instance turn at its latest pose job, for the render's check
// (solveLock).
float jobRows[9]{};
bool jobRowsSet{};
std::atomic<uint32_t> heroJobsThisFrame{};
// When the body last changed the hero's joints (GetTickCount64), and whether
// it changed the latest hero job's: the eyes may show the hero only while it
// does. A job it left alone (a rig it cannot use) shows the game's pose and
// head, whatever an earlier job of the frame did.
std::atomic<uint64_t> lastDrawn{};
std::atomic<bool> latestSolved{};

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
template <class F> void publish(F change) {
    AcquireSRWLockExclusive(&statusLock);
    InterlockedIncrement64(&SpidyBodyData.sequence);
    change(SpidyBodyData);
    InterlockedIncrement64(&SpidyBodyData.sequence);
    ReleaseSRWLockExclusive(&statusLock);
}
// The game's name hash (1bb88d0): CRC-32 table steps seeded with 0xedb88320,
// no final inversion.
uint32_t nameHash(const char* name) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xedb88320u;
    for (; *name; ++name)
        c = table[(c ^ static_cast<uint8_t>(*name)) & 0xff] ^ (c >> 8);
    return c;
}
struct RigData {
    std::vector<int16_t> parent;
    std::vector<uint32_t> hash;
    std::vector<float> rest; // model space, the layout body_ik works on
};
// The game's conversion of a local pose to model space, guarded.
bool toModel(float* out, uintptr_t local, uintptr_t joints, uint32_t count) {
    __try {
        reinterpret_cast<ToModel>(base + 0x1600770)(out, reinterpret_cast<const void*>(local),
                                                    reinterpret_cast<const void*>(joints), count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// Reads the rig's joints and builds its rest pose with the game's own
// conversion. A root (parent -1) is placed from the matrix before the first
// joint, which must be the identity.
bool readRig(uintptr_t rig, RigData& out) {
    uint16_t count{};
    const auto joints = pointer(rig + 8), rest = pointer(rig + 0x18);
    if (!read(rig + 2, &count, 2) || !count || count > native_body::maxJoints || !joints || !rest)
        return false;
    std::vector<uint8_t> table(count * 16ull);
    if (!read(joints, table.data(), table.size()))
        return false;
    out.parent.resize(count);
    out.hash.resize(count);
    for (uint32_t j = 0; j < count; ++j) {
        std::memcpy(&out.parent[j], &table[j * 16], 2);
        std::memcpy(&out.hash[j], &table[j * 16 + 8], 4);
        if (out.parent[j] >= static_cast<int>(j) || out.parent[j] < -1)
            return false; // the conversion needs parents before children
    }
    std::vector<float> model((count + 1) * 16ull);
    const float identity[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(model.data(), identity, sizeof(identity));
    if (!toModel(model.data() + 16, rest, joints, count))
        return false;
    out.rest.assign(model.begin() + 16, model.end());
    for (const float v : out.rest)
        if (!std::isfinite(v))
            return false;
    return true;
}
int byName(const RigData& rig, std::initializer_list<const char*> names) {
    for (const auto* name : names) {
        const auto h = nameHash(name);
        for (size_t j = 0; j < rig.hash.size(); ++j)
            if (rig.hash[j] == h)
                return static_cast<int>(j);
    }
    return -1;
}
// The joints from `from` (exclusive) up to `to` (inclusive) along the
// parents, bottom first; empty when `to` is not under `from`.
std::vector<int16_t> chain(const RigData& rig, int from, int to) {
    std::vector<int16_t> out;
    for (int j = to; j >= 0 && j != from; j = rig.parent[j]) {
        out.push_back(static_cast<int16_t>(j));
        if (out.size() > rig.parent.size())
            return {};
        if (rig.parent[j] < 0)
            return {};
    }
    std::reverse(out.begin(), out.end());
    return out;
}
// Roles from the probe's override, or from the rig's joint names.
uint32_t identify(const RigData& data, body::Rig& rig) {
    rig = {};
    rig.parent = data.parent;
    const int n = static_cast<int>(data.parent.size());
    if (roles.joints && roles.joints == static_cast<uint32_t>(n)) {
        rig.pelvis = roles.pelvis;
        rig.head = roles.head;
        rig.eyes[0] = roles.eyes[0];
        rig.eyes[1] = roles.eyes[1];
        rig.spine.assign(roles.spine, roles.spine + std::clamp<int>(roles.spineCount, 0, 8));
        rig.neck.assign(roles.neck, roles.neck + std::clamp<int>(roles.neckCount, 0, 8));
        for (int i = 0; i < 2; ++i) {
            const auto* a = roles.arms[i];
            rig.arms[i] = {a[0], a[1], a[2], a[3], a[4], a[5]};
            rig.legs[i] = {roles.legs[i][0], roles.legs[i][1], roles.legs[i][2]};
        }
        return native_body::none;
    }
    // The hero's rig (Spider-Man Remastered 4.0630, every suit tried so far;
    // tools/probe_game_body.py lists it): a_body is the root of the body,
    // with the pelvis (and under it the legs) and the spine as its children.
    // Fingers A to D run index to little finger; B_B is the middle knuckle.
    // The body never moves the rig's other roots (fx_root, sync, the IK
    // targets, cameraTarget, aimTarget), which gameplay reads.
    rig.pelvis = byName(data, {"a_body", "pelvis"});
    rig.head = byName(data, {"head"});
    const int chest = byName(data, {"chest"});
    const char* side[2] = {"LF_", "RT_"};
    for (int i = 0; i < 2; ++i) {
        const std::string s = side[i];
        auto named = [&](std::initializer_list<const char*> names) {
            for (const auto* name : names)
                if (const int j = byName(data, {(s + name).c_str()}); j >= 0)
                    return j;
            return -1;
        };
        rig.arms[i] = {named({"clavicle"}), named({"uparm"}), named({"loarm"}), named({"wrist"}),
                       named({"finger_B_B"}), named({"thumb_A"})};
        rig.legs[i] = {named({"upleg"}), named({"loleg"}), named({"foot"})};
        rig.eyes[i] = named({"middle_eye_deform"});
        // Fingers A (index) to D (little), from the base in the palm to the
        // end past the tip; the thumb likewise.
        for (const char* finger : {"A", "B", "C", "D"}) {
            std::vector<int16_t> chain;
            for (const char* joint : {"_A", "_B", "_C", "_D", "_D_end"})
                if (const int j = named({(std::string("finger_") + finger + joint).c_str()}); j >= 0)
                    chain.push_back(static_cast<int16_t>(j));
            if (chain.size() == 5)
                rig.arms[i].fingers.push_back(chain);
        }
        for (const char* joint : {"thumb_A", "thumb_B", "thumb_C", "thumb_C_end"})
            if (const int j = named({joint}); j >= 0)
                rig.arms[i].thumbChain.push_back(static_cast<int16_t>(j));
        if (rig.arms[i].thumbChain.size() != 4)
            rig.arms[i].thumbChain.clear();
    }
    if (rig.pelvis < 0 || rig.head < 0 || chest < 0)
        return native_body::unknownRig;
    rig.spine = chain(data, rig.pelvis, chest);
    rig.neck = chain(data, chest, data.parent[rig.head]);
    if (rig.spine.empty() || (data.parent[rig.head] != chest && rig.neck.empty()))
        return native_body::unknownRig;
    for (const auto& a : rig.arms)
        if (a.upper < 0 || a.lower < 0 || a.hand < 0 || a.finger < 0)
            return native_body::unknownRig;
    for (const auto& l : rig.legs)
        if (l.upper < 0 || l.lower < 0 || l.foot < 0)
            return native_body::unknownRig;
    return native_body::none;
}
// Which way the actor faces about the world's up (its forward row, or, facing
// straight up or down, its left one), radians.
float heading(const float* m) {
    const float fx = m[8], fz = m[10];
    if (fx * fx + fz * fz > .09f)
        return std::atan2(fx, fz);
    return std::atan2(m[0], m[2]) - 1.5707963f;
}
// The hero instance's model axes in world (rows of its transform), as a
// rotation; false unless they are one.
bool instanceTurn(const float* m, Quat& out) {
    Vec3 axes[3];
    for (int r = 0; r < 3; ++r) {
        axes[r] = {m[r * 4], m[r * 4 + 1], m[r * 4 + 2]};
        if (!finite(axes[r]) || std::abs(length(axes[r]) - 1) > .02f)
            return false;
    }
    if (dot(axes[0], cross(axes[1], axes[2])) < .9f)
        return false;
    out = body::fromAxes(axes[0], axes[1], axes[2]);
    return true;
}
// The cache entry of a rig (solveLock): found, or read into the entry the rig
// had, an unused one, or the one used longest ago.
Cache& rigEntry(uintptr_t rig) {
    uint16_t count{};
    const uintptr_t table = pointer(rig + 8), restTable = pointer(rig + 0x18);
    read(rig + 2, &count, sizeof(count));
    const uint32_t serial = rolesSerial.load();
    Cache* slot = &caches[0];
    for (auto& c : caches) {
        if (c.rig == rig && c.table == table && c.restTable == restTable && c.count == count &&
            c.rolesSerial == serial) {
            c.used = ++cacheUses;
            return c;
        }
        if (slot->rig != rig && (c.rig == rig || c.used < slot->used))
            slot = &c;
    }
    Cache& entry = *slot;
    entry = {};
    entry.rig = rig;
    entry.table = table;
    entry.restTable = restTable;
    entry.count = count;
    entry.rolesSerial = serial;
    entry.used = ++cacheUses;
    RigData data;
    if (!rig || !readRig(rig, data)) {
        entry.problem = native_body::noRig;
    } else {
        entry.joints = static_cast<uint32_t>(data.parent.size());
        entry.problem = identify(data, entry.ik);
        if (!entry.problem && !body::prepare(entry.ik, data.rest))
            entry.problem = native_body::badRest;
        entry.rest = data.rest;
    }
    return entry;
}
void solveHero(float* out, uintptr_t job, uintptr_t instance) {
    const auto rig = pointer(job + 0x28);
    float transform[16]{};
    Quat toWorld{};
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&solveLock);
    const auto started = now;
    if (instance != lastInstance) {
        state = {};
        actorHeadingSet = trackingYawSet = false;
        lastInstance = instance;
        lastRig = 0;
    }
    if (rig != lastRig) {
        rigSwitches += lastRig != 0;
        lastRig = rig;
    }
    const Cache& cache = rigEntry(rig);
    Command wanted{};
    AcquireSRWLockShared(&commandLock);
    wanted = command;
    const bool live = GetTickCount64() < commandDeadline;
    ReleaseSRWLockShared(&commandLock);
    const bool on = live && (wanted.flags & native_body::bodyOn);
    uint32_t problem = cache.problem;
    const bool turned = read(instance, transform, sizeof(transform)) && instanceTurn(transform, toWorld);
    if (!problem && !turned)
        problem = native_body::badInstance;
    body::Result result;
    float scale = 1, armScale = 1;
    const bool capture = captureWanted.exchange(false);
    const uint32_t joints = cache.joints;
    if (capture && joints <= native_body::maxJoints)
        std::memcpy(SpidyBodyPoses.game, out, joints * 64ull);
    if (turned) {
        // The actor turned under the body since the last job: the body keeps
        // facing the same way in the world.
        const float facing = heading(transform);
        if (actorHeadingSet)
            body::turnState(state, -std::remainder(facing - actorHeading, 6.2831853f));
        actorHeading = facing;
        actorHeadingSet = true;
    }
    if (on && std::isfinite(wanted.trackingYaw)) {
        // A snap turn turns the player, the body with it.
        if (trackingYawSet)
            body::turnState(state, std::remainder(wanted.trackingYaw - trackingYaw, 6.2831853f));
        trackingYaw = wanted.trackingYaw;
        trackingYawSet = true;
    }
    if (!problem && (on || state.weight > 0)) {
        if (on) {
            // World axes relative to the feet into the hero's model space.
            const Quat toModel = toWorld.conjugate();
            body::Targets t;
            t.head = true;
            t.airborne = wanted.flags & native_body::airborne;
            t.up = toModel.rotate({0, 1, 0});
            t.eyes = toModel.rotate(wanted.eyes);
            t.facing = toModel * wanted.facing;
            if (const Quat tilt = wanted.tilt; tilt.x != 0 || tilt.y != 0 || tilt.z != 0)
                t.tilt = toModel * tilt * toWorld;
            for (int i = 0; i < 2; ++i) {
                const auto& h = wanted.hands[i];
                t.hands[i] = {h.tracked != 0, toModel.rotate(h.grip), toModel * h.orientation, h.fist};
            }
            lastTargets = t;
        }
        body::Config config;
        config.hideHead = wanted.flags & native_body::hideHead;
        config.handOrientation = wanted.flags & native_body::handTurn;
        // The player's size: their T-pose calibration's eye height and arm,
        // or the headset's running height and the hero's own arms.
        heroProportions = body_calibration::proportions(cache.ik);
        heroProportionsSet = true;
        if (wanted.height > 1 && wanted.height < 2.5f && cache.ik.eyeHeight > 1)
            scale = wanted.flags & native_body::calibrated
                        ? body_calibration::bodyScale(wanted.height, heroProportions)
                        : std::clamp(wanted.height / cache.ik.eyeHeight, .85f, 1.2f);
        armScale = body_calibration::armScale(wanted.armLength, scale, heroProportions);
        const float dt = lastSolve.QuadPart ? static_cast<float>(now.QuadPart - lastSolve.QuadPart) / frequency.QuadPart
                                            : 0.f;
        lastSolve = now;
        body::Pose pose(out, static_cast<int>(joints));
        result = body::solve(pose, cache.ik, lastTargets, config, state, on, dt, scale, armScale);
    } else {
        lastSolve = now;
    }
    if (capture && joints <= native_body::maxJoints) {
        InterlockedIncrement64(&SpidyBodyPoses.sequence);
        SpidyBodyPoses.joints = joints;
        SpidyBodyPoses.rig = rig;
        SpidyBodyPoses.instance = instance;
        SpidyBodyPoses.out = reinterpret_cast<uintptr_t>(out);
        ++SpidyBodyPoses.captured;
        std::memcpy(SpidyBodyPoses.transform, transform, sizeof(transform));
        std::memcpy(SpidyBodyPoses.body, out, joints * 64ull);
        if (cache.rest.size() == joints * 16ull)
            std::memcpy(SpidyBodyPoses.rest, cache.rest.data(), joints * 64ull);
        InterlockedIncrement64(&SpidyBodyPoses.sequence);
    }
    if (turned) {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                jobRows[r * 3 + c] = transform[r * 4 + c];
        jobRowsSet = true;
    }
    if (result.solved && on)
        lastDrawn = GetTickCount64();
    latestSolved = result.solved && on;
    const uint32_t switches = rigSwitches;
    LARGE_INTEGER finished{};
    QueryPerformanceCounter(&finished);
    ReleaseSRWLockExclusive(&solveLock);
    ++heroJobsThisFrame;
    publish([&](Status& d) {
        ++d.heroJobs;
        d.solved += result.solved;
        d.problem = problem;
        d.joints = joints;
        d.rig = rig;
        d.rigSwitches = switches;
        d.instance = instance;
        d.state = problem ? native_body::failed : result.solved ? native_body::active : native_body::waiting;
        d.weight = result.weight;
        d.scale = scale;
        d.armScale = armScale;
        d.calibrated = (wanted.flags & native_body::calibrated) != 0;
        d.yaw = result.yaw;
        d.grounded = result.grounded;
        d.handError[0] = result.handError[0];
        d.handError[1] = result.handError[1];
        d.headError = result.headError;
        d.solveMs += static_cast<double>(finished.QuadPart - started.QuadPart) * 1000 / frequency.QuadPart;
    });
}
float writer(float* out, void* job) {
    const float moved = originalWriter(out, job);
    if (!enabled.load(std::memory_order_relaxed))
        return moved;
    InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&SpidyBodyData.jobs));
    const auto hero = record.load(std::memory_order_relaxed);
    const auto instance = hero ? pointer(hero) : 0;
    if (instance && pointer(instance + 0xd8) == reinterpret_cast<uintptr_t>(out))
        solveHero(out, reinterpret_cast<uintptr_t>(job), instance);
    return moved;
}
} // namespace

uint32_t native_body::start(uintptr_t gameBase) {
    AcquireSRWLockExclusive(&lifecycle);
    uint32_t result{};
    do {
        if (enabled)
            break;
        base = gameBase;
        QueryPerformanceFrequency(&frequency);
        const unsigned char writerBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54,
                                             0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x20};
        const unsigned char modelBytes[] = {0x48, 0x8b, 0xc4, 0x57, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00};
        if (!base || !entry(0x1601290, writerBytes, sizeof(writerBytes)) ||
            !entry(0x1600770, modelBytes, sizeof(modelBytes))) {
            result = 7001;
            break;
        }
        if (!hooked) {
            const auto s = MH_Initialize();
            if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
                result = 7100 + s;
                break;
            }
            writerHook = reinterpret_cast<void*>(base + 0x1601290);
            if (const auto c = MH_CreateHook(writerHook, reinterpret_cast<void*>(writer),
                                             reinterpret_cast<void**>(&originalWriter));
                c != MH_OK) {
                result = 7200 + c;
                break;
            }
            hooked = true;
        }
        AcquireSRWLockExclusive(&solveLock);
        caches.fill({});
        state = {};
        lastInstance = lastRig = 0;
        rigSwitches = 0;
        lastSolve = {};
        ReleaseSRWLockExclusive(&solveLock);
        latestSolved = false;
        publish([](Status& d) {
            const auto sequence = d.sequence;
            d = {};
            d.sequence = sequence;
            d.state = waiting;
            d.problem = noRig;
        });
        enabled = true;
        if (const auto s = MH_EnableHook(writerHook); s != MH_OK) {
            enabled = false;
            result = 7300 + s;
        }
    } while (false);
    if (result)
        publish([&](Status& d) { d.state = failed; });
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
void native_body::retarget(uintptr_t heroRecord) {
    record = heroRecord;
}
void native_body::submit(const Command& next) {
    AcquireSRWLockExclusive(&commandLock);
    command = next;
    commandDeadline = GetTickCount64() + std::min<uint32_t>(next.leaseMs, 500);
    ReleaseSRWLockExclusive(&commandLock);
}
void native_body::renderFrame() {
    if (!enabled.load(std::memory_order_relaxed))
        return;
    // How far the hero turned between its pose job and this render.
    const auto hero = record.load();
    const auto instance = hero ? pointer(hero) : 0;
    float m[16]{}, rows[16]{};
    float turn = -1;
    AcquireSRWLockShared(&solveLock);
    const bool set = jobRowsSet;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            rows[r * 4 + c] = jobRows[r * 3 + c];
    ReleaseSRWLockShared(&solveLock);
    Quat atRender{}, atJob{};
    if (instance && set && read(instance, m, sizeof(m)) && instanceTurn(m, atRender) && instanceTurn(rows, atJob))
        turn = body::angle(atRender * atJob.conjugate());
    const uint32_t jobs = heroJobsThisFrame.exchange(0);
    publish([&](Status& d) {
        ++d.renders;
        d.heroJobsLastFrame = jobs;
        d.heroJobsMax = std::max(d.heroJobsMax, jobs);
        if (turn >= 0) {
            d.turnLast = turn;
            d.turnMax = std::max(d.turnMax, turn);
        }
    });
}
bool native_body::drawn() {
    // The hero's joints are rewritten every frame; a pose that stopped
    // coming through the body (another writer, a paused job, a rig it cannot
    // use) shows the head.
    return enabled.load(std::memory_order_relaxed) && latestSolved.load() && GetTickCount64() - lastDrawn.load() < 150;
}
native_body::Status native_body::status() {
    AcquireSRWLockShared(&statusLock);
    auto out = SpidyBodyData;
    ReleaseSRWLockShared(&statusLock);
    return out;
}
bool native_body::proportions(body_calibration::Proportions& out) {
    AcquireSRWLockShared(&solveLock);
    const bool set = heroProportionsSet;
    if (set)
        out = heroProportions;
    ReleaseSRWLockShared(&solveLock);
    return set;
}
uint32_t native_body::stop() {
    AcquireSRWLockExclusive(&lifecycle);
    // Blend back to the game's pose for a moment, then let go of it.
    AcquireSRWLockExclusive(&commandLock);
    command.flags &= ~bodyOn;
    ReleaseSRWLockExclusive(&commandLock);
    const auto limit = GetTickCount64() + 400;
    while (enabled && status().weight > 0 && GetTickCount64() < limit)
        Sleep(10);
    enabled = false;
    uint32_t result{};
    if (hooked)
        if (const auto s = MH_DisableHook(writerHook); s != MH_OK && s != MH_ERROR_DISABLED)
            result = 7400 + s;
    publish([&](Status& d) { d.state = result ? failed : off; });
    ReleaseSRWLockExclusive(&lifecycle);
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyBodyStart(void* input) {
    native_body::ProbeConfig c{};
    if (!read(reinterpret_cast<uintptr_t>(input), &c, sizeof(c)) || c.magic != 0x53424346 || c.version != 1 ||
        c.bytes != sizeof(c) || c.pid != GetCurrentProcessId() ||
        c.base != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) || !c.record)
        return 7501;
    // The eye views anchor to this hero and hide it unless the body is on
    // it, as in a VR session.
    native_appearance::setPlayerRecord(c.record);
    native_body::retarget(c.record);
    return native_body::start(c.base);
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyBodySubmit(void* input) {
    // A version 1 command (the probe's) is shorter: read as many bytes as it says.
    Command c{};
    uint32_t header[3]{};
    const auto at = reinterpret_cast<uintptr_t>(input);
    if (!read(at, header, sizeof(header)) || header[0] != 0x53424443 ||
        !((header[1] == 1 && header[2] == native_body::commandBytesV1) ||
          (header[1] == 2 && header[2] == sizeof(c))) ||
        !read(at, &c, header[2]) || c.leaseMs > 500 || !finite(c.eyes) || !std::isfinite(c.height) ||
        !(c.armLength >= 0 && c.armLength <= 2))
        return 7502;
    const float tilt = c.tilt.x * c.tilt.x + c.tilt.y * c.tilt.y + c.tilt.z * c.tilt.z + c.tilt.w * c.tilt.w;
    if (!std::isfinite(tilt) || std::abs(tilt - 1) > .01f)
        return 7502;
    native_body::submit(c);
    return 0;
}
// The probe's joint roles for the rig it names by joint count; a count of 0
// returns to the names.
extern "C" __declspec(dllexport) DWORD WINAPI SpidyBodyRoles(void* input) {
    Roles r{};
    if (!read(reinterpret_cast<uintptr_t>(input), &r, sizeof(r)) || r.magic != 0x53424452 || r.version != 1 ||
        r.bytes != sizeof(r) || r.joints > native_body::maxJoints)
        return 7503;
    AcquireSRWLockExclusive(&solveLock);
    roles = r;
    ReleaseSRWLockExclusive(&solveLock);
    ++rolesSerial;
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyBodyCapture(void*) {
    captureWanted = true;
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyBodyStop(void*) {
    return native_body::stop();
}
