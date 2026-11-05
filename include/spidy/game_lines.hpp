#pragma once
#include "math.hpp"
#include <cstdint>

// Web lines in the game (web_line.hpp, docs/WEB-LINE.md): the lines the swing
// module keeps, for the headset (SpidyLineSample: their ropes and strands, the
// gesture's tick) and the session report (SpidyLineData).
namespace spidy::game_lines {
struct Line {
    Vec3 start{}, end{};
    uint32_t id{};
    float age{};
};
// armed: the gesture is ready now (both hands' webs on surfaces, hands
// together). made, expired: lines made, and those that went (too old, too
// far, or the oldest when a new one came). zips, perches: zips to a line
// begun, and those that came to their perch on it. target, targetId: the line
// point the head looks at, the zip's marker (0: none in view). phase: the
// zip's (web_line.hpp LineZip::Phase: 0 none, 1 flying, 2 on the line, 3
// jumping off it). jumps, landings: jumps off a line, and those that came
// down on it again.
// Version 2 (November 3): room for 8 lines (six at once now). Version 3: the
// zip. Version 4: jumps and landings. Version 5: the zip's web.
struct LineData {
    uint32_t magic = 0x534c494e, version = 5, bytes = sizeof(LineData), status{};
    int64_t sequence{};
    uint64_t made{}, expired{}, zips{}, perches{}, jumps{}, landings{};
    uint32_t count{}, armed{};
    Line lines[8]{};
    Vec3 target{};
    // onLine: the line the player is on or zips to (0: none), for the
    // headset's walk along it.
    uint32_t targetId{}, phase{}, onLine{};
    // A zip under way (phase 1): where its web holds (the line's point), the
    // hand that shot it, and which zip it is (for its rope: a new one, a new
    // web).
    Vec3 zipTo{};
    uint32_t zipHand{};
    uint64_t zipKey{};
};
static_assert(sizeof(Line) == 32 && sizeof(LineData) == 384);
} // namespace spidy::game_lines
