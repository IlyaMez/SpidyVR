// SESSION DIAGNOSTICS: the feature lines (RUN LOGS' "run log:", WEB AIM's
// "forearm aim:", DODGE TUMBLE's "dodge tumble:", the state changes on the
// ground and in the air) where a player can see and send them. Until v4 they
// went only into the session report's timeline (game_prompts::note: 32 lines
// of 128 characters, "forearm aim:" every 2 s pushing out everything else),
// never into the launcher's console (the November 2 logs: not one such line in
// four sessions). Now each also goes into SpidyDiagData, which run_game_vr.py
// reads with the rest of a session, prints to the console and appends to
// reports/<session>-diag.log. The report's timeline is as before.
//
// Two rings, so the frequent lines cannot push out the rare ones: `main` for
// everything, `aim` for "forearm aim:" alone. Each line is numbered by the
// ring's count of lines ever written, so a reader that falls behind knows how
// many it lost.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace spidy::diag_log {
inline constexpr unsigned mainLines = 128, aimLines = 16, lineChars = 240, buildChars = 96;
enum class Channel : uint8_t { main, aim };
// Which ring a line belongs in: "forearm aim:" lines in their own.
inline Channel channelOf(std::string_view text) {
    return text.substr(0, 12) == "forearm aim:" ? Channel::aim : Channel::main;
}
template <unsigned N> struct Ring {
    uint64_t written{};
    char line[N][lineChars]{};
    void push(const char* text) {
        auto& slot = line[written % N];
        std::snprintf(slot, lineChars, "%s", text ? text : "");
        ++written;
    }
    // The newest `count` lines' text in writing order, at most N.
    const char* at(uint64_t index) const {
        return index < written && written - index <= N ? line[index % N] : nullptr;
    }
};
// SpidyDiagData (the stereo module's export, run_game_vr.py diag_snapshot):
// sequence odd while it is being written, the build's id, then both rings.
struct Data {
    uint32_t magic = 0x47414944 /* DIAG */, version = 1, bytes = sizeof(Data), reserved{};
    volatile int64_t sequence{};
    char build[buildChars]{};
    Ring<mainLines> main{};
    Ring<aimLines> aim{};
};
static_assert(offsetof(Data, sequence) == 16 && offsetof(Data, build) == 24 && offsetof(Data, main) == 120 &&
              offsetof(Data, aim) == 120 + 8 + mainLines * lineChars &&
              sizeof(Data) == 120 + 2 * 8 + (mainLines + aimLines) * lineChars);
// The build's id: the version, and the git commit it was built from where the
// build knew it (CMake, at configure), else when it was built.
inline std::string buildId(const char* version, const char* commit, const char* stamp) {
    std::string out = "Spidy ";
    out += version && *version ? version : "?";
    if (commit && *commit)
        out += std::string(" (commit ") + commit + ")";
    else
        out += std::string(" (built ") + (stamp && *stamp ? stamp : "?") + ")";
    return out;
}
// WEB AIM's "forearm aim:" lines, at most when something a reader cares about
// changed: the fallback (why the controller's line was used), a turn moved by
// turnStep degrees or more, or heartbeat seconds since the last; and never
// more than once in quiet seconds.
class ChangeOnly {
  public:
    static constexpr float turnStep = 5, heartbeat = 10, quiet = .5f;
    bool update(float seconds, int reason, float turnDegrees) {
        since_ += seconds > 0 && seconds < 1 ? seconds : 0.f;
        const bool changed = !primed_ || reason != reason_ ||
                             (turnDegrees - turn_ >= turnStep || turn_ - turnDegrees >= turnStep);
        if (!primed_ || (changed && since_ >= quiet) || since_ >= heartbeat) {
            primed_ = true;
            reason_ = reason;
            turn_ = turnDegrees;
            since_ = 0;
            return true;
        }
        return false;
    }

  private:
    bool primed_{};
    int reason_{};
    float turn_{}, since_{};
};
// A line for the diagnostics (and only there: the report's timeline is
// game_prompts::note's, which calls this too). Stamped with the seconds since
// the session's first line. Thread-safe. In the stereo module (diag_log.cpp).
void write(const char* text);
// Formatted, as printf.
void writef(const char* format, ...);
// The rings, copied into SpidyDiagData by the VR worker.
void copyOut(Data& out);
// This build's id (buildId with what CMake gave diag_log.cpp).
const char* build();
} // namespace spidy::diag_log
