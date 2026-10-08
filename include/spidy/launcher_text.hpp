#pragma once
// Text handling for the Spidy launcher: Steam's VDF files, constants read from
// the Python tools, command lines, and log lines. No Windows calls, so the
// checks in tests/launcher_tests.cpp run anywhere.
#include "body_calibration.hpp"
#include "eye_resolution.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace spidy::launcher {

// Every value that follows `key` in a Valve KeyValues (VDF/ACF) text, in order.
// Keys match without regard to case, as Steam's do; escapes (\\ \" \n \t) are decoded.
inline std::vector<std::string> vdfValues(std::string_view text, std::string_view key) {
    std::vector<std::string> tokens;
    std::vector<bool> quoted;
    for (size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n')
                ++i;
        } else if (c == '"') {
            std::string token;
            for (++i; i < text.size() && text[i] != '"'; ++i) {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    const char next = text[++i];
                    token += next == 'n' ? '\n' : next == 't' ? '\t' : next;
                } else {
                    token += text[i];
                }
            }
            ++i;
            tokens.push_back(std::move(token));
            quoted.push_back(true);
        } else if (c == '{' || c == '}') {
            tokens.emplace_back(1, c);
            quoted.push_back(false);
            ++i;
        } else {
            ++i;
        }
    }
    const auto same = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    };
    std::vector<std::string> values;
    for (size_t i = 0; i + 1 < tokens.size(); ++i)
        if (quoted[i] && quoted[i + 1] && same(tokens[i], key)) {
            values.push_back(tokens[i + 1]);
            ++i;
        }
    return values;
}

// The value of a module-level Python constant (`NAME = "text"` or `NAME = 123`).
// The launcher reads the supported build's hash and the VR memory need from the
// tools themselves, so each fact keeps one source.
inline std::optional<std::string> pythonConstant(std::string_view source, std::string_view name) {
    size_t at = 0;
    while ((at = source.find(name, at)) != std::string_view::npos) {
        const bool lineStart = at == 0 || source[at - 1] == '\n';
        size_t i = at + name.size();
        at = i;
        if (!lineStart)
            continue;
        while (i < source.size() && source[i] == ' ')
            ++i;
        if (i >= source.size() || source[i] != '=')
            continue;
        ++i;
        while (i < source.size() && source[i] == ' ')
            ++i;
        if (i < source.size() && (source[i] == '"' || source[i] == '\'')) {
            const char quote = source[i];
            const size_t end = source.find(quote, i + 1);
            if (end == std::string_view::npos)
                return std::nullopt;
            return std::string(source.substr(i + 1, end - i - 1));
        }
        size_t end = i;
        while (end < source.size() && (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '_' ||
                                       source[end] == '.'))
            ++end;
        if (end > i)
            return std::string(source.substr(i, end - i));
    }
    return std::nullopt;
}

// A friendly name for an OpenXR runtime manifest path, as tools/xr_runtime.py names it.
inline std::string runtimeLabel(std::string_view manifest) {
    std::string lowered(manifest);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 8> known{{
        {"virtualdesktop", "Virtual Desktop"},
        {"steamxr", "SteamVR"},
        {"oculus", "Meta Quest Link"},
        {"mixedreality", "Windows Mixed Reality"},
        {"pimax", "Pimax"},
        {"varjo", "Varjo"},
        {"wivrn", "WiVRn"},
        {"vive", "VIVE"},
    }};
    for (const auto& [piece, label] : known)
        if (lowered.find(piece) != std::string::npos)
            return std::string(label);
    return {};
}

// The headset check's line, from spidy_headset_probe.exe's output or from
// tools/xr_runtime.py --detect's, which names the runtime it found the headset
// in and the runtimes it asked first. `found`: the program said so (exit 0).
inline std::string headsetSummary(std::string_view output, bool found) {
    const auto starts = [](std::string_view line, std::string_view prefix) {
        return line.substr(0, prefix.size()) == prefix;
    };
    std::string runtime, headset, eye, last;
    while (!output.empty()) {
        const size_t end = output.find('\n');
        std::string_view line = output.substr(0, end);
        output = end == std::string_view::npos ? std::string_view() : output.substr(end + 1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.remove_suffix(1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
            line.remove_prefix(1);
        if (line.empty() || starts(line, "Looking for the headset in ") || starts(line, "Waiting for "))
            continue;
        if (starts(line, "VR runtime: "))
            runtime = std::string(line.substr(12, line.find(" (") - 12));
        else if (starts(line, "Headset available: "))
            headset = std::string(line.substr(19, line.find(';') - 19));
        else if (starts(line, "Recommended eye 0: "))
            eye = std::string(line.substr(19));
        else
            last = std::string(line);
    }
    if (!found)
        return "Not found. Put the headset on and connect it" + (last.empty() ? std::string(".") : " (" + last + ").");
    std::string summary = headset.empty() ? "Headset available" : "Connected: " + headset;
    if (!runtime.empty())
        summary += " via " + runtime;
    return eye.empty() ? summary + "." : summary + " - " + eye + " per eye";
}

// The eye size the headset check's runtime recommends ("Recommended eye 0: 3072x3264").
inline std::optional<std::array<uint32_t, 2>> recommendedEye(std::string_view output) {
    constexpr std::string_view prefix = "Recommended eye 0: ";
    const size_t at = output.find(prefix);
    if (at == std::string_view::npos || (at && output[at - 1] != '\n'))
        return std::nullopt;
    const char* p = output.data() + at + prefix.size();
    const char* end = output.data() + output.size();
    std::array<uint32_t, 2> eye{};
    auto [x, first] = std::from_chars(p, end, eye[0]);
    if (first != std::errc() || x == end || *x != 'x')
        return std::nullopt;
    if (std::from_chars(x + 1, end, eye[1]).ec != std::errc() || !validEyeSize(eye[0]) || !validEyeSize(eye[1]))
        return std::nullopt;
    return eye;
}

// What the game in VR commits, in GB: `baseMb` (run_game_vr.py's VR_COMMIT_MB, measured at 3072 x 3264 per
// eye) and `bytesPerPixel` (its EYE_COMMIT_BYTES) for each pixel of the two eyes beyond that, as its
// vr_commit_mb counts it.
inline double vrCommitGb(double baseMb, double bytesPerPixel, std::array<uint32_t, 2> eye) {
    const double extra = 2 * (static_cast<double>(eye[0]) * eye[1] - 3072.0 * 3264) * bytesPerPixel / (1 << 20);
    return (baseMb + std::max(0.0, extra)) / 1024;
}

// Quotes one argument so CommandLineToArgvW (and Python) read it back unchanged.
inline std::wstring quoteArgument(std::wstring_view argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
        return std::wstring(argument);
    std::wstring quoted = L"\"";
    for (size_t i = 0;; ++i) {
        size_t slashes = 0;
        while (i < argument.size() && argument[i] == L'\\') {
            ++slashes;
            ++i;
        }
        if (i == argument.size()) {
            quoted.append(slashes * 2, L'\\');
            break;
        }
        if (argument[i] == L'"') {
            quoted.append(slashes * 2 + 1, L'\\');
        } else {
            quoted.append(slashes, L'\\');
        }
        quoted += argument[i];
    }
    return quoted + L'"';
}

// Webs catching props and thugs, the web shooter, your own body and punching
// are always on, and the game draws the webs: the launcher has not offered
// them since October 8 (run_game_vr.py's switches still turn them off).
struct SessionOptions {
    bool smallWindow = true;
    bool stockMonitorView = false;
    int renderScale = 100; // eye resolution in percent of the headset's recommendation, per side
    int swingSpeed = 32;
    bool aimMarkers = true; // markers where each hand's web would land (X switches them in VR)
    bool airWebs = true;    // a web that meets nothing within reach holds in open air there
    int snapTurn = 30;  // degrees per flick of the right stick; 0: no snap turning
    int haptics = 100;  // controller vibration, percent
    int screenSize = 1; // the game screen in the headset: 0 small, 1 medium, 2 large
    int smoothTurn = 0; // degrees a second the right stick turns you while held over; 0: it snap turns
    int weight = 60;    // how heavy you are while webs fly you, percent of real gravity
    // Your T-pose calibration in the headset (body_calibration.hpp): the eye height and arm length Spider-Man's
    // body is sized to, millimetres. Both 0: none yet, and VR asks for one at the first gameplay unless
    // calibrationPrompt is off (you skipped it).
    int eyeHeightMm = 0, armLengthMm = 0;
    bool calibrationPrompt = true;
    bool operator==(const SessionOptions&) const = default;
};

// Whether `options` hold a T-pose calibration a session takes: both measurements, each within its range.
inline bool calibrated(const SessionOptions& options) {
    const auto within = [](int value, unsigned low, unsigned high) {
        return value >= static_cast<int>(low) && value <= static_cast<int>(high);
    };
    return within(options.eyeHeightMm, body_calibration::minEyeHeightMm, body_calibration::maxEyeHeightMm) &&
           within(options.armLengthMm, body_calibration::minArmLengthMm, body_calibration::maxArmLengthMm);
}

// Arguments after the script for tools/run_game_vr.py, for an untimed session
// that starts the game itself (or uses the one already running).
inline std::vector<std::wstring> sessionArguments(const SessionOptions& options, std::wstring_view report,
                                                  std::wstring_view runtime, std::wstring_view stopEvent) {
    std::vector<std::wstring> args{L"--auto-launch", L"--seconds", L"0", L"--swing-speed",
                                   std::to_wstring(std::clamp(options.swingSpeed, 1, 65)), L"--output",
                                   std::wstring(report)};
    if (!options.smallWindow)
        args.emplace_back(L"--full-desktop-view");
    if (options.stockMonitorView)
        args.emplace_back(L"--stock-monitor-view");
    if (!options.aimMarkers)
        args.emplace_back(L"--no-aim-markers");
    if (!options.airWebs)
        args.emplace_back(L"--no-air-webs");
    if (calibrated(options)) {
        args.emplace_back(L"--eye-height");
        args.emplace_back(std::to_wstring(options.eyeHeightMm));
        args.emplace_back(L"--arm-length");
        args.emplace_back(std::to_wstring(options.armLengthMm));
    } else if (!options.calibrationPrompt) {
        args.emplace_back(L"--no-calibration-prompt");
    }
    // The game's Settings offer these too (SPIDY VR), all but the render scale; the defaults go unsaid.
    const std::pair<int, std::pair<const wchar_t*, int>> settings[] = {
        {std::clamp(options.renderScale, static_cast<int>(minimumRenderScale), static_cast<int>(maximumRenderScale)),
         {L"--render-scale", 100}},
        {std::clamp(options.snapTurn, 0, 90), {L"--snap-turn", 30}},
        {std::clamp(options.smoothTurn, 0, 360), {L"--smooth-turn", 0}},
        {std::clamp(options.haptics, 0, 100), {L"--haptics", 100}},
        {std::clamp(options.screenSize, 0, 2), {L"--screen-size", 1}},
        {std::clamp(options.weight, 40, 300), {L"--weight", 60}}};
    for (const auto& [value, flag] : settings)
        if (value != flag.second) {
            args.emplace_back(flag.first);
            args.emplace_back(std::to_wstring(value));
        }
    if (!runtime.empty()) {
        args.emplace_back(L"--xr-runtime");
        args.emplace_back(runtime);
    }
    if (!stopEvent.empty()) {
        args.emplace_back(L"--stop-event");
        args.emplace_back(stopEvent);
    }
    return args;
}

// The line tools/run_game_vr.py prints when a session ends with other VR
// settings than it began with (the SPIDY VR tab in the game's Settings,
// X for the aim markers, a T-pose calibration made or skipped):
//   VR settings from the headset: aim_markers=1 web_grab=1 air_webs=0 ...
// Applies the ones the launcher offers to `options` for the next session
// (web_grab, web_shooter, punch and body it ignores); false when the line is
// another one, or changes nothing.
inline bool headsetSettings(std::string_view line, SessionOptions& options) {
    constexpr std::string_view prefix = "VR settings from the headset: ";
    if (line.substr(0, prefix.size()) != prefix)
        return false;
    line.remove_prefix(prefix.size());
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
        line.remove_suffix(1);
    SessionOptions next = options;
    while (!line.empty()) {
        const size_t space = line.find(' ');
        const std::string_view pair = line.substr(0, space);
        line = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
        const size_t equals = pair.find('=');
        if (equals == std::string_view::npos)
            continue;
        const std::string_view key = pair.substr(0, equals), value = pair.substr(equals + 1);
        int number{};
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (error != std::errc() || end != value.data() + value.size())
            continue;
        if (key == "aim_markers")
            next.aimMarkers = number != 0;
        else if (key == "air_webs")
            next.airWebs = number != 0;
        else if (key == "swing_speed")
            next.swingSpeed = std::clamp(number, 10, 65);
        else if (key == "snap_turn")
            next.snapTurn = std::clamp(number, 0, 90);
        else if (key == "smooth_turn")
            next.smoothTurn = std::clamp(number, 0, 360);
        else if (key == "haptics")
            next.haptics = std::clamp(number, 0, 100);
        else if (key == "screen_size")
            next.screenSize = std::clamp(number, 0, 2);
        else if (key == "weight")
            next.weight = std::clamp(number, 40, 300);
        else if (key == "eye_height_mm")
            next.eyeHeightMm = number;
        else if (key == "arm_length_mm")
            next.armLengthMm = number;
        else if (key == "calibration_prompt")
            next.calibrationPrompt = number != 0;
    }
    // A calibration is both measurements within their ranges, or none at all; anything else keeps the old one.
    if (!calibrated(next) && (next.eyeHeightMm || next.armLengthMm)) {
        next.eyeHeightMm = options.eyeHeightMm;
        next.armLengthMm = options.armLengthMm;
    }
    const bool changed = !(next == options);
    options = next;
    return changed;
}

enum class LineKind { normal, good, warning, error };

// How the launcher colours a line the session printed.
inline LineKind classifyLine(std::string_view line) {
    const auto starts = [&](std::string_view prefix) { return line.substr(0, prefix.size()) == prefix; };
    if (starts("WARNING") || starts("Click the game window"))
        return LineKind::warning;
    if (starts("Spidy VR unavailable") || starts("Traceback") || starts("RuntimeError") || starts("OSError") ||
        line.find("Error:") != std::string_view::npos)
        return LineKind::error;
    if (starts("Headset available") || starts("Player found") || starts("Rendering ") || starts("Render memory: 512"))
        return LineKind::good;
    return LineKind::normal;
}

// "14.44.35211.0" style versions, compared field by field.
inline std::array<int, 4> parseVersion(std::string_view text) {
    std::array<int, 4> fields{};
    size_t field = 0;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            fields[field] = fields[field] * 10 + (c - '0');
        } else if (c == '.') {
            if (++field == fields.size())
                break;
        } else if (c != 'v' && c != 'V') {
            break;
        }
    }
    return fields;
}

} // namespace spidy::launcher
