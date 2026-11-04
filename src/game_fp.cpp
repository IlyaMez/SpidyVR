// First-person cutscenes: finds the head of the character a scene plays (see
// game_fp.hpp). Entity layout, as everywhere in this project: an entity object
// holds its world matrix at +0, its flags at +0x5c (0x20 hidden, 0x200 parked),
// its handle at +0x64, an info pointer at +0x88 (the skeleton's asset id at
// info+8, its joint table at info+0x200 with the joint count as a u16 at +2)
// and its pose array at +0xd8. The head joint is the one named 0x37519912.
//
// MSVC: functions with __try hold no object that needs a destructor.
#include "spidy/game_fp.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

namespace spidy::game_fp {
namespace {
constexpr uintptr_t getJointRva = 0x1775a80, hideRva = 0x191afb0, showRva = 0x191b880;
constexpr uintptr_t poolBaseRva = 0x7a44380, poolCountRva = 0x7a4439c;
constexpr uint32_t headHash = 0x37519912u;
constexpr uint64_t peterHead = 0x9870FFAD9BAF955AULL, photoHead = 0x9EC5B594796AB2EDULL,
                   mjHead = 0xA5642706FFA88585ULL;
// Peter's own body in the apartment scenes (181 joints; its head joint carries the head the game flags
// there): a skeleton known by what it is, which the view can read whatever the game flags on it.
constexpr uint64_t peterBody = 0x9A61012E52D29386ULL;
// Aunt May's scene: Spider-Man there (masked, then with the mask off) is two entities of this one body (233
// joints); the unmasked one carries Peter's face rig (his head asset, 514 joints).
constexpr uint64_t peterSceneBody = 0x9862E9D2D5173E1DULL;
// The suit-up scenes bring their own Spider-Man body of 212 joints: 878B in the MJ apartment, BF45 in the
// intro (Peter's apartment); both are drawn in the shot while the player's entity is hidden.
constexpr uint64_t suitUpBodyMj = 0x878B7EEABDC354A2ULL;
constexpr uint64_t suitUpBodyIntro = 0xBF45D48B5314EC76ULL;
inline bool isPeterBody(uint64_t asset) {
    return asset == peterBody || asset == peterSceneBody || asset == suitUpBodyMj || asset == suitUpBodyIntro;
}
// The suited hero's own body, and the empty head that comes with it.
constexpr uint64_t suitBody = 0xB742D90D7B153BDFULL, emptyHead = 0x8D7D1866DB29AEEEULL;
// How far above the head joint the eyes sit, in the hero's own space, metres.
constexpr float eyeUp = .04f;
constexpr uint32_t entityBytes = 0xc0;
using GetJoint = float*(__fastcall*)(uint64_t object, float* out, uint32_t hash);
using Visibility = void(__fastcall*)(void* entity);

uintptr_t exe{};
GetJoint getJoint{};
Visibility hideEntity{}, showEntity{};
std::atomic<bool> wanted{}, released{};
std::atomic<uintptr_t> heroObject{};
std::atomic<uint32_t> sourceKind{};      // Status::source
std::atomic<uint32_t> standIn{}, twin{}; // pool handles
std::atomic<uint64_t> lastSampleMs{}, lostMs{};
// How long the last head holds when its source is gone (a cut between scenes): the view stays, the screen
// does not flash.
constexpr uint64_t holdMs = 1500;
std::atomic<uint32_t> nSamples{}, nMisses{}, nPasses{}, nHidden{};
std::atomic<uint32_t> passMicros{};
volatile LONG64 vqCalls = 0;
HANDLE workerThread{};

// The skeletons the view is on, whose head joints the pose job shrinks (shrinkHead): the body that plays the
// character (the hero, or the stand-in's twin) and the stand-in's own head object.
std::atomic<uintptr_t> shrinkObject[2]{};
// For the log: what the pose job did for the skeletons the view is on (a head that stays visible: is it never
// shrunk, has the rig no head joint, or is the head not on this skeleton's joints).
std::atomic<uint32_t> shrinkDone{}, shrinkNoHead{}, shrinkOffTarget{};
bool okPointer(uint64_t p) {
    return p > 0x10000 && p < 0x00007FFFFFFFFFFFull;
}
// A cache of committed regions per thread: one VirtualQuery in this game costs
// about 0.3 ms, and a pass over the entity pool asks about thousands of objects.
bool readable(uint64_t address, size_t size) {
    if (address < 0x10000 || address > 0x00007FFFFFFFFFFFull)
        return false;
    static thread_local uint64_t begins[256], ends[256];
    static thread_local ULONGLONG times[256];
    static thread_local int count = 0, next = 0;
    const ULONGLONG now = GetTickCount64();
    uint64_t at = address;
    const uint64_t end = address + size;
    while (at < end) {
        bool hit = false;
        for (int i = 0; i < count; ++i)
            if (at >= begins[i] && at < ends[i] && now - times[i] < 1000) {
                at = ends[i];
                hit = true;
                break;
            }
        if (hit)
            continue;
        MEMORY_BASIC_INFORMATION info;
        InterlockedIncrement64(&vqCalls);
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(at), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        const uint64_t from = reinterpret_cast<uint64_t>(info.BaseAddress), to = from + info.RegionSize;
        int slot = -1;
        for (int i = 0; i < count; ++i)
            if (begins[i] == from) {
                slot = i;
                break;
            }
        if (slot < 0) {
            if (count < 256)
                slot = count++;
            else {
                slot = next;
                next = (next + 1) & 255;
            }
        }
        begins[slot] = from;
        ends[slot] = to;
        times[slot] = now;
        at = to;
    }
    return true;
}
bool readU32(uint64_t address, uint32_t& value) {
    __try {
        value = *reinterpret_cast<volatile uint32_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool readU64(uint64_t address, uint64_t& value) {
    __try {
        value = *reinterpret_cast<volatile uint64_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// Looks like a game entity object: an info pointer at +0x88.
bool entityOk(uint64_t object) {
    __try {
        return okPointer(object) && readable(object, 0x100) &&
               okPointer(*reinterpret_cast<uint64_t*>(object + 0x88));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// --- the entity pool: 0xc0 bytes an entry, the object pointer first, a generation (11 bits) at +0x10.
uint64_t poolBase() {
    uint64_t base{};
    return readU64(exe + poolBaseRva, base) ? base : 0;
}
uint32_t poolCount() {
    uint32_t count{};
    return readU32(exe + poolCountRva, count) && count <= 0x100000 ? count : 0;
}
uint64_t poolEntry(uint32_t index) {
    const uint64_t base = poolBase();
    return base && index < poolCount() ? base + static_cast<uint64_t>(index) * entityBytes : 0;
}
// The entry a handle (generation << 20 | index) names, if it is still the same entity.
uint64_t resolve(uint32_t handle, uint32_t* index = nullptr) {
    const uint32_t generation = (handle >> 20) & 0x7ff;
    if (!generation)
        return 0;
    const uint32_t at = handle & 0xfffff;
    const uint64_t entry = poolEntry(at);
    uint32_t current{};
    if (!entry || !readU32(entry + 0x10, current) || (current & 0xffff) != generation)
        return 0;
    if (index)
        *index = at;
    return entry;
}
uint64_t objectOf(uint32_t handle) {
    const uint64_t entry = resolve(handle);
    uint64_t object{};
    return entry && readU64(entry, object) && entityOk(object) ? object : 0;
}
uint32_t handleOf(uint32_t index) {
    const uint64_t entry = poolEntry(index);
    uint32_t generation{};
    if (!entry || !readU32(entry + 0x10, generation))
        return 0;
    generation &= 0xffff;
    return generation && generation <= 0x7ff ? (generation << 20) | (index & 0xfffff) : 0;
}
// A skeleton object's asset id and joint count: 0 a skeleton, 1 junk (not an
// entity), 2 an entity that has no skeleton yet (one still spawning).
int classify(uint64_t object, uint64_t& asset, unsigned& joints) {
    __try {
        if (!okPointer(object) || !readable(object, 0x100))
            return 1;
        const uint64_t info = *reinterpret_cast<uint64_t*>(object + 0x88);
        if (!okPointer(info) || !readable(info, 0x208))
            return 2;
        const uint64_t table = *reinterpret_cast<uint64_t*>(info + 0x200);
        if (!okPointer(table) || !readable(table, 8))
            return 2;
        const unsigned n = *reinterpret_cast<uint16_t*>(table + 2);
        if (!n || n > 600)
            return 2;
        asset = *reinterpret_cast<uint64_t*>(info + 8);
        joints = n;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}
bool classifyNow(uint64_t object, uint64_t& asset, unsigned& joints) {
    return classify(object, asset, joints) == 0;
}
void multiply(const float* a, const float* b, float* c) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k)
                s += a[i * 4 + k] * b[k * 4 + j];
            c[i * 4 + j] = s;
        }
}
// The head joint of a skeleton object in the world, `up` metres above it in
// the joint's own space: false when it has no usable one.
bool headWorld(uint64_t object, float* out, float up) {
    __try {
        if (!getJoint || !okPointer(object))
            return false;
        const uint64_t info = *reinterpret_cast<uint64_t*>(object + 0x88);
        if (!okPointer(info) || !okPointer(*reinterpret_cast<uint64_t*>(info + 0x200)) ||
            !okPointer(*reinterpret_cast<uint64_t*>(object + 0xd8)))
            return false;
        float joint[32]{};
        getJoint(object, joint, headHash);
        for (int i = 0; i < 16; ++i)
            if (!std::isfinite(joint[i]))
                return false;
        // (A head the pose job shrank has axes a thousandth long: still a head, its axes are normalised
        // later.)
        if (std::abs(joint[15] - 1.f) > .01f ||
            joint[0] * joint[0] + joint[1] * joint[1] + joint[2] * joint[2] < 1e-8f)
            return false;
        joint[13] += up;
        const float* world = reinterpret_cast<const float*>(object);
        for (int i = 0; i < 16; ++i)
            if (!std::isfinite(world[i]))
                return false;
        multiply(joint, world, out);
        for (int i = 0; i < 16; ++i)
            if (!std::isfinite(out[i]))
                return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// Two world matrices at the same place with the same axes.
bool sameFrame(const float* a, const float* b, float position, float axes) {
    const float dx = a[12] - b[12], dy = a[13] - b[13], dz = a[14] - b[14];
    if (dx * dx + dy * dy + dz * dz > position * position)
        return false;
    for (int r = 0; r < 3; ++r) {
        const float* p = a + r * 4;
        const float* q = b + r * 4;
        const float pl = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        const float ql = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
        if (pl < 1e-6f || ql < 1e-6f || (p[0] * q[0] + p[1] * q[1] + p[2] * q[2]) / (pl * ql) < axes)
            return false;
    }
    return true;
}
bool isHeadAsset(uint64_t a) {
    return a == peterHead || a == photoHead || a == mjHead;
}

// --- what every pool entry holds, cached: refilled only when its generation or
// object pointer changes, so a whole pass is one cheap read per entry. Worker only.
struct Cache {
    uint16_t* generation{};
    uint64_t* object{};
    uint64_t* asset{};
    uint16_t* joints{};
    uint32_t* retry{};
    uint32_t* first{};
    uint32_t size{}, count{};
} cache;
void refreshCache() {
    uint32_t n = poolCount();
    const uint64_t base = poolBase();
    if (!base || !n) {
        cache.count = 0;
        return;
    }
    if (n > cache.size) {
        std::free(cache.generation);
        std::free(cache.object);
        std::free(cache.asset);
        std::free(cache.joints);
        std::free(cache.retry);
        std::free(cache.first);
        cache.generation = static_cast<uint16_t*>(std::calloc(n, sizeof(uint16_t)));
        cache.object = static_cast<uint64_t*>(std::calloc(n, sizeof(uint64_t)));
        cache.asset = static_cast<uint64_t*>(std::calloc(n, sizeof(uint64_t)));
        cache.joints = static_cast<uint16_t*>(std::calloc(n, sizeof(uint16_t)));
        cache.retry = static_cast<uint32_t*>(std::calloc(n, sizeof(uint32_t)));
        cache.first = static_cast<uint32_t*>(std::calloc(n, sizeof(uint32_t)));
        if (!cache.generation || !cache.object || !cache.asset || !cache.joints || !cache.retry ||
            !cache.first) {
            cache.size = cache.count = 0;
            return;
        }
        cache.size = n;
    }
    LARGE_INTEGER begin, end, frequency;
    QueryPerformanceCounter(&begin);
    const uint32_t now = static_cast<uint32_t>(GetTickCount64());
    bool chunkOk = false;
    for (uint32_t i = 0; i < n; ++i) {
        if ((i & 255) == 0) {
            const uint32_t m = n - i < 256 ? n - i : 256;
            chunkOk =
                readable(base + static_cast<uint64_t>(i) * entityBytes, static_cast<size_t>(m) * entityBytes);
        }
        if (!chunkOk) {
            cache.generation[i] = 0;
            continue;
        }
        const uint64_t entry = base + static_cast<uint64_t>(i) * entityBytes;
        const uint16_t generation = *reinterpret_cast<volatile uint16_t*>(entry + 0x10);
        if (!generation) {
            cache.generation[i] = 0;
            continue;
        }
        const uint64_t object = *reinterpret_cast<volatile uint64_t*>(entry);
        // Unchanged: a failed classification of an old entry is retried every 10 s, a new one every pass for
        // 3 s.
        if (cache.generation[i] == generation && cache.object[i] == object &&
            (cache.joints[i] || static_cast<int32_t>(now - cache.retry[i]) < 0))
            continue;
        if (cache.generation[i] != generation || cache.object[i] != object)
            cache.first[i] = now;
        cache.generation[i] = generation;
        cache.object[i] = object;
        cache.asset[i] = 0;
        cache.joints[i] = 0;
        uint64_t asset{};
        unsigned joints{};
        const int result = okPointer(object) ? classify(object, asset, joints) : 1;
        if (result == 0) {
            cache.asset[i] = asset;
            cache.joints[i] = static_cast<uint16_t>(joints);
        } else {
            cache.retry[i] = result == 2 && now - cache.first[i] < 3000 ? now : now + 10000;
        }
    }
    cache.count = n;
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart > 0)
        passMicros = static_cast<uint32_t>(1e6 * static_cast<double>(end.QuadPart - begin.QuadPart) /
                                           static_cast<double>(frequency.QuadPart));
    ++nPasses;
}

// --- the scene camera, as the eye module sees it each frame (camera()): who is in the shot decides whom the
// view is on
std::atomic<float> camPose[16];
// Half the width and half the height of the camera's picture, as tangents (0: not known).
std::atomic<float> camHalfWidth{}, camHalfHeight{};
std::atomic<uint64_t> camMs{};
// The scene's own camera as the view (sceneCamera): 0 never, 1 when no head can be found, 2 always (no head
// is looked for). lastCameraMs: the last frame the camera was the view; viewOnCamera: the last frame was.
std::atomic<int> cameraMode{};
std::atomic<uint64_t> lastCameraMs{};
std::atomic<bool> viewOnCamera{};
bool cameraNow(float* out) {
    const uint64_t t = camMs.load(std::memory_order_acquire);
    if (!t || GetTickCount64() - t > 500)
        return false;
    for (int i = 0; i < 16; ++i)
        out[i] = camPose[i].load(std::memory_order_relaxed);
    return true;
}
// In the shot: inside the picture the scene's camera draws (its own field of view, a tenth wider so that a
// head at the edge counts), ahead of it. When the picture's size is not known, a cone of about 60 degrees to
// either side and 45 up or down. Visibility in the shot, not distance: how far he is does not matter.
bool inShot(const float* camera, const float* point) {
    const float dx = point[0] - camera[12], dy = point[1] - camera[13], dz = point[2] - camera[14];
    const float ahead = dx * camera[8] + dy * camera[9] + dz * camera[10];
    const float across = dx * camera[0] + dy * camera[1] + dz * camera[2];
    const float below = dx * camera[4] + dy * camera[5] + dz * camera[6];
    float halfWidth = camHalfWidth.load(std::memory_order_relaxed),
          halfHeight = camHalfHeight.load(std::memory_order_relaxed);
    if (!(halfWidth > .2f && halfWidth < 5.f && halfHeight > .1f && halfHeight < 5.f)) {
        halfWidth = 1.8f;
        halfHeight = 1.f;
    } else {
        halfWidth *= 1.1f;
        halfHeight *= 1.1f;
    }
    return ahead > .2f && std::fabs(across) < halfWidth * ahead && std::fabs(below) < halfHeight * ahead;
}

// --- hiding: asked for by the worker, done by the render thread (the game's own
// DrawOff / DrawOn handlers are meant for the game's main thread).
struct Request {
    uint32_t handle;
    int on;
};
Request queue[64];
volatile LONG queueWrite = 0, queueRead = 0;
void ask(uint32_t handle, bool hide) {
    const LONG w = queueWrite;
    if (w - queueRead >= 64)
        return; // never reached while the render thread drains it every frame
    queue[w & 63] = {handle, hide ? 1 : 0};
    MemoryBarrier();
    queueWrite = w + 1;
}
// Owned by the render thread: the handles kept hidden, and whether this module
// hid each one (an entity the game had hidden already stays the game's).
struct Kept {
    uint32_t handle;
    bool ours;
};
Kept kept[48];
int keptCount = 0;
void keep(uint32_t handle) {
    for (int i = 0; i < keptCount; ++i)
        if (kept[i].handle == handle)
            return;
    if (keptCount >= 48) {
        int w = 0;
        for (int i = 0; i < keptCount; ++i)
            if (resolve(kept[i].handle))
                kept[w++] = kept[i];
        keptCount = w;
    }
    if (keptCount < 48)
        kept[keptCount++] = {handle, false};
}
void hideNow(Kept& k) {
    __try {
        const uint64_t object = objectOf(k.handle);
        uint32_t flags{};
        if (!object || !hideEntity || !readU32(object + 0x5c, flags))
            return;
        if (!(flags & 0x20)) {
            hideEntity(reinterpret_cast<void*>(object));
            k.ours = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
void showAll() {
    for (int i = 0; i < keptCount; ++i) {
        __try {
            const uint64_t object = objectOf(kept[i].handle);
            uint32_t flags{};
            if (kept[i].ours && object && showEntity && readU32(object + 0x5c, flags) && (flags & 0x20))
                showEntity(reinterpret_cast<void*>(object));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    keptCount = 0;
    nHidden = 0;
}
void drain() {
    while (queueRead != queueWrite) {
        const Request r = queue[queueRead & 63];
        queueRead = queueRead + 1;
        if (r.on)
            keep(r.handle);
        else {
            for (int i = 0; i < keptCount; ++i)
                if (kept[i].handle == r.handle) {
                    __try {
                        const uint64_t object = objectOf(r.handle);
                        uint32_t flags{};
                        if (kept[i].ours && object && showEntity && readU32(object + 0x5c, flags) &&
                            (flags & 0x20))
                            showEntity(reinterpret_cast<void*>(object));
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                    }
                    kept[i].handle = 0;
                }
        }
    }
}
void keepHidden() {
    unsigned live = 0;
    for (int i = 0; i < keptCount; ++i) {
        if (!kept[i].handle)
            continue;
        hideNow(kept[i]);
        ++live;
    }
    nHidden = live;
}

// --- the worker: which skeleton plays the player's character, and what is on its head.
// The worker's lines for the session log: a ring, so that several lines said within one headset frame all
// reach the log (the reader takes one line a frame; a single slot lost every line but the last of a burst).
constexpr uint32_t noteSlots = 256;
char noteText[noteSlots][200];
std::atomic<uint32_t> noteSerial{};
std::atomic<uint32_t> noteSeq[noteSlots]{}; // slot n % noteSlots holds line n once this says n + 1
void say(const char* text) {
    // More than one thread says lines (the worker, the pose-job hook, the render thread): a line claims its
    // number first, and the reader takes it only once its slot says it is written. (With one counter read and
    // then stored, two lines said together took the same slot and one was lost: the log showed the key line
    // without the lists before it.)
    const uint32_t n = noteSerial.fetch_add(1, std::memory_order_acq_rel);
    std::snprintf(noteText[n % noteSlots], sizeof(noteText[0]), "%s", text);
    noteSeq[n % noteSlots].store(n + 1, std::memory_order_release);
}
// The player's own choice of whose eyes (X on the left controller): requests counted by the render thread,
// taken by the worker (swapSeen is the worker's, and outlives a scene's Worker); altCount: how many of Miles
// and MJ are in the scene.
std::atomic<uint32_t> swapRequests{};
std::atomic<int> altCount{}, choiceShown{};
uint32_t swapSeen{};
struct Worker {
    int choice{}; // 0: automatic (the hero, or whoever the scene plays), 1: the one chosen with X
    uint64_t chosenObject{};
    uint64_t standInAsset{};
    ULONGLONG lastPass{}, lastTwin{}, selected{}, lastHide{}, quietSince{}, begin{};
    int twinTries{}, twinMisses{}, otherNoted{};
    bool standInHidden{};
    // The hero is not hidden, but Peter is in the scene on a skeleton of his own, not on the hero: the scene
    // plays Peter (the apartment eviction: Spider-Man stood outside the building, unhidden, while Peter was
    // inside).
    bool otherPlays{};
    // Since when the shot has asked for the other character (the switch waits a moment, so a frame of a cut
    // does not switch), when Peter's double was last seen, the last check of the way back, and the bodies the
    // log has named.
    ULONGLONG switchSince{}, peterSeen{}, lastBack{}, swapSince{}, lastSwap{};
    int flaggedNoted{}, partsNoted{};
    uint32_t bodyNoted[24]{}, flagged[6]{}, hiddenBodyFlags{};
    bool peterFirstNoted{}, quietNoted{};
    ULONGLONG quietLogAt{};
    int quietLogs{};
    uint64_t neighboursFor{}, heroObjectSeen{};
    bool heroHidden{}, heroHiddenKnown{};
    int heroTransitions{};
    uintptr_t shrinkWatch{};
    ULONGLONG shrinkSince{};
    uint32_t shrinkBase[3]{};
    bool shrinkReported{};
    int shrinkLogs{};
    int bodyNotedCount{};
    bool heroNoted{};
    // The stand-in's head object went but the body twin it was paired with is still there: the view reads the
    // body.
    bool twinOnly{};
    // The stand-in was chosen while the game had its head object flagged (hidden or parked, in the shot): the
    // flag is then no sign that it was lost.
    bool standInFlagged{};
    uint32_t asked[64]{};
    int askedCount{};
} w;
void hideOnce(uint32_t handle) {
    for (int i = 0; i < w.askedCount; ++i)
        if (w.asked[i] == handle)
            return;
    if (w.askedCount < 64)
        w.asked[w.askedCount++] = handle;
    ask(handle, true);
}
// What this worker hid goes back (the view moves to another character, whose parts are the ones to hide now).
void showAsked() {
    for (int i = 0; i < w.askedCount; ++i)
        ask(w.asked[i], false);
    w.askedCount = 0;
}
void forgetAll() {
    // The scene is over (or has no first person): everything this worker asked to hide is shown again.
    for (int i = 0; i < w.askedCount; ++i)
        ask(w.asked[i], false);
    w = Worker{};
    standIn = twin = 0;
    sourceKind = 0;
}
// Hides every small skeleton object whose head joint sits exactly where the
// source's does: the character's head, and what grows on it (hair, eyes,
// mouth, face parts). A body (100 joints or more) is never one.
int hideAtHead(const float* head, uint64_t sourceObject, uint64_t exceptObject, bool flagged = false) {
    int found = 0;
    for (uint32_t i = 0; i < cache.count; ++i) {
        const unsigned joints = cache.joints[i];
        if (!cache.generation[i] || joints < 2 || joints >= 100)
            continue;
        const uint64_t object = cache.object[i];
        if (object == sourceObject || object == exceptObject)
            continue;
        uint32_t flags{};
        if (!readU32(object + 0x5c, flags) || ((flags & 0x200) && !flagged))
            continue;
        float other[16];
        if (!headWorld(object, other, 0) || !sameFrame(head, other, .02f, .97f))
            continue;
        if (const uint32_t handle = handleOf(i)) {
            hideOnce(handle);
            ++found;
        }
    }
    return found;
}
// What sits at a body's head joint, for the log: the assets of the small skeleton objects (a head, its face,
// its hair) whose head joint is exactly where the body's is, up to three. Returns how many there are.
int headParts(const float* head, uint64_t bodyObject, uint64_t* assets, int room) {
    int found = 0;
    for (uint32_t i = 0; i < cache.count; ++i) {
        const unsigned joints = cache.joints[i];
        if (!cache.generation[i] || joints < 2 || joints >= 100 || cache.object[i] == bodyObject)
            continue;
        float other[16];
        if (!headWorld(cache.object[i], other, 0) || !sameFrame(head, other, .02f, .97f))
            continue;
        bool known = false;
        for (int k = 0; k < found && k < room; ++k)
            known = known || assets[k] == cache.asset[i];
        if (known)
            continue;
        if (found < room)
            assets[found] = cache.asset[i];
        ++found;
    }
    return found;
}
// A head object the game flags (hidden or parked) is a character in the scene only when a visible body
// carries it: a body of 100 joints or more, not hidden, whose head joint is where the head object's is (the
// apartment's Peter at his door, with the head object on his body). A flagged head with no visible body under
// it is a model parked in the scene (City Hall's Peter at the start, nearby and not part of it).
bool carriedByVisibleBody(const float* head, uint64_t headObject) {
    for (uint32_t i = 0; i < cache.count; ++i) {
        if (!cache.generation[i] || cache.joints[i] < 100 || cache.object[i] == headObject)
            continue;
        uint32_t flags{};
        float body[16];
        if (!readU32(cache.object[i] + 0x5c, flags) || (flags & 0x20) || !headWorld(cache.object[i], body, 0))
            continue;
        if (sameFrame(head, body, .02f, .97f))
            return true;
    }
    return false;
}
// For the log: the body (100 joints or more, not a head asset) whose head joint is nearest to a point.
bool nearestBody(const float* point, uint64_t& asset, unsigned& joints, uint32_t& flags, float& distance) {
    bool found = false;
    distance = 1e9f;
    for (uint32_t i = 0; i < cache.count; ++i) {
        if (!cache.generation[i] || cache.joints[i] < 100 || isHeadAsset(cache.asset[i]))
            continue;
        float other[16];
        uint32_t f{};
        if (!readU32(cache.object[i] + 0x5c, f) || !headWorld(cache.object[i], other, 0))
            continue;
        const float dx = point[0] - other[12], dy = point[1] - other[13], dz = point[2] - other[14];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < distance) {
            distance = d;
            asset = cache.asset[i];
            joints = cache.joints[i];
            flags = f;
            found = true;
        }
    }
    return found;
}
// The suit's own mask and lens pieces: separate skeleton objects spawned right
// before the hero's pool entry (the few entries just below it).
void hideSuitPieces(uint64_t heroObjectAddress) {
    uint32_t heroIndex = 0;
    bool have = false;
    for (uint32_t i = 0; i < cache.count && !have; ++i)
        if (cache.generation[i] && cache.object[i] == heroObjectAddress) {
            heroIndex = i;
            have = true;
        }
    if (!have || heroIndex < 2)
        return;
    for (int64_t k = static_cast<int64_t>(heroIndex) - 8; k < static_cast<int64_t>(heroIndex); ++k) {
        if (k < 0)
            continue;
        const uint32_t i = static_cast<uint32_t>(k);
        if (!cache.generation[i] || cache.joints[i] < 2 || cache.joints[i] > 40)
            continue;
        const uint64_t object = cache.object[i];
        if (object == heroObjectAddress || !entityOk(object))
            continue;
        if (const uint32_t handle = handleOf(i))
            hideOnce(handle);
    }
}
// The body that plays the stand-in's head: a visible skeleton of 100 joints or
// more, not a head asset, whose head joint sits exactly on the stand-in head's.
uint32_t findTwin(uint64_t standInObject) {
    float standInHead[16];
    if (!headWorld(standInObject, standInHead, 0))
        return 0;
    uint32_t best = 0;
    unsigned bestJoints = 0;
    float bestDistance = 1e9f;
    for (uint32_t i = 0; i < cache.count; ++i) {
        if (!cache.generation[i] || cache.joints[i] < 100 || isHeadAsset(cache.asset[i]))
            continue;
        const uint64_t object = cache.object[i];
        if (object == standInObject)
            continue;
        const uint32_t handle = handleOf(i);
        uint32_t flags{};
        if (!handle || handle == standIn.load() || !readU32(object + 0x5c, flags) || (flags & 0x220))
            continue;
        float other[16];
        if (!headWorld(object, other, 0) || !sameFrame(standInHead, other, .02f, .97f))
            continue;
        const float dx = standInHead[12] - other[12], dy = standInHead[13] - other[13],
                    dz = standInHead[14] - other[14];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (cache.joints[i] > bestJoints || (cache.joints[i] == bestJoints && d < bestDistance)) {
            best = handle;
            bestJoints = cache.joints[i];
            bestDistance = d;
        }
    }
    return best;
}
// Makes the skeleton at pool index `index` the view (its head joint, the view reading it as a body: a twin).
bool followBody(uint32_t index) {
    const uint32_t handle = handleOf(index);
    if (!handle)
        return false;
    showAsked();
    w.quietSince = 0;
    w.standInAsset = 0;
    w.standInFlagged = false;
    w.selected = GetTickCount64();
    w.lastTwin = 0;
    w.twinTries = w.twinMisses = 0;
    w.standInHidden = false;
    w.twinOnly = true;
    standIn = 0;
    twin = handle;
    sourceKind = 4;
    return true;
}
void tick() {
    if (cameraMode.load() == 2) {
        sourceKind = 6; // the scene's own camera is the view: no character is looked for (work() does not
                        // call this either)
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const uint64_t hero = heroObject.load();
    uint32_t heroFlags{};
    if (!hero || !entityOk(hero) || !readU32(hero + 0x5c, heroFlags)) {
        sourceKind = 1;
        return;
    }
    char text[240];
    if (!w.begin)
        w.begin = now;
    {
        // For the log: the player's entity changing (the suit-up swaps who plays), and the hero hidden or
        // shown again.
        const bool hidden = (heroFlags & 0x20) != 0;
        if (w.heroTransitions < 24 && ((w.heroObjectSeen && w.heroObjectSeen != hero) ||
                                       (w.heroHiddenKnown && hidden != w.heroHidden))) {
            ++w.heroTransitions;
            uint64_t a{};
            unsigned j{};
            if (!classifyNow(hero, a, j))
                a = j = 0;
            std::snprintf(
                text, sizeof(text),
                "first person: the player's entity is %016llX, %u joints, %s (flags %08X) (at %llu ms)",
                static_cast<unsigned long long>(a), j, hidden ? "hidden" : "visible", heroFlags,
                static_cast<unsigned long long>(now % 100000000ull));
            say(text);
        }
        w.heroObjectSeen = hero;
        w.heroHidden = hidden;
        w.heroHiddenKnown = true;
    }
    {
        // Whose eyes: every character drawn in the scene that is not the hero's own entity and is known by
        // its body asset (Miles, MJ, Spider-Man, and all of Peter's bodies; no other character, whatever the
        // joint count), in pool order; the player's X goes to the next, and after the last back to the
        // automatic choice (the hero, or whoever the scene plays).
        constexpr int maxCandidates = 12;
        int cands[maxCandidates];
        int nCands = 0;
        for (uint32_t i = 0; i < cache.count && nCands < maxCandidates; ++i) {
            if (!cache.generation[i] || cache.joints[i] < 100 || cache.object[i] == hero)
                continue;
            const uint64_t a = cache.asset[i];
            const bool named = a == 0x9A1B2EE486D6B6DEULL || a == mjHead || a == 0xAF3AC54D239CD584ULL ||
                               a == suitBody || isPeterBody(a) || a == 0x80C52396F2470510ULL ||
                               a == 0x8CDBDBE3D4F0C56FULL || a == 0x878B7EEABDC354A2ULL;
            if (!named)
                continue;
            uint32_t flags{};
            float at[16];
            if (!readU32(cache.object[i] + 0x5c, flags) || (flags & 0x20) ||
                !headWorld(cache.object[i], at, 0))
                continue;
            cands[nCands++] = static_cast<int>(i);
        }
        altCount = nCands;
        auto backToAuto = [&]() {
            showAsked(); // what was hidden for the chosen character (and not for the hero) is drawn again
            w.choice = 0;
            w.chosenObject = 0;
            w.twinOnly = false;
            w.selected = 0;
            w.standInAsset = 0;
            w.standInFlagged = false;
            standIn = twin = 0;
            sourceKind = 1;
        };
        const uint32_t req = swapRequests.load();
        if (req != swapSeen) {
            swapSeen = req;
            int at = -1; // where the chosen one stands in the list now
            for (int k = 0; k < nCands; ++k)
                if (w.chosenObject && cache.object[cands[k]] == w.chosenObject)
                    at = k;
            const int next = w.chosenObject ? at + 1 : 0; // a chosen one that is gone: from the first
            if (next >= nCands) {
                backToAuto();
                say("first person: character choice: automatic again (X)");
            } else {
                w.choice = 1;
                w.chosenObject = cache.object[cands[next]];
                uint32_t chosenFlags{};
                readU32(w.chosenObject + 0x5c, chosenFlags);
                std::snprintf(
                    text, sizeof(text),
                    "first person: character choice %d of %d: asset %016llX, %u joints, flags %08X (X)",
                    next + 1, nCands, static_cast<unsigned long long>(cache.asset[cands[next]]),
                    static_cast<unsigned>(cache.joints[cands[next]]), chosenFlags);
                say(text);
            }
        }
        choiceShown = w.choice;
        if (w.choice) {
            int target = -1;
            for (int k = 0; k < nCands; ++k)
                if (cache.object[cands[k]] == w.chosenObject)
                    target = cands[k];
            if (target < 0 || !followBody(static_cast<uint32_t>(target))) {
                backToAuto();
                say("first person: the chosen character is gone from the scene: automatic again");
            } else {
                // His head parts, and what hangs just below his pool entry (mask, lenses, hair), are hidden
                // as for the hero.
                const uint64_t body = cache.object[target];
                float bodyHead[16];
                if (now - w.lastHide >= 500 && headWorld(body, bodyHead, 0)) {
                    w.lastHide = now;
                    hideAtHead(bodyHead, body, 0, true);
                    hideSuitPieces(body);
                }
                return;
            }
        }
    }
    {
        // What the pose job did to the skeleton the view is on, once, a moment after the view moved to it.
        const uintptr_t body = shrinkObject[0].load(std::memory_order_relaxed);
        if (body != w.shrinkWatch) {
            w.shrinkWatch = body;
            w.shrinkSince = now;
            w.shrinkReported = false;
            w.shrinkBase[0] = shrinkDone.load(std::memory_order_relaxed);
            w.shrinkBase[1] = shrinkNoHead.load(std::memory_order_relaxed);
            w.shrinkBase[2] = shrinkOffTarget.load(std::memory_order_relaxed);
        } else if (body && !w.shrinkReported && w.shrinkLogs < 12 && now - w.shrinkSince >= 1500) {
            w.shrinkReported = true;
            ++w.shrinkLogs;
            uint64_t asset{};
            unsigned joints{};
            if (!classifyNow(body, asset, joints))
                asset = joints = 0;
            std::snprintf(text, sizeof(text),
                          "first person: head shrink on the view's skeleton (%016llX, %u joints): done %u, "
                          "rig without a head joint %u, "
                          "pose jobs for other skeletons %u",
                          static_cast<unsigned long long>(asset), joints,
                          shrinkDone.load(std::memory_order_relaxed) - w.shrinkBase[0],
                          shrinkNoHead.load(std::memory_order_relaxed) - w.shrinkBase[1],
                          shrinkOffTarget.load(std::memory_order_relaxed) - w.shrinkBase[2]);
            say(text);
        }
    }
    if (!(heroFlags & 0x20) && !w.otherPlays) {
        // The hero plays himself. His head (and what is on it) is whatever sits at his head joint.
        if (standIn.load() || twin.load()) {
            say("first person: the hero is visible again, the stand-in is released");
            standIn = twin = 0;
            w.standInAsset = 0;
            w.twinOnly = false;
        }
        // A glimpse of Peter's double (walking through a doorway) is a few tenths of a second: look every 100
        // ms.
        if (now - w.lastPass < 100u)
            return;
        w.lastPass = now;
        refreshCache();
        float head[16];
        if (!headWorld(hero, head, 0)) {
            sourceKind = 1;
            return;
        }
        float cam[16];
        const bool haveCam = cameraNow(cam);
        const bool heroIn = haveCam && inShot(cam, head + 12);
        // The hero is not Spider-Man's own body (Miles, MJ: the scenes where they are the player character,
        // such as City Hall with all three in it): Peter, where he is in the scene, is the view and stays it,
        // whether or not he is in the shot, and the other characters' turns in the shot do not take it from
        // him. Identity of the hero's body, not shot or distance. (Spider-Man's own body: the shot decides,
        // as in the intro and the apartment.)
        uint64_t heroBodyAsset{};
        unsigned heroBodyJoints{};
        const bool peterFirst = classifyNow(hero, heroBodyAsset, heroBodyJoints) && heroBodyAsset != suitBody;
        // Peter's head, visible, on a skeleton that is not the hero's (its head joint is not at his): Peter
        // is in the scene beside the hero, or instead of him (the apartment eviction: Spider-Man stands
        // outside, unhidden, while Peter is inside). Identity of the skeleton, not distance. (Another
        // character's head, MJ's or a photo head, is no sign of this: they stand in scenes the hero plays.)
        // The shot decides which of the two the view is on: Peter if he is in it and the hero is not; the
        // hero stays the view while he is out of the shot as well (a cut to whom he talks to), until Peter is
        // the one in it.
        bool peterHere = false, peterIn = false, peterFlagged = false;
        uint32_t peterIndex = 0;
        for (uint32_t i = 0; i < cache.count; ++i) {
            if (!cache.generation[i] || !cache.joints[i] || !isHeadAsset(cache.asset[i]))
                continue;
            uint32_t flags{};
            if (!readU32(cache.object[i] + 0x5c, flags))
                continue;
            float other[16];
            if (!headWorld(cache.object[i], other, 0))
                continue;
            const bool flagged = (flags & 0x220) != 0;
            const bool in = haveCam && inShot(cam, other + 12);
            if (flagged) {
                // Hidden or parked by the game. Named once each (what Peter inside a building is flagged as,
                // and which body is nearest to him). Counted as Peter only while he is in the shot (the
                // apartment: his head is flagged while the camera has him).
                const uint32_t handle = handleOf(i);
                const uint32_t key = handle * 2654435761u ^ flags;
                bool known = false;
                for (int k = 0; k < w.flaggedNoted && k < 6; ++k)
                    known = known || w.flagged[k] == key;
                if (handle && !known && w.flaggedNoted < 6 && cache.asset[i] == peterHead &&
                    !sameFrame(head, other, .02f, .97f)) {
                    w.flagged[w.flaggedNoted++] = key;
                    uint64_t bodyAsset{};
                    unsigned bodyJoints{};
                    uint32_t bodyFlags{};
                    float bodyDistance{};
                    if (nearestBody(other + 12, bodyAsset, bodyJoints, bodyFlags, bodyDistance))
                        std::snprintf(text, sizeof(text),
                                      "first person: a Peter head is in the scene but flagged %08X; shot: "
                                      "%d; nearest body: "
                                      "%016llX, %u joints, flags %08X, %.2f m",
                                      flags, in ? 1 : 0, static_cast<unsigned long long>(bodyAsset),
                                      bodyJoints, bodyFlags, bodyDistance);
                    else
                        std::snprintf(
                            text, sizeof(text),
                            "first person: a Peter head is in the scene but flagged %08X; shot: %d; no body",
                            flags, in ? 1 : 0);
                    say(text);
                }
                if (!(in && cache.asset[i] == peterHead && !sameFrame(head, other, .02f, .97f)) ||
                    !carriedByVisibleBody(other, cache.object[i]))
                    continue;
            } else {
                if (sameFrame(head, other, .02f, .97f))
                    continue;
                if (w.otherNoted < 6) {
                    ++w.otherNoted;
                    std::snprintf(text, sizeof(text),
                                  "first person: a visible head (asset %016llX) is not on the hero%s; shot: "
                                  "hero %d, it %d",
                                  static_cast<unsigned long long>(cache.asset[i]),
                                  cache.asset[i] == peterHead ? " (Peter)" : "", heroIn ? 1 : 0, in ? 1 : 0);
                    say(text);
                }
            }
            if (cache.asset[i] != peterHead)
                continue;
            // Two Peters (the one in the picture and one that is not, such as one under the map): the one in
            // the shot; of two in the shot, the one the game has not flagged.
            if (!peterHere || (in && !peterIn) || (in == peterIn && peterFlagged && !flagged)) {
                peterHere = true;
                peterIndex = i;
                peterIn = in;
                peterFlagged = flagged;
            }
        }
        // Peter's own body, known by its asset: where it is in the shot decides as for his head. Flagged or
        // not.
        bool bodyHere = false, bodyIn = false;
        uint32_t bodyIndex = 0;
        for (uint32_t i = 0; i < cache.count; ++i) {
            if (!cache.generation[i] || cache.joints[i] < 100 || !isPeterBody(cache.asset[i]))
                continue;
            uint32_t bodyFlags{};
            if (!readU32(cache.object[i] + 0x5c, bodyFlags))
                continue;
            float other[16];
            if (!headWorld(cache.object[i], other, 0) || sameFrame(head, other, .02f, .97f))
                continue;
            const bool in = haveCam && inShot(cam, other + 12);
            if (bodyFlags & 0x20) {
                // Hidden by the game (not drawn): the apartment hallway has this body parked and hidden, away
                // from where Peter walks, and it is not the character the scene shows. Named once per flag
                // value.
                if (w.hiddenBodyFlags != bodyFlags) {
                    w.hiddenBodyFlags = bodyFlags;
                    std::snprintf(text, sizeof(text),
                                  "first person: Peter's body is there but hidden by the game (flags %08X); "
                                  "shot: %d; not used",
                                  bodyFlags, in ? 1 : 0);
                    say(text);
                }
                continue;
            }
            if (!bodyHere || (in && !bodyIn)) {
                bodyHere = true;
                bodyIndex = i;
                bodyIn = in;
            }
        }
        if (!w.heroNoted) {
            uint64_t heroAsset{};
            unsigned heroJoints{};
            if (classifyNow(hero, heroAsset, heroJoints)) {
                w.heroNoted = true;
                uint64_t parts[3]{};
                const int nParts = headParts(head, hero, parts, 3);
                std::snprintf(text, sizeof(text),
                              "first person: the hero's body is asset %016llX, %u joints, head parts: %d "
                              "(%016llX %016llX %016llX)",
                              static_cast<unsigned long long>(heroAsset), static_cast<unsigned>(heroJoints),
                              nParts, static_cast<unsigned long long>(parts[0]),
                              static_cast<unsigned long long>(parts[1]),
                              static_cast<unsigned long long>(parts[2]));
                say(text);
            }
        }
        // For the log, once each: the bodies in the scene that are not the hero's (what Peter inside the
        // apartment is made of, when he is not found by his head).
        for (uint32_t i = 0; i < cache.count && w.bodyNotedCount < 24; ++i) {
            if (!cache.generation[i] || cache.joints[i] < 100 || cache.object[i] == hero)
                continue;
            uint32_t flags{};
            if (!readU32(cache.object[i] + 0x5c, flags) || (flags & 0x220))
                continue;
            const uint32_t handle = handleOf(i);
            float other[16];
            if (!handle || !headWorld(cache.object[i], other, 0) || sameFrame(head, other, .02f, .97f))
                continue;
            // Named once, and once more when it is in the shot (what the camera shows in the hallway).
            const uint32_t key = handle * 2u + (haveCam && inShot(cam, other + 12) ? 1u : 0u);
            bool known = false;
            for (int k = 0; k < w.bodyNotedCount; ++k)
                known = known || w.bodyNoted[k] == key;
            if (known)
                continue;
            w.bodyNoted[w.bodyNotedCount++] = key;
            uint64_t parts[3]{};
            const int nParts = headParts(other, cache.object[i], parts, 3);
            std::snprintf(
                text, sizeof(text),
                "first person: another body is visible (asset %016llX, %u joints), shot: %d, head parts: %d "
                "(%016llX %016llX %016llX)",
                static_cast<unsigned long long>(cache.asset[i]), static_cast<unsigned>(cache.joints[i]),
                haveCam && inShot(cam, other + 12) ? 1 : 0, nParts, static_cast<unsigned long long>(parts[0]),
                static_cast<unsigned long long>(parts[1]), static_cast<unsigned long long>(parts[2]));
            say(text);
            break; // one line a pass
        }
        if (peterHere || bodyHere)
            w.peterSeen = now;
        // His body (the view reads it, as a twin) when it is the one in the shot; else his head.
        const bool viaBody = bodyHere && (!haveCam || peterFirst || (bodyIn && !heroIn));
        const bool viaHead = peterHere && (!haveCam || peterFirst || (peterIn && !heroIn));
        if (peterFirst && (viaBody || viaHead) && !w.peterFirstNoted) {
            w.peterFirstNoted = true;
            std::snprintf(text, sizeof(text),
                          "first person: the hero is not Spider-Man (body %016llX): Peter, where he is, is "
                          "the view and stays it",
                          static_cast<unsigned long long>(heroBodyAsset));
            say(text);
        }
        const bool toPeter = viaBody || viaHead;
        if (!toPeter)
            w.switchSince = 0;
        else if (!w.switchSince)
            w.switchSince = now;
        if (toPeter && (!haveCam || now - w.switchSince >= 200)) {
            if (const uint32_t handle = handleOf(viaBody ? bodyIndex : peterIndex)) {
                showAsked(); // the hero's parts hidden so far are not hidden now: he is in the scene, not its
                             // eyes
                w.otherPlays = true;
                w.switchSince = 0;
                w.selected = now;
                w.lastTwin = 0;
                w.twinTries = w.twinMisses = 0;
                w.standInHidden = false;
                if (viaBody) {
                    w.standInAsset = 0;
                    w.standInFlagged = false;
                    w.twinOnly = true;
                    standIn = 0;
                    twin = handle;
                    sourceKind = 4;
                    say("first person: Peter's body is in the shot and the hero is not: the view reads it");
                } else {
                    w.standInAsset = cache.asset[peterIndex];
                    w.standInFlagged = peterFlagged;
                    twin = 0;
                    standIn = handle;
                    sourceKind = 3;
                    say(peterFlagged ? "first person: Peter (his head flagged by the game) is in the shot "
                                       "and the hero is not: the view goes to Peter"
                                     : "first person: Peter is in the shot and the hero is not: the view "
                                       "goes to Peter");
                }
            }
        }
        if (w.otherPlays)
            return;
        sourceKind = 2;
        const int n = hideAtHead(head, hero, 0);
        uint64_t asset{};
        unsigned joints{};
        if (classifyNow(hero, asset, joints)) {
            // Spider-Man's own body: the mask and lens pieces spawned just below his pool entry. Another
            // character's (Miles, MJ): the same place, as his face is not at his head joint (head parts: 0 in
            // the log); what is hidden is named once.
            hideSuitPieces(hero);
            if (asset != suitBody && w.neighboursFor != hero) {
                w.neighboursFor = hero;
                uint32_t heroIndex = 0;
                bool have = false;
                for (uint32_t i = 0; i < cache.count && !have; ++i)
                    if (cache.generation[i] && cache.object[i] == hero) {
                        heroIndex = i;
                        have = true;
                    }
                char list[150];
                int used = 0, shown = 0;
                list[0] = 0;
                for (int64_t k = static_cast<int64_t>(heroIndex) - 8;
                     have && k < static_cast<int64_t>(heroIndex); ++k) {
                    if (k < 0 || !cache.generation[k] || cache.joints[k] < 2 || cache.joints[k] > 40 ||
                        cache.object[k] == hero || shown >= 3 || used > 110)
                        continue;
                    uint32_t flags{};
                    float at[16];
                    const bool read =
                        readU32(cache.object[k] + 0x5c, flags) && headWorld(cache.object[k], at, 0);
                    const float d = read ? std::sqrt((at[12] - head[12]) * (at[12] - head[12]) +
                                                     (at[13] - head[13]) * (at[13] - head[13]) +
                                                     (at[14] - head[14]) * (at[14] - head[14]))
                                         : -1.f;
                    used += std::snprintf(list + used, sizeof(list) - used,
                                          " [%016llX %u joints flags %08X %.2f m]",
                                          static_cast<unsigned long long>(cache.asset[k]),
                                          static_cast<unsigned>(cache.joints[k]), flags, d);
                    ++shown;
                }
                std::snprintf(text, sizeof(text),
                              "first person: hidden next to this hero's pool entry (%016llX):%s",
                              static_cast<unsigned long long>(asset), shown ? list : " nothing");
                say(text);
            }
        }
        static int logged = 0;
        if (n && logged < 6) {
            ++logged;
            std::snprintf(text, sizeof(text),
                          "first person: the hero's head parts hidden (%d objects at his head joint)", n);
            say(text);
        }
        return;
    }
    // Peter's double plays the scene while the hero is about (not hidden): the view goes back to the hero
    // when he is in the shot and Peter is not. Out of the shot, Peter stays the view too (a cut to whom he
    // talks to).
    if (w.otherPlays && !(heroFlags & 0x20)) {
        if (now - w.lastBack >= 100) {
            w.lastBack = now;
            // Peter's body is the view and is gone from the picture (parked or hidden: Aunt May's scene swaps
            // Spider-Man's masked body for the unmasked one): another drawn body of his that is in the shot
            // takes over. Identity of the skeleton and the shot, not distance.
            if (twin.load() && w.twinOnly) {
                const uint64_t current = objectOf(twin.load());
                uint64_t currentAsset{};
                unsigned currentJoints{};
                float retargetCam[16], currentHead[16];
                uint32_t currentFlags{};
                if (current && cameraNow(retargetCam) && classifyNow(current, currentAsset, currentJoints) &&
                    isPeterBody(currentAsset)) {
                    const bool currentShown = readU32(current + 0x5c, currentFlags) &&
                                              !(currentFlags & 0x20) && headWorld(current, currentHead, 0) &&
                                              inShot(retargetCam, currentHead + 12);
                    if (!currentShown) {
                        refreshCache();
                        for (uint32_t i = 0; i < cache.count; ++i) {
                            if (!cache.generation[i] || cache.joints[i] < 100 ||
                                !isPeterBody(cache.asset[i]) || cache.object[i] == current)
                                continue;
                            uint32_t flags{};
                            float at[16];
                            if (!readU32(cache.object[i] + 0x5c, flags) || (flags & 0x20) ||
                                !headWorld(cache.object[i], at, 0) || !inShot(retargetCam, at + 12))
                                continue;
                            if (const uint32_t handle = handleOf(i)) {
                                showAsked();
                                w.selected = now;
                                w.lastTwin = 0;
                                w.twinTries = w.twinMisses = 0;
                                twin = handle;
                                sourceKind = 4;
                                say("first person: Peter's body left the picture: the view reads his other "
                                    "body that is in the shot");
                                return;
                            }
                        }
                    }
                }
            }
            float cam[16], heroHead[16];
            const uint64_t source = twin.load() ? objectOf(twin.load()) : objectOf(standIn.load());
            bool back = false;
            uint64_t backAsset{};
            unsigned backJoints{};
            // Peter stays the view while the hero is another character's body (Miles, MJ): see peterFirst.
            const bool heroOther = classifyNow(hero, backAsset, backJoints) && backAsset != suitBody;
            if (!heroOther && cameraNow(cam) && headWorld(hero, heroHead, 0)) {
                float peterHead4[16];
                const bool peterIn =
                    source && headWorld(source, peterHead4, 0) && inShot(cam, peterHead4 + 12);
                back = inShot(cam, heroHead + 12) && !peterIn;
            }
            if (!back)
                w.switchSince = 0;
            else if (!w.switchSince)
                w.switchSince = now;
            if (back && now - w.switchSince >= 200) {
                showAsked(); // Peter's parts hidden so far come back: he is in the scene now, not its eyes
                w.otherPlays = false;
                w.switchSince = 0;
                w.standInHidden = false;
                w.standInAsset = 0;
                standIn = twin = 0;
                w.lastPass = 0;
                say("first person: the hero is in the shot and Peter is not: the view goes back to the hero");
                return;
            }
        }
    }
    // The hero is parked out of sight: a stand-in skeleton plays him.
    uint64_t standInObject = 0;
    if (const uint32_t h = standIn.load()) {
        standInObject = objectOf(h);
        uint64_t asset{};
        unsigned joints{};
        uint32_t flags{};
        if (!standInObject || !classifyNow(standInObject, asset, joints) || asset != w.standInAsset ||
            !readU32(standInObject + 0x5c, flags) || ((flags & 0x200) && !w.standInFlagged)) {
            say("first person: the stand-in was lost (freed, replaced or parked); its last head is held for "
                "a moment");
            lostMs = now;
            // The head object goes (parked while he bends over to pick something up, replaced), the body that
            // carries it does not: the view reads the body, as it did, until the head object is back.
            const uint64_t body = twin.load() ? objectOf(twin.load()) : 0;
            uint32_t bodyFlags{};
            float bodyHead[16];
            if (body && entityOk(body) && readU32(body + 0x5c, bodyFlags) && headWorld(body, bodyHead, 0)) {
                w.twinOnly = true;
                say("first person: its body is still there: the view reads the body");
            } else {
                twin = 0;
            }
            standIn = 0;
            w.twinTries = w.twinMisses = 0;
            w.standInHidden = false;
            standInObject = 0;
        }
    }
    if (!standInObject && w.twinOnly) {
        const uint64_t body = twin.load() ? objectOf(twin.load()) : 0;
        uint32_t bodyFlags{};
        float bodyHead[16];
        if (body && entityOk(body) && readU32(body + 0x5c, bodyFlags) && headWorld(body, bodyHead, 0)) {
            if (now - w.lastHide >= 500) {
                w.lastHide = now;
                refreshCache();
                hideAtHead(bodyHead, body, 0, true);
                {
                    uint64_t bodyAsset{};
                    unsigned bodyJoints{};
                    if (classifyNow(body, bodyAsset, bodyJoints) && bodyAsset == suitBody)
                        hideSuitPieces(body);
                }
                // The head object back at the body's head (not parked) is the stand-in again.
                for (uint32_t i = 0; i < cache.count; ++i) {
                    if (!cache.generation[i] || !cache.joints[i] || cache.asset[i] != w.standInAsset)
                        continue;
                    uint32_t flags{};
                    float other[16];
                    if (!readU32(cache.object[i] + 0x5c, flags) || (flags & 0x200) ||
                        !headWorld(cache.object[i], other, 0) || !sameFrame(bodyHead, other, .02f, .97f))
                        continue;
                    if (const uint32_t handle = handleOf(i)) {
                        standIn = handle;
                        w.twinOnly = false;
                        w.selected = now;
                        w.standInHidden = false;
                        say("first person: the head object is back: it is the stand-in again");
                        break;
                    }
                }
            }
            sourceKind = 4;
            return;
        }
        w.twinOnly = false; // the body went too
        twin = 0;
    }
    if (standInObject && w.standInAsset != peterHead && !w.twinOnly && now - w.lastSwap >= 100) {
        // Another character's head is the view (MJ's, a photo's) and Peter's is there, not flagged: Peter
        // first.
        w.lastSwap = now;
        refreshCache();
        uint32_t peter = 0;
        for (uint32_t i = 0; i < cache.count && !peter; ++i) {
            if (!cache.generation[i] || !cache.joints[i] || cache.asset[i] != peterHead)
                continue;
            uint32_t flags{};
            float other[16];
            if (!readU32(cache.object[i] + 0x5c, flags) || (flags & 0x220) ||
                !headWorld(cache.object[i], other, 0))
                continue;
            peter = handleOf(i);
        }
        if (peter) {
            showAsked(); // the parts hidden for the one we leave come back
            w.standInAsset = peterHead;
            w.standInFlagged = false;
            w.selected = now;
            w.lastTwin = 0;
            w.twinTries = w.twinMisses = 0;
            w.standInHidden = false;
            twin = 0;
            standIn = peter;
            sourceKind = 3;
            standInObject = objectOf(peter);
            say("first person: Peter is in the scene: the view moves from this character to him");
        }
    }
    if (standInObject && w.standInAsset == peterHead && now - w.lastSwap >= 100) {
        // Peter is the view, but he is out of the shot while another Peter is in it (the intro has a second
        // one, under the map, which can be the one found first): the view goes to the one in the shot. Nobody
        // in the shot: it stays.
        w.lastSwap = now;
        float cam[16], mine[16];
        const uint64_t current = twin.load() ? objectOf(twin.load()) : standInObject;
        uint32_t better = 0;
        if (cameraNow(cam) && current && headWorld(current, mine, 0) && !inShot(cam, mine + 12)) {
            refreshCache();
            for (uint32_t i = 0; i < cache.count && !better; ++i) {
                if (!cache.generation[i] || !cache.joints[i] || cache.asset[i] != peterHead ||
                    cache.object[i] == standInObject)
                    continue;
                uint32_t flags{};
                float other[16];
                if (!readU32(cache.object[i] + 0x5c, flags) || (flags & 0x220) ||
                    !headWorld(cache.object[i], other, 0) || sameFrame(mine, other, .02f, .97f) ||
                    !inShot(cam, other + 12))
                    continue;
                better = handleOf(i);
            }
        }
        if (!better)
            w.swapSince = 0;
        else if (!w.swapSince)
            w.swapSince = now;
        if (better && now - w.swapSince >= 200) {
            showAsked(); // the hidden parts of the one we leave come back
            w.swapSince = 0;
            w.standInFlagged = false;
            w.selected = now;
            w.lastTwin = 0;
            w.twinTries = w.twinMisses = 0;
            w.standInHidden = false;
            twin = 0;
            standIn = better;
            sourceKind = 3;
            standInObject = objectOf(better);
            say("first person: another Peter is in the shot and this one is not: the view moves to him");
        }
    }
    if (standInObject) {
        if (const uint32_t t = twin.load(); t && !objectOf(t)) {
            say("first person: the body twin was lost");
            twin = 0;
        }
        if (!twin.load() && w.twinTries < 12 && now - w.lastTwin >= 400) {
            w.lastTwin = now;
            ++w.twinTries;
            refreshCache();
            if (const uint32_t t = findTwin(standInObject)) {
                twin = t;
                sourceKind = 4;
                say("first person: the stand-in's body twin found; the view reads it");
            } else if (w.twinMisses < 3) {
                ++w.twinMisses;
                say("first person: no body twin with the stand-in's head joint yet");
            }
        }
        if (!w.standInHidden && (twin.load() || now - w.selected >= 400)) {
            w.standInHidden = true;
            hideOnce(standIn.load());
            say("first person: the stand-in's head is hidden");
        }
        if (w.standInHidden && now - w.lastHide >= 500) {
            w.lastHide = now;
            refreshCache();
            float head[16];
            const uint64_t source = twin.load() ? objectOf(twin.load()) : standInObject;
            if (source && headWorld(source, head, 0))
                hideAtHead(head, source, 0);
        }
        sourceKind = twin.load() ? 4 : 3;
        return;
    }
    // No stand-in yet: a visible head object of a playable character is the one that plays him.
    const ULONGLONG gap = w.quietSince && now - w.quietSince > 4000 ? 500 : 100;
    if (now - w.lastPass < gap)
        return;
    w.lastPass = now;
    refreshCache();
    // Peter's head over any other; among those, the one in the shot (a second Peter that is not in the
    // picture, one under the map, is not the scene's eyes); otherwise the first, as before.
    float pickCam[16];
    const bool pickHaveCam = cameraNow(pickCam);
    int pick = -1, pickScore = -1;
    for (uint32_t i = 0; i < cache.count; ++i) {
        if (!cache.generation[i] || !cache.joints[i] || !isHeadAsset(cache.asset[i]))
            continue;
        uint32_t flags{};
        if (!readU32(cache.object[i] + 0x5c, flags) || (flags & 0x220))
            continue;
        int score = cache.asset[i] == peterHead ? 2 : 0;
        float at[16];
        if (pickHaveCam && headWorld(cache.object[i], at, 0) && inShot(pickCam, at + 12))
            ++score;
        if (score > pickScore) {
            pick = static_cast<int>(i);
            pickScore = score;
        }
    }
    if (pick < 0) {
        // No head of a playable character to follow. Peter has become Spider-Man in the scene (the suit-up)
        // while the hero is parked out of sight: a skeleton with Spider-Man's own body asset that is not the
        // hero's entity, not hidden, plays him. Known by its asset (as Peter's body is), the one in the shot
        // if there are two.
        {
            int suit = -1;
            int suitScore = -1;
            const bool heroGone = (heroFlags & 0x20) != 0;
            for (uint32_t i = 0; i < cache.count; ++i) {
                if (!cache.generation[i] || cache.joints[i] < 100 || cache.object[i] == hero)
                    continue;
                uint32_t flags{};
                float at[16];
                if (!readU32(cache.object[i] + 0x5c, flags) || (flags & 0x20) ||
                    !headWorld(cache.object[i], at, 0))
                    continue;
                const bool in = pickHaveCam && inShot(pickCam, at + 12);
                // Spider-Man's own body; with the hero hidden (the player's entity parked): Peter's known
                // bodies (his apartment, civilian and City Hall bodies), and a body of the scene's own that
                // is not another named character's (MJ, Miles) and is large enough to be a player's (150
                // joints; the crowd's are 122 to 127), in the shot, with the most joints of those (the
                // suit-up scene brings in its own skeletons, a 212 and a 418 joint one, drawn in the shot
                // while the hero's entity is hidden).
                const uint64_t a2 = cache.asset[i];
                const bool known = a2 == suitBody ||
                                   (heroGone && (isPeterBody(a2) || a2 == 0x80C52396F2470510ULL ||
                                                 a2 == 0x8CDBDBE3D4F0C56FULL || a2 == 0x878B7EEABDC354A2ULL));
                if (!known)
                    continue;
                int score = 1000 + (in ? 500 : 0) + static_cast<int>(cache.joints[i]);
                if (score > suitScore) {
                    suit = static_cast<int>(i);
                    suitScore = score;
                }
            }
            if (suit >= 0) {
                if (const uint32_t handle = handleOf(static_cast<uint32_t>(suit))) {
                    showAsked();
                    w.quietSince = 0;
                    w.standInAsset = 0;
                    w.standInFlagged = false;
                    w.selected = now;
                    w.lastTwin = 0;
                    w.twinTries = w.twinMisses = 0;
                    w.standInHidden = false;
                    w.twinOnly = true;
                    standIn = 0;
                    twin = handle;
                    sourceKind = 4;
                    std::snprintf(text, sizeof(text),
                                  "first person: a body of Spider-Man or Peter (not the hero's entity) plays "
                                  "the scene: "
                                  "the view reads it (asset %016llX, %u joints, flags %08X)",
                                  static_cast<unsigned long long>(cache.asset[suit]),
                                  static_cast<unsigned>(cache.joints[suit]), heroFlags);
                    say(text);
                    return;
                }
            }
        }
        if (!w.quietSince)
            w.quietSince = now;
        if ((!w.quietNoted && now - w.quietSince >= 1000) ||
            (w.quietNoted && w.quietLogs < 3 && now - w.quietLogAt >= 5000)) {
            // For the log: what is in the scene when there is nobody to follow, at one second and twice more
            // at five-second intervals (who plays the suited Spider-Man may come into the pool later). Every
            // skeleton of 100 joints or more: first those drawn (not flagged hidden), then the hidden ones; a
            // character who plays the scene is among the first. Short lines (a log slot holds 199
            // characters): five entries a line, three lines at most a kind.
            w.quietNoted = true;
            w.quietLogAt = now;
            ++w.quietLogs;
            uint64_t heroAsset{};
            unsigned heroJoints{};
            if (!classifyNow(hero, heroAsset, heroJoints))
                heroAsset = heroJoints = 0;
            std::snprintf(text, sizeof(text),
                          "first person: nobody to follow (hero %016llX %u, flags %08X), look #%d",
                          static_cast<unsigned long long>(heroAsset), heroJoints, heroFlags, w.quietLogs);
            say(text);
            for (int pass = 0; pass < 2; ++pass) {
                int lines = 0, onLine = 0, total = 0;
                int used = 0;
                for (uint32_t i = 0; i < cache.count; ++i) {
                    if (!cache.generation[i] || cache.joints[i] < 100 || cache.object[i] == hero)
                        continue;
                    uint32_t flags{};
                    float at[16];
                    if (!readU32(cache.object[i] + 0x5c, flags) || !headWorld(cache.object[i], at, 0) ||
                        ((flags & 0x20) != 0) != (pass == 1))
                        continue;
                    ++total;
                    if (lines >= 3)
                        continue;
                    if (!onLine)
                        used = std::snprintf(text, sizeof(text), "fp %s:", pass ? "HIDDEN" : "DRAWN");
                    used += std::snprintf(text + used, sizeof(text) - used, " %016llX/%u/%04X/%d",
                                          static_cast<unsigned long long>(cache.asset[i]),
                                          static_cast<unsigned>(cache.joints[i]), flags & 0xffff,
                                          pickHaveCam && inShot(pickCam, at + 12) ? 1 : 0);
                    if (++onLine == 5) {
                        say(text);
                        onLine = 0;
                        ++lines;
                    }
                }
                if (onLine) {
                    say(text);
                    ++lines;
                }
                if (!total) {
                    std::snprintf(text, sizeof(text), "fp %s: none", pass ? "HIDDEN" : "DRAWN");
                    say(text);
                }
            }
            say("fp key: asset/joints/flags/in the shot (1 or 0)");
        }
        // Peter has been gone from the scene for a while: the hero plays it (or nobody does) again.
        if (w.otherPlays && now - w.quietSince > 1500)
            w.otherPlays = false;
        sourceKind = lostMs.load() && now - lostMs.load() < 600 ? 5 : 1;
        return;
    }
    w.quietSince = 0;
    w.quietNoted = false;
    w.quietLogs = 0;
    const uint32_t handle = handleOf(static_cast<uint32_t>(pick));
    if (!handle)
        return;
    w.standInAsset = cache.asset[pick];
    w.standInFlagged = false;
    w.selected = now;
    w.lastTwin = 0;
    w.twinTries = w.twinMisses = 0;
    w.standInHidden = false;
    twin = 0;
    standIn = handle;
    sourceKind = 3;
    std::snprintf(text, sizeof(text),
                  "first person: stand-in %08X (asset %016llX) plays the player's character", handle,
                  static_cast<unsigned long long>(w.standInAsset));
    say(text);
}
DWORD WINAPI work(void*) {
    bool was = false;
    for (;;) {
        Sleep(20); // a cut to the next scene finds its character within a few frames
        const bool now = wanted.load();
        // The scene's own camera is the view (no theatre screen, no first person): no character is looked
        // for, and what was hidden for one is shown.
        const bool cameraOnly = now && cameraMode.load() == 2;
        if (!now || cameraOnly) {
            if (was) {
                was = false;
                forgetAll();
            }
            if (cameraOnly)
                sourceKind = 6;
            continue;
        }
        was = true;
        __try {
            tick();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            sourceKind = 1;
        }
    }
    return 0;
}

// --- the head, as the render thread samples it each frame
float lastHead[16];
bool haveHead{};
// The head joint's own axes are the rig's, not the world's: which of them is the character's forward and
// which is up is read from the rest pose by the body (setHeadAxes), as the vectors in the joint's frame.
// forward, up, then the eyes' place from the joint in (forward, up, left) metres. Constant per rig: plain
// atomics.
std::atomic<float> headAxes[9];
std::atomic<bool> headAxesSet{};
// A cut: the view's source changed or the head jumped (a new shot): counted by frame(), read by frameLevel().
std::atomic<uint32_t> cutCount{};
// The heading of the character's head (level: its forward without height, or its crown's when it looks
// steeply up or down) and of frameLevel's frame, world yaw as the rig has it (0 facing -z), for the play that
// follows the scene.
std::atomic<float> headYaw{}, levelYaw{};
std::atomic<bool> headYawKnown{}, levelYawKnown{};
uint64_t prevSource{};
float prevPos[3]{};
bool prevSet{};
// frameLevel's heading, latched at the start of a scene and at each cut (the render thread).
float levelAxes[16]{};
bool levelSet{};
uint32_t levelCut{};
} // namespace

void setHeadAxes(Vec3 forward, Vec3 up, Vec3 eyes) {
    const float v[9] = {forward.x, forward.y, forward.z, up.x, up.y, up.z, eyes.x, eyes.y, eyes.z};
    for (int i = 0; i < 9; ++i)
        if (!std::isfinite(v[i]))
            return;
    for (int i = 0; i < 9; ++i)
        headAxes[i].store(v[i], std::memory_order_relaxed);
    headAxesSet.store(true, std::memory_order_release);
}

bool start(uintptr_t exeBase) {
    if (exe)
        return true;
    exe = exeBase;
    getJoint = reinterpret_cast<GetJoint>(exe + getJointRva);
    hideEntity = reinterpret_cast<Visibility>(exe + hideRva);
    showEntity = reinterpret_cast<Visibility>(exe + showRva);
    return true;
}
void want(bool on) {
    // The worker starts with the first scene that wants first person (the XR worker's thread, as start()).
    if (on && !workerThread && exe)
        workerThread = CreateThread(nullptr, 0, work, nullptr, 0, nullptr);
    if (on == wanted.load())
        return;
    wanted = on;
    if (!on) {
        released = true;
        viewOnCamera = false;
    }
}
void sceneCamera(int mode) {
    cameraMode = mode < 0 ? 0 : mode > 2 ? 2 : mode;
}
void cycleCharacter() {
    swapRequests.fetch_add(1, std::memory_order_relaxed);
}
int alternatives() {
    return wanted.load() ? altCount.load() : 0;
}
int chosenCharacter() {
    return choiceShown.load();
}
bool onCamera() {
    return wanted.load() && viewOnCamera.load();
}
void hero(uintptr_t instance) {
    heroObject = instance;
    // Requests from the worker are carried out here, on the game's own thread.
    drain();
    if (released.exchange(false) && !wanted.load()) {
        shrinkObject[0] = shrinkObject[1] = 0;
        showAll();
        haveHead = false;
        sourceKind = 0;
    }
    // While wanted the head is sampled (and what is kept hidden hidden) every frame, whether or not an eye
    // command for the scene has arrived yet: that is what tells the XR worker a scene can be shown this way.
    if (wanted.load()) {
        Mat4 sampled{};
        frame(sampled);
    }
}
void camera(const Mat4& pose, float tangentHalfWidth, float tangentHalfHeight) {
    camHalfWidth.store(std::fabs(tangentHalfWidth), std::memory_order_relaxed);
    camHalfHeight.store(std::fabs(tangentHalfHeight), std::memory_order_relaxed);
    for (int i = 0; i < 16; ++i)
        camPose[i].store(pose[i], std::memory_order_relaxed);
    camMs.store(GetTickCount64(), std::memory_order_release);
}
// The scene's own camera as the view: its place, on a level frame facing where it faces (looking steeply up
// or down, where its crown points, as frameLevel does), so that a camera that tilts or rolls does not tilt or
// roll the viewer. A cut (the camera far from where it was, or turned far in a frame) is counted as the
// head's cuts are, for frameLevel.
bool cameraFrame(Mat4& head) {
    float cam[16];
    if (!cameraNow(cam)) {
        ++nMisses;
        return false;
    }
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(cam[i])) {
            ++nMisses;
            return false;
        }
    float ax = cam[8], az = cam[10];
    if (ax * ax + az * az < .09f) {
        const float s = cam[9] < 0 ? -1.f : 1.f;
        ax = s * cam[4];
        az = s * cam[6];
    }
    const float l = std::sqrt(ax * ax + az * az);
    if (l < 1e-3f) {
        ++nMisses;
        return false;
    }
    ax /= l;
    az /= l;
    static float prevAx{}, prevAz{};
    {
        const float dx = cam[12] - prevPos[0], dy = cam[13] - prevPos[1], dz = cam[14] - prevPos[2];
        if (prevSet &&
            (prevSource != 1 || dx * dx + dy * dy + dz * dz > 1.f || ax * prevAx + az * prevAz < .5f))
            ++cutCount;
        prevSource = 1; // not an entity's address: the camera
        prevPos[0] = cam[12];
        prevPos[1] = cam[13];
        prevPos[2] = cam[14];
        prevSet = true;
        prevAx = ax;
        prevAz = az;
    }
    // The viewer stands a little in front of the camera's lens (along where it looks), so that a shot seen in
    // the headset is not as far away as the director's camera is: the view's place, the picture's direction
    // and field stay the camera's.
    constexpr float closer = .6f;
    float fx = cam[8], fy = cam[9],
          fz = cam[10]; // the game's camera looks along its +z column (inShot's "ahead")
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fl > 1e-3f) {
        fx *= closer / fl;
        fy *= closer / fl;
        fz *= closer / fl;
    } else
        fx = fy = fz = 0;
    const float level[16] = {-az,          0, ax, 0, 0, -1, 0, 0, ax, 0, az, 0, cam[12] + fx, cam[13] + fy,
                             cam[14] + fz, 1};
    headYaw.store(std::atan2(-ax, -az), std::memory_order_relaxed);
    headYawKnown.store(true, std::memory_order_relaxed);
    const ULONGLONG now = GetTickCount64();
    lastCameraMs = now;
    viewOnCamera = true;
    ++nSamples;
    std::memcpy(head.data(), level, sizeof(level));
    return true;
}
bool frame(Mat4& head) {
    if (!wanted.load()) {
        prevSet = levelSet = false;
        return false;
    }
    if (cameraMode.load(std::memory_order_relaxed) == 2) {
        // The scene's own camera is the view: nothing is hidden or shrunk, no head is looked for.
        shrinkObject[0] = shrinkObject[1] = 0;
        return cameraFrame(head);
    }
    keepHidden();
    uint64_t source = 0;
    const uint32_t kind = sourceKind.load();
    if (kind == 2)
        source = heroObject.load();
    else if (kind == 3 || kind == 4) {
        if (kind == 4)
            source = objectOf(twin.load());
        if (!source)
            source = objectOf(standIn.load());
    }
    // The pose job shrinks the head joint of these skeletons (shrinkHead).
    {
        uintptr_t body = 0, headObject = 0;
        if (kind == 2)
            body = static_cast<uintptr_t>(heroObject.load());
        else if (kind == 3 || kind == 4) {
            headObject = static_cast<uintptr_t>(objectOf(standIn.load()));
            body = kind == 4 ? static_cast<uintptr_t>(source) : headObject;
        }
        shrinkObject[0] = body;
        shrinkObject[1] = headObject;
    }
    float world[16];
    bool ok = source && entityOk(source) && headAxesSet.load(std::memory_order_acquire) &&
              headWorld(source, world, 0);
    if (ok) {
        // The joint's rows are its own axes: normalised (a shrunk head's are a thousandth long). The
        // character's forward and up are given in them (setHeadAxes); the view's rows are right, down,
        // forward.
        float joint[3][3];
        for (int r = 0; r < 3 && ok; ++r) {
            const float l = std::sqrt(world[r * 4] * world[r * 4] + world[r * 4 + 1] * world[r * 4 + 1] +
                                      world[r * 4 + 2] * world[r * 4 + 2]);
            if (l < 1e-6f)
                ok = false;
            else
                for (int c = 0; c < 3; ++c)
                    joint[r][c] = world[r * 4 + c] / l;
        }
        if (ok) {
            float f[3]{}, u[3]{};
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r) {
                    f[c] += headAxes[r].load(std::memory_order_relaxed) * joint[r][c];
                    u[c] += headAxes[3 + r].load(std::memory_order_relaxed) * joint[r][c];
                }
            auto norm = [](float* v) {
                const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (l < 1e-6f)
                    return false;
                for (int c = 0; c < 3; ++c)
                    v[c] /= l;
                return true;
            };
            auto crossOf = [](const float* a, const float* b, float* o) {
                o[0] = a[1] * b[2] - a[2] * b[1];
                o[1] = a[2] * b[0] - a[0] * b[2];
                o[2] = a[0] * b[1] - a[1] * b[0];
            };
            float left[3];
            ok = norm(f) && norm(u);
            if (ok) {
                crossOf(u, f, left); // left = up x forward, as the body's rest pose defines them
                ok = norm(left);
            }
            if (ok) {
                crossOf(f, left, u); // up again, square to both
                const float x = headAxes[6].load(std::memory_order_relaxed);
                const float y = headAxes[7].load(std::memory_order_relaxed);
                const float z = headAxes[8].load(std::memory_order_relaxed);
                float eye[3];
                for (int c = 0; c < 3; ++c)
                    eye[c] = world[12 + c] + f[c] * x + u[c] * y + left[c] * z;
                for (int c = 0; c < 3; ++c) {
                    world[c] = -left[c];    // right
                    world[4 + c] = -u[c];   // down
                    world[8 + c] = f[c];    // forward
                    world[12 + c] = eye[c]; // between the eyes
                }
                world[3] = world[7] = world[11] = 0;
                world[15] = 1;
            }
        }
    }
    const ULONGLONG now = GetTickCount64();
    if (ok) {
        // A cut: another skeleton plays the view, or the head is somewhere else (more than a metre in a
        // frame).
        {
            const float dx = world[12] - prevPos[0], dy = world[13] - prevPos[1], dz = world[14] - prevPos[2];
            if (prevSet && (static_cast<uint64_t>(source) != prevSource || dx * dx + dy * dy + dz * dz > 1.f))
                ++cutCount;
            prevSource = static_cast<uint64_t>(source);
            prevPos[0] = world[12];
            prevPos[1] = world[13];
            prevPos[2] = world[14];
            prevSet = true;
        }
        std::memcpy(lastHead, world, sizeof(world));
        haveHead = true;
        lastSampleMs = now;
        ++nSamples;
        {
            // For the log, every 2.5 s: where the eyes are, and where the scene's camera is (a view that sits
            // on the camera is not a head that moves with the character).
            static ULONGLONG lastView{};
            if (now - lastView >= 2500) {
                lastView = now;
                float cam[16];
                const bool haveCam = cameraNow(cam);
                char t[200];
                std::snprintf(t, sizeof(t),
                              "first person: view: source %d, eyes (%.2f %.2f %.2f), camera %s (%.2f %.2f "
                              "%.2f), apart %.2f m, cuts %u",
                              static_cast<int>(sourceKind.load()), world[12], world[13], world[14],
                              haveCam ? "at" : "none", haveCam ? cam[12] : 0.f, haveCam ? cam[13] : 0.f,
                              haveCam ? cam[14] : 0.f,
                              haveCam ? std::sqrt((cam[12] - world[12]) * (cam[12] - world[12]) +
                                                  (cam[13] - world[13]) * (cam[13] - world[13]) +
                                                  (cam[14] - world[14]) * (cam[14] - world[14]))
                                      : 0.f,
                              static_cast<unsigned>(cutCount));
                say(t);
            }
        }
    } else if (haveHead && lastSampleMs.load() && now - lastSampleMs.load() < holdMs) {
        // The source has just gone (a cut, a freed stand-in): no jump to the game's camera, the last head
        // holds.
        std::memcpy(world, lastHead, sizeof(world));
        ++nMisses;
    } else {
        // No head, and none held: the scene's own camera, when that is what the player asked for.
        if (cameraMode.load(std::memory_order_relaxed) == 1 && cameraFrame(head))
            return true;
        ++nMisses;
        return false;
    }
    viewOnCamera = false;
    {
        float ax = world[8], az = world[10];
        if (ax * ax + az * az < .09f) {
            const float s = world[9] < 0 ? -1.f : 1.f;
            ax = s * world[4];
            az = s * world[6];
        }
        if (ax * ax + az * az > 1e-6f) {
            headYaw.store(std::atan2(-ax, -az), std::memory_order_relaxed);
            headYawKnown.store(true, std::memory_order_relaxed);
        }
    }
    std::memcpy(head.data(), world, sizeof(world));
    return true;
}
bool sceneYaw(bool level, float& yaw) {
    if (level ? !levelYawKnown.load(std::memory_order_relaxed)
              : !headYawKnown.load(std::memory_order_relaxed))
        return false;
    yaw = (level ? levelYaw : headYaw).load(std::memory_order_relaxed);
    return std::isfinite(yaw);
}
bool frameLevel(Mat4& out) {
    Mat4 head{};
    if (!frame(head))
        return false;
    const uint32_t cut = cutCount.load();
    if (!levelSet || cut != levelCut) {
        // Where the character faces now, level: the head's forward without its height, or, looking steeply up
        // or down, where its crown points (ahead looking down, behind looking up).
        float ax = head[8], az = head[10];
        if (ax * ax + az * az < .09f) {
            const float s = head[9] < 0 ? -1.f : 1.f;
            ax = s * head[4];
            az = s * head[6];
        }
        const float l = std::sqrt(ax * ax + az * az);
        if (l > 1e-3f) {
            ax /= l;
            az /= l;
            const float level[16] = {-az, 0, ax, 0, 0, -1, 0, 0, ax, 0, az, 0, 0, 0, 0, 1};
            std::memcpy(levelAxes, level, sizeof(level));
            levelSet = true;
            levelCut = cut;
        }
    }
    if (!levelSet) {
        out = head; // straight up or down from the start: the head as it is until it faces somewhere
        return true;
    }
    levelYaw.store(std::atan2(-levelAxes[8], -levelAxes[10]), std::memory_order_relaxed);
    levelYawKnown.store(true, std::memory_order_relaxed);
    std::memcpy(out.data(), levelAxes, sizeof(levelAxes));
    out[12] = head[12];
    out[13] = head[13];
    out[14] = head[14];
    return true;
}
namespace {
bool shrinkJoints(float* joints, uintptr_t rig, uintptr_t a, uintptr_t b) {
    __try {
        uint64_t mine{};
        const uint64_t array = reinterpret_cast<uint64_t>(joints);
        const bool onA = a && readU64(a + 0xd8, mine) && mine == array;
        const bool onB = b && readU64(b + 0xd8, mine) && mine == array;
        if ((!onA && !onB) || !okPointer(rig)) {
            shrinkOffTarget.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const unsigned n = *reinterpret_cast<uint16_t*>(rig + 2);
        const uint64_t table = *reinterpret_cast<uint64_t*>(rig + 8);
        if (!n || n > 1024 || !okPointer(table))
            return false;
        // Joints 16 bytes each: the parent (s16) at +0, the name's hash at +8; a parent comes before its
        // children.
        bool below[1024];
        int head = -1;
        for (unsigned j = 0; j < n; ++j) {
            const int parent = *reinterpret_cast<int16_t*>(table + j * 16);
            below[j] = false;
            if (*reinterpret_cast<uint32_t*>(table + j * 16 + 8) == headHash && head < 0) {
                head = static_cast<int>(j);
                below[j] = true;
            } else if (parent >= 0 && parent < static_cast<int>(j) && below[parent])
                below[j] = true;
        }
        if (head < 0) {
            shrinkNoHead.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const float* at = joints + head * 16 + 12;
        const float pivot[3] = {at[0], at[1], at[2]};
        if (!std::isfinite(pivot[0] + pivot[1] + pivot[2]))
            return false;
        for (unsigned j = 0; j < n; ++j) {
            if (!below[j])
                continue;
            float* m = joints + j * 16;
            for (int row = 0; row < 3; ++row)
                for (int i = 0; i < 3; ++i)
                    m[row * 4 + i] *= .001f;
            for (int i = 0; i < 3; ++i)
                m[12 + i] = pivot[i] + (m[12 + i] - pivot[i]) * .001f;
        }
        shrinkDone.fetch_add(1, std::memory_order_relaxed);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
} // namespace
bool shrinkHead(float* joints, uintptr_t rig) {
    if (!wanted.load(std::memory_order_relaxed))
        return false;
    const uintptr_t a = shrinkObject[0].load(std::memory_order_relaxed),
                    b = shrinkObject[1].load(std::memory_order_relaxed);
    return (a || b) && joints && shrinkJoints(joints, rig, a, b);
}
bool available() {
    if (!wanted.load())
        return false;
    const ULONGLONG now = GetTickCount64();
    const int mode = cameraMode.load();
    const ULONGLONG t = lastSampleMs.load(), c = lastCameraMs.load();
    if (mode != 2 && t && now - t < holdMs)
        return true;
    return mode != 0 && c && now - c < holdMs;
}
bool note(uint32_t& seen, char* out, size_t size) {
    const uint32_t serial = noteSerial.load(std::memory_order_acquire);
    if (serial == seen || !out || !size)
        return false;
    // Further behind than the ring holds (a reader that was away): skip to the oldest line still there.
    if (serial - seen > noteSlots)
        seen = serial - noteSlots;
    const uint32_t written = noteSeq[seen % noteSlots].load(std::memory_order_acquire);
    if (written != seen + 1) {
        if (written > seen + 1) // overwritten by a later line: that one is gone
            ++seen;
        return false; // not written yet: the next call
    }
    std::snprintf(out, size, "%s", noteText[seen % noteSlots]);
    ++seen;
    return true;
}
Status status() {
    Status s;
    s.source = wanted.load() ? sourceKind.load() : 0;
    s.hidden = nHidden.load();
    s.samples = nSamples.load();
    s.misses = nMisses.load();
    s.passes = nPasses.load();
    s.passMs = passMicros.load() / 1000.f;
    s.vqCalls = static_cast<uint64_t>(vqCalls);
    return s;
}
void release() {
    released = true;
    wanted = false;
    hero(heroObject.load());
}
} // namespace spidy::game_fp
