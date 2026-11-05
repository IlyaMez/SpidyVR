#pragma once
#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

// The game's own web lines. Hero::HeroRopeManager (vtable 38b3df8) owns 16
// RopeManager slots; swing states create type-2 ropes with EnsureRope 677d20
// and aim them with SetRopeTargetPosition 67d7c0. RopeManager::Update 676dd0
// starts each rope at per-hand joint positions; Spidy substitutes the tracked
// wrists there while immersive VR owns the webs.
namespace spidy::native_webs {
struct Hand {
    // 1: a web from the wrist to `anchor`. 2: the web of attachment
    // `attachedAt` was let go of and dissolves, its far end staying on
    // `anchor`, a target it threw or let go of: ReleaseRope (67b610) keeps the
    // handle when asked, and a released rope's update still reads its target
    // position (6786c0 returns +67c, which SetRopeTargetPosition writes).
    // Until October 6 a let-go web's end stayed where the target had been.
    // The two ropes are not the two hands' own: the caller says which web
    // each draws and from which wrist (WebRopes). A rope the game makes
    // sounds as a web shot, so a hand that takes hold of the other hand's web
    // gets none.
    uint32_t attached{}, tracked{};
    int64_t attachedAt{}; // one attachment; a new value shoots a new web
    Vec3 anchor{}, wrist{};
};
// One web in both hands (WebRopes::upper): the rope `rope` names leaves the
// wrist of the hand that holds the web nearer its end (its Hand's), and comes
// to that wrist from the other hand's, `wrist`, hanging as `slack` metres more
// rope than the straight line between the two would.
struct Held {
    uint32_t rope{}; // 1 + the index in Request::hands of that rope; 0: none
    float slack{};
    Vec3 wrist{};
};
// The edges a web bends round (the swing's physical webs), the one nearest
// the hand first: the game's rope goes round them to the web's end, its
// Hand's anchor.
constexpr int maxRopeBends = 6;
struct Bent {
    uint32_t count{};
    Vec3 points[maxRopeBends]{};
};
// Wrists are world positions placed from `feet`. The game thread moves them
// with the player to the simulation frame it is updating.
struct Request {
    Vec3 feet{};
    Hand hands[2]{};
    Held held{};
};
// The points the game's rope takes from the lower hand's wrist `from` to the
// upper hand's `to` (both included), `count` of them, `slack` metres longer
// than the straight line and bowing off it as a rope does: as far as a
// parabola of that length bows (s = c + 8 d^2 / 3c), at most half a metre,
// downward square to the line - sideways (world x) as the line stands
// straight up - and with the hands together, half its length straight down.
inline void heldPoints(Vec3 from, Vec3 to, float slack, int count, Vec3* out) {
    if (count < 2 || !out)
        return;
    const Vec3 chord = to - from;
    const float c = length(chord), extra = std::isfinite(slack) ? std::max(slack, 0.f) : 0.f;
    const float sag = std::min(.5f, c > .01f ? std::sqrt(3 * c * extra / 8) : extra / 2);
    const Vec3 down{0, -1, 0};
    Vec3 bow = down;
    if (c > .01f) {
        const Vec3 along = chord / c, side{1, 0, 0};
        bow = (down - along * dot(down, along)) + (side - along * dot(side, along)) * .05f;
        bow = length(bow) > 1e-4f ? normalized(bow) : down;
    }
    for (int i = 0; i < count; ++i) {
        const float s = static_cast<float>(i) / static_cast<float>(count - 1);
        out[i] = from + chord * s + bow * (4 * sag * s * (1 - s));
    }
}
// The points Spidy lays at the start of one of the game's ropes, `capacity`
// at most: from the wrist `start` - or for a web in both hands from the lower
// wrist `lower`, bowing `slack` up to `start` (heldPoints) - then round each
// edge it bends round. The game draws on from the rope's Start to its end, so
// the last of them is made its Start while the game lays the rope. Returns how
// many, and in `upperAt` where `start` is among them; 0 when there is nothing
// to lay but `start`.
inline int ropePoints(Vec3 start, const Vec3* lower, float slack, const Bent& bent, int capacity, Vec3* out,
                      int* upperAt = nullptr) {
    const int edges = std::clamp(static_cast<int>(bent.count), 0, maxRopeBends);
    if (capacity < 2 || !out || (!lower && !edges))
        return 0;
    int n = 1;
    if (lower) {
        // The bow takes what the edges leave it, three points at least.
        n = std::min(capacity, std::max(3, capacity - edges));
        heldPoints(*lower, start, slack, n, out);
    }
    out[n - 1] = start;
    if (upperAt)
        *upperAt = n - 1;
    for (int k = 0; k < edges && n < capacity; ++k)
        out[n++] = bent.points[k];
    return n;
}
enum State : uint32_t { off = 0, waiting = 1, active = 2, failed = 3 };
struct Status {
    uint32_t state{}, live{}; // live: bit per hand with a game rope
    uint64_t creates{}, releases{}, failures{}, updates{};
    // Distance from each owned rope's first point (rope +1c) to the tracked
    // start Spidy wrote, last update and maximum, in metres; -1 before any.
    // The game's hand-held tail kept this near 0.45 m until October 5.
    float startError = -1, startErrorMax = -1;
};
uint32_t start(uintptr_t base, uintptr_t record);
// The local player became another actor (a loaded save, a respawn, a
// character switch); zero until there is one again.
void retarget(uintptr_t record);
// `bends`: the edges each of the request's two webs bends round (Bent, one
// per Request::hands entry); none when null.
void submit(const Request&, uint32_t leaseMs, const Bent* bends = nullptr);
Status status();
// The web tie's webs (web_tie.hpp), in the game's own rope material: from
// where each sticks to a surface, `anchor`, to the enemy it holds, `end`.
// The hero rope manager starts a rope at one of two hand points (Start, Grip,
// Grip2, End at 7d60.. +12 per hand), so a tie's rope is made on a hand whose
// own web is not drawn now, and that hand's points are the anchor for the
// update. While both hands have webs of their own, a tie's rope is let go and
// the overlay draws it; it is made again once a hand is free.
constexpr unsigned maxTieRopes = 2;
struct TieRope {
    Vec3 anchor{}, end{};
};
void submitTies(const TieRope* ropes, uint32_t count, uint32_t leaseMs);
// A bit per tie (in submitTies' order) the game draws now.
uint32_t tiesDrawn();
// Web lines (web_line.hpp), in the game's own rope material: from `start`
// to `end`, both on surfaces, each rope kept for its line (`id`). The hero
// rope manager starts a rope at one of two hand points (Start, Grip, Grip2,
// End at 7d60.. +12 per hand), and a line lasts while the player uses both
// hands. So the update's ropes are laid with their hand's points set for
// each in turn: before the update for the first rope on a hand, and in the
// hand points the game asks for each rope (95f9c0) for the next one there,
// in slot order. Whether the game draws a line from its start, not from the
// hand, is seen after each update (its rope's third point within
// lineRopeTolerance of the line, once lineRopeSettle updates old); one drawn
// from the hand lineRopeBadRun updates in a row is let go and made again
// once a hand is free, the overlay drawing it meanwhile. Until November 3 a
// hand's Start was set in the hand points for the rope being laid: 679dc0
// reads it before, and lines came from the player's hand.
// The lines' ropes and the strands of the thugs hung from them, ten at most
// (a hero rope manager has 16 ropes, the hands', their trailing ones and the
// ties' among them); past that the overlay draws a strand. A line's rope with
// its strands laid in it (November 3, 19:00) crashed the game past 8 points
// and drew the line twice (November 4).
constexpr unsigned maxLineRopes = 10;
constexpr float lineRopeTolerance = .3f;
constexpr uint32_t lineRopeSettle = 30, lineRopeBadRun = 6;
// A rope may carry two strands when ropes run short: laid from the first's
// head up to its top (via), along the line to the second's top (start), and
// down to its head (end) - three points laid, well within a rope's 8.
struct LineRope {
    Vec3 start{}, end{};
    uint32_t id{};
    uint32_t viaCount{};
    Vec3 via[2]{};
};
void submitLines(const LineRope* ropes, uint32_t count, uint32_t leaseMs);
// A bit for each line (in submitLines' order) the game draws now.
uint32_t linesDrawn();
// The line ropes' telemetry (export SpidyLineRopeData): ropes made and let
// go, those let go because they were drawn from the hand (hidden), the
// updates a line's rope was checked in, and in how many it lay on the line
// (good) or off it (bad), of the bad those while its hand's own web or a
// tie's was drawn too (shared); the hand points set for a rope (moves); the
// last and worst distance off the line.
// Version 2 (November 3): hidden; ropes kept by line, checked by their third point.
struct LineRopeData {
    uint32_t magic = 0x534c5250, version = 2, bytes = sizeof(LineRopeData), status{};
    int64_t sequence{};
    uint64_t made{}, released{}, hidden{}, checks{}, good{}, bad{}, moves{}, shared{};
    float lastOff{}, worstOff{};
};
static_assert(sizeof(LineRopeData) == 96);
// True while the game draws this hand's web, so the overlay must not.
bool drawing(unsigned hand);
// The game draws a web held in both hands from wrist to wrist (Request::held):
// the overlay draws none of it.
bool drawsHeld();
// Hero position the newest hero rope update read, and how many updates have
// read one. Ropes start from it; native eyes place the same frame from it.
bool heroSample(uint64_t& samples, Vec3& position);
// First point of each hand's game rope as built by the update that took hero
// sample `sample`. Returns a bit per hand with a rope; 0 for any other sample.
uint32_t ropeStarts(uint64_t sample, Vec3 starts[2]);
// Tube and end-cone instances of the hero's ropes. They are never avatar.
bool webInstance(uintptr_t instance);
uint32_t stop();
// Wire formats of the SpidyWebsStart and SpidyWebsSubmit exports, which let a
// headless probe drive the game's web lines without the OpenXR worker.
struct ProbeConfig {
    uint32_t magic = 0x53574243, version = 1, bytes = sizeof(ProbeConfig), pid{};
    uint64_t base{}, record{};
};
struct ProbeCommand {
    uint32_t magic = 0x5357424d, version = 3, bytes = sizeof(ProbeCommand), leaseMs{};
    Request request{};
    // Version 3: the edges each of the request's two webs bends round (Bent).
    // Version 2 (probeCommandBytesV2) ends before them.
    Bent bends[2]{};
};
constexpr uint32_t probeCommandBytesV2 = 136;
static_assert(sizeof(Hand) == 40 && sizeof(Held) == 20 && sizeof(Request) == 120 && sizeof(ProbeConfig) == 32 &&
              sizeof(Bent) == 76 && sizeof(ProbeCommand) == 288);
} // namespace spidy::native_webs
