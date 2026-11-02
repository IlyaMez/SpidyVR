#pragma once
#include <algorithm>
#include <cstdint>

// The game's frames paced by the headset's (stereo_probe.cpp: paceFrame).
//
// The XR worker sends the eyes a command once a headset frame, as soon as the
// runtime has let that frame begin (SpidySetEyes), and shows whichever pair the
// game finished last. The game drew as fast as it could: 78 to 157 pairs a
// second against the headset's 90 on October 30. Pairs the headset never
// showed cost the card and the game's threads, and the newest pair was a
// different time old at each headset frame, so the city moved by uneven steps
// in a swing (players saw judder at well over 90 frames a second). While the
// eyes render in place of the game's view, each game frame takes its head from
// a command newer than the last frame's, waiting for the next one if none has
// come: one pair a headset frame, each begun at the same point of it. A game
// slower than the headset finds a newer command waiting and does not wait.
namespace spidy::native_pace {
// The headset's frame period in QPC ticks, from the gaps between commands: a
// running average that leaves out gaps over 50 ms (menus, a runtime that
// stopped sending frames).
inline double period(double average, int64_t gap, int64_t frequency) {
    if (gap <= 0 || frequency <= 0 || gap > frequency / 20)
        return average;
    return average > 0 ? average + (static_cast<double>(gap) - average) / 16 : static_cast<double>(gap);
}
// The longest a frame waits for the next command, ms: a period and a half,
// 4 to 20 ms (20 before there is a period), so a worker that stops sending
// never holds the game under 50 frames a second.
inline uint32_t waitMs(double periodTicks, int64_t frequency) {
    if (!(periodTicks > 0) || frequency <= 0)
        return 20;
    const double ms = periodTicks * 1500.0 / static_cast<double>(frequency);
    return static_cast<uint32_t>(std::clamp(ms, 4.0, 20.0) + .5);
}
// SpidyPaceData: what pacing did, for the session report.
struct Data {
    // on: pacing is wanted (SpidyEyePacing).
    uint32_t magic = 0x50414345, version = 1, bytes = sizeof(Data), on{};
    int64_t sequence{};
    // Frames that took their head while paced, those that waited for a newer
    // command and those that gave up waiting; the time waited, microseconds,
    // in all and the longest; the headset's period as measured, microseconds.
    uint64_t frames{}, waits{}, timeouts{}, waitedUs{};
    uint32_t mostWaitUs{}, periodUs{};
};
static_assert(sizeof(Data) == 64);
} // namespace spidy::native_pace
