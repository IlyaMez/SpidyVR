#pragma once
// Settings profiles: how Spidy plays, as a text that a player keeps, copies into a Discord message or a
// file, and pastes or imports again. The launcher keeps its profiles as files in %APPDATA%\Spidy\profiles
// (apps/launcher/system.cpp); what is here has no Windows calls, and tests/launcher_tests.cpp checks it.
#include "launcher_text.hpp"
#include "vr_settings.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace spidy::launcher {

// The settings a profile holds: every setting of the SPIDY VR tab but VR VIEW (each session starts in VR),
// CALIBRATE BODY (a request, not a setting) and RECORDING FOV (the monitor's), the realistic flips' rows
// the launcher alone shows (vr_settings::Place::launcher), and two settings no more (retired). Not the
// launcher's graphics options
// (render resolution, upscaling, the game window, the monitor, the game's depth of field and sharpening),
// which suit one PC, as RECORDING FOV does, nor the T-pose calibration, which is one body's: a profile
// leaves them as they are.
struct PlaySettings {
    SessionOptions options;  // the launcher's options (launcher.ini)
    vr_settings::Values tab; // those launcher.ini has no key for (spidy_vr.ini, vr_settings::tabOnlyText)
};

// One setting of a profile: its key in the profile's text, its row in the tab, the whole numbers it takes
// (0 and 1 for a switch), and where the launcher keeps it: one of its options (launcher.ini), or one of
// those in spidy_vr.ini. Or nowhere: a setting no more (retired, no row), whose one value is low.
struct ProfileSetting {
    std::string_view key;
    vr_settings::Item item{};
    int low{}, high{};
    bool SessionOptions::*option{};
    int SessionOptions::*optionNumber{};
    bool vr_settings::Values::*flag{};
    int vr_settings::Values::*number{};
    constexpr bool retired() const { return !option && !optionNumber && !flag && !number; }
    constexpr bool isSwitch() const { return option || flag || (retired() && high <= 1); }
};

namespace detail {
using vr_settings::Item;
using vr_settings::Values;
constexpr ProfileSetting launcherOption(std::string_view key, Item item, bool SessionOptions::*option) {
    return {key, item, 0, 1, option};
}
constexpr ProfileSetting launcherOption(std::string_view key, Item item, int low, int high,
                                        int SessionOptions::*number) {
    return {key, item, low, high, nullptr, number};
}
constexpr ProfileSetting tabOnly(std::string_view key, Item item, bool Values::*flag) {
    return {key, item, 0, 1, nullptr, nullptr, flag};
}
constexpr ProfileSetting tabOnly(std::string_view key, Item item, int low, int high, int Values::*number) {
    return {key, item, low, high, nullptr, nullptr, nullptr, number};
}
// A setting no more since October 25, which a profile still holds at the one value Spidy plays with now:
// the launchers before (0.4.1 on) have it, and without it a profile would be one short of their settings
// ("It has 32 of the 34") and never taken for the ones in use. Read, it is that value whatever the text
// says, and using it changes nothing.
constexpr ProfileSetting retired(std::string_view key, int value) {
    return {key, Item::none, value, value};
}
// In the tab's order, its sections WEBS, SWINGING, COMFORT, HAPTICS, DISPLAY, BODY (seated play; not
// HEIGHT, which is the player's own, as a calibration is) and EXPERIMENTAL. The keys are launcher.ini's and
// spidy_vr.ini's, the weight's the session's line's (launcher.ini calls it gravity_percent only because
// older launchers saved other weights as weight). A key, once in a profile someone shared, keeps its name
// and meaning, its place too once its setting is no more: wall running, on (the swing's own walls), and
// the wall camera's distance, fixed at 25 cm.
inline constexpr ProfileSetting profileSettings[] = {
    launcherOption("arcade_webs", Item::webStyle, &SessionOptions::arcadeWebs),
    launcherOption("trigger_webs", Item::webButton, &SessionOptions::triggerWebs),
    launcherOption("aim_markers", Item::aimMarkers, &SessionOptions::aimMarkers),
    launcherOption("air_webs", Item::airWebs, &SessionOptions::airWebs),
    tabOnly("smart_anchor", Item::smartAnchor, &Values::smartAnchor),
    launcherOption("swing_speed", Item::swingSpeed, 10, 65, &SessionOptions::swingSpeed),
    launcherOption("weight", Item::weight, 40, 300, &SessionOptions::weight),
    tabOnly("elastic_web", Item::elasticWeb, 0, 3, &Values::elasticWeb),
    tabOnly("soft_release", Item::softRelease, 0, 3, &Values::softRelease),
    tabOnly("swing_assist", Item::swingAssist, &Values::swingAssist),
    tabOnly("web_slingshot", Item::webSlingshot, &Values::webSlingshot),
    tabOnly("slingshot_speed", Item::slingshotSpeed, 0, 300, &Values::slingshotSpeed),
    tabOnly("better_climbing", Item::betterClimbing, &Values::betterClimbing),
    tabOnly("two_hand_zip", Item::twoHandZip, &Values::twoHandZip),
    tabOnly("rope_steering", Item::ropeSteering, &Values::ropeSteering),
    retired("wall_run", 1),
    launcherOption("snap_turn", Item::snapTurn, 0, 90, &SessionOptions::snapTurn),
    launcherOption("smooth_turn", Item::smoothTurn, 0, 360, &SessionOptions::smoothTurn),
    launcherOption("haptics", Item::haptics, 0, 100, &SessionOptions::haptics),
    tabOnly("blow_sounds", Item::blowSounds, 0, 2, &Values::blowSounds),
    launcherOption("body_haptics", Item::bodyHaptics, 0, 100, &SessionOptions::bodyHaptics),
    tabOnly("body_haptics_webs", Item::hapticWebs, &Values::hapticWebs),
    tabOnly("body_haptics_wind", Item::hapticWind, &Values::hapticWind),
    tabOnly("body_haptics_combat", Item::hapticCombat, &Values::hapticCombat),
    tabOnly("body_haptics_movement", Item::hapticMovement, &Values::hapticMovement),
    launcherOption("screen_size", Item::screenSize, 0, 2, &SessionOptions::screenSize),
    launcherOption("screen_depth", Item::screenDepth, 0, 3, &SessionOptions::screenDepth),
    launcherOption("hud", Item::hud, 0, 3, &SessionOptions::hud),
    tabOnly("ui_style", Item::uiStyle, 0, 1, &Values::uiStyle),
    tabOnly("combat_visuals", Item::combatVisuals, &Values::combatVisuals),
    tabOnly("near_clip_mm", Item::nearClip, 10, 100, &Values::nearClipMm),
    tabOnly("seated", Item::seated, &Values::seated),
    launcherOption("flips", Item::flips, &SessionOptions::flips),
    tabOnly("controlled_flips", Item::controlledFlips, &Values::controlledFlips),
    launcherOption("flip_speed", Item::flipSpeed, 90, 480, &SessionOptions::flipSpeed),
    tabOnly("hand_climb", Item::handClimb, &Values::handClimb),
    tabOnly("wall_run_camera_tilt", Item::wallRunCameraTilt, &Values::wallRunCameraTilt),
    retired("wall_distance_cm", vr_settings::fixedWallDistance),
    tabOnly("realistic_flips", Item::realisticFlips, &Values::realisticFlips),
    tabOnly("rotation_strength", Item::automaticStrength, 25, 100, &Values::automaticStrength),
    tabOnly("rotation_speed", Item::automaticSpeed, 90, 360, &Values::automaticSpeed),
    tabOnly("flight_speed_min", Item::automaticMinimumSpeed, 3, 16, &Values::automaticMinimumSpeed),
    tabOnly("swing_momentum", Item::automaticReleaseFlip, &Values::automaticReleaseFlip),
    tabOnly("rotation_response_ms", Item::automaticResponse, 100, 750, &Values::automaticResponse),
    tabOnly("hand_momentum", Item::automaticHandStrength, 0, 200, &Values::automaticHandStrength),
    tabOnly("angular_drag_ms", Item::automaticFalloff, 500, 3000, &Values::automaticFalloff),
    tabOnly("hand_speed_min_cm", Item::automaticHandThreshold, 50, 300, &Values::automaticHandThreshold),
    tabOnly("air_rotation_brake", Item::automaticAirBrake, &Values::automaticAirBrake),
    tabOnly("flips_in_combat", Item::automaticCombat, &Values::automaticCombat),
};
} // namespace detail
using detail::profileSettings;

inline int profileValue(const ProfileSetting& s, const PlaySettings& settings) {
    if (s.retired())
        return s.low;
    if (s.option)
        return settings.options.*s.option ? 1 : 0;
    if (s.optionNumber)
        return settings.options.*s.optionNumber;
    if (s.flag)
        return settings.tab.*s.flag ? 1 : 0;
    return settings.tab.*s.number;
}

// Within the setting's range; a switch is on for any number but 0. A retired one has nowhere to go.
inline void setProfileValue(const ProfileSetting& s, int value, PlaySettings& settings) {
    value = s.isSwitch() ? (value != 0 ? 1 : 0) : std::clamp(value, s.low, s.high);
    if (s.option)
        settings.options.*s.option = value != 0;
    else if (s.optionNumber)
        settings.options.*s.optionNumber = value;
    else if (s.flag)
        settings.tab.*s.flag = value != 0;
    else if (s.number)
        settings.tab.*s.number = value;
}

// A profile: a value for each setting it holds. One it lacks (a profile from an older Spidy, one written
// by hand) stays as it is when the profile is used.
struct Profile {
    std::string name;     // what its text calls it ("name="); may be empty
    std::string madeWith; // the version of Spidy that wrote it ("spidy=")
    std::array<std::optional<int>, std::size(profileSettings)> values{};
    size_t count() const {
        return static_cast<size_t>(
            std::count_if(values.begin(), values.end(), [](const auto& v) { return v.has_value(); }));
    }
    bool complete() const { return count() == values.size(); }
    bool operator==(const Profile&) const = default;
};

inline Profile profileOf(const PlaySettings& settings, std::string name = {}) {
    Profile profile;
    profile.name = std::move(name);
    for (size_t i = 0; i < std::size(profileSettings); ++i)
        profile.values[i] = profileValue(profileSettings[i], settings);
    return profile;
}

inline void applyProfile(const Profile& profile, PlaySettings& settings) {
    for (size_t i = 0; i < std::size(profileSettings); ++i)
        if (profile.values[i])
            setProfileValue(profileSettings[i], *profile.values[i], settings);
}

// Whether the settings are the profile's: each one it holds at its value.
inline bool profileMatches(const Profile& profile, const PlaySettings& settings) {
    for (size_t i = 0; i < std::size(profileSettings); ++i)
        if (profile.values[i] && *profile.values[i] != profileValue(profileSettings[i], settings))
            return false;
    return true;
}

// A profile's name as the launcher shows and saves it: no control characters, no spaces at either end, at
// most 40 characters.
inline std::string profileName(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    std::string name;
    size_t characters = 0;
    for (size_t i = 0; i < text.size() && characters < 40;) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const size_t length = std::min(text.size() - i, lead >= 0xF0   ? size_t{4}
                                                         : lead >= 0xE0 ? size_t{3}
                                                         : lead >= 0xC0 ? size_t{2}
                                                                        : size_t{1});
        if (lead >= 0x20 && lead != 0x7F) {
            name.append(text.substr(i, length));
            ++characters;
        }
        i += length;
    }
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    return name;
}

// The file a profile of that name is kept in, without its ".spidy": the name without the characters Windows
// refuses in a file name and the dots and spaces it drops from a name's end, with "_" after a name Windows
// keeps for a device (CON, NUL, COM1...). Empty when nothing is left.
inline std::string profileFileName(std::string_view name) {
    std::string file;
    for (const char c : profileName(name))
        if (std::string_view("<>:\"/\\|?*").find(c) == std::string_view::npos)
            file += c;
    while (!file.empty() && (file.back() == ' ' || file.back() == '.'))
        file.pop_back();
    while (!file.empty() && file.front() == ' ')
        file.erase(file.begin());
    std::string device = file.substr(0, file.find('.'));
    std::transform(device.begin(), device.end(), device.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const bool numbered = device.size() == 4 && (device.starts_with("COM") || device.starts_with("LPT")) &&
                          device[3] >= '1' && device[3] <= '9';
    if (device == "CON" || device == "PRN" || device == "AUX" || device == "NUL" || numbered)
        file.insert(device.size(), "_");
    return file;
}

// A profile's text: a line that says what it is, its name, the Spidy that wrote it, then a key=value line
// for each setting it holds, in the tab's order.
inline std::string profileText(const Profile& profile, std::string_view version) {
    std::string text = "# Spidy settings profile. To use it, in the Spidy launcher: Profile > Paste a "
                       "profile, or Import a file.\n";
    if (!profile.name.empty())
        text.append("name=").append(profile.name).append("\n");
    text.append("spidy=").append(version).append("\n");
    for (size_t i = 0; i < std::size(profileSettings); ++i)
        if (const auto& value = profile.values[i])
            text += std::string(profileSettings[i].key) + "=" + std::to_string(*value) + "\n";
    return text;
}

// The text to paste into a Discord message: the profile in a code block (ini, which colours its keys).
inline std::string profileMessage(const Profile& profile, std::string_view version) {
    return "```ini\n" + profileText(profile, version) + "```";
}

// The profile in a text: one profileText wrote, copied out of a Discord message (the code block's marks, a
// quote's ">" and whatever else is around it are passed over), or written by hand. Its settings come one to
// a line, or several to a line as a session's "VR settings from the headset" line has them, with spaces
// round '=' or none; a switch takes on/off, true/false and yes/no too. A value outside its setting's range
// is taken to the nearer end, and a retired setting's to its one value; a key no setting has, and a value
// that is no whole number, are passed over. launcher.ini's gravity_percent is the weight. nullopt when the
// text holds no setting at all.
inline std::optional<Profile> readProfile(std::string_view text) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3);
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    const auto lower = [](std::string_view s) {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    };
    Profile profile;
    bool any = false;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        while (!line.empty() && (blank(line.front()) || line.front() == '>'))
            line.remove_prefix(1);
        while (!line.empty() && blank(line.back()))
            line.remove_suffix(1);
        if (line.empty() || line.front() == '#' || line.front() == ';')
            continue;
        // The name is the rest of its line, spaces and all.
        if (const size_t equals = line.find('='); equals != std::string_view::npos) {
            std::string_view key = line.substr(0, equals);
            while (!key.empty() && blank(key.back()))
                key.remove_suffix(1);
            if (lower(key) == "name") {
                profile.name = profileName(line.substr(equals + 1));
                continue;
            }
        }
        // Spaces round '=' go, and each word with '=' in it is a setting.
        std::string joined;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] != '=') {
                joined += line[i];
                continue;
            }
            while (!joined.empty() && blank(joined.back()))
                joined.pop_back();
            joined += '=';
            while (i + 1 < line.size() && blank(line[i + 1]))
                ++i;
        }
        const auto apart = [&](char c) { return blank(c) || c == '`' || c == ',' || c == ';'; };
        for (std::string_view rest = joined; !rest.empty();) {
            size_t stop = 0;
            while (stop < rest.size() && !apart(rest[stop]))
                ++stop;
            const std::string_view word = rest.substr(0, stop);
            rest.remove_prefix(std::min(rest.size(), stop + 1));
            const size_t equals = word.find('=');
            if (equals == std::string_view::npos)
                continue;
            std::string key = lower(word.substr(0, equals));
            const std::string_view value = word.substr(equals + 1);
            if (key == "spidy") {
                profile.madeWith.clear();
                for (const char c : value.substr(0, 16))
                    if ((c >= '0' && c <= '9') || c == '.')
                        profile.madeWith += c;
                continue;
            }
            if (key == "gravity_percent")
                key = "weight";
            for (size_t i = 0; i < std::size(profileSettings); ++i) {
                const auto& s = profileSettings[i];
                if (s.key != key)
                    continue;
                int number{};
                const std::string spelled = lower(value);
                const char* const valueEnd = value.data() + value.size();
                if (s.isSwitch() && (spelled == "on" || spelled == "true" || spelled == "yes"))
                    number = 1;
                else if (s.isSwitch() && (spelled == "off" || spelled == "false" || spelled == "no"))
                    number = 0;
                else if (const auto [last, error] = std::from_chars(value.data(), valueEnd, number);
                         error != std::errc() || last != valueEnd)
                    break;
                profile.values[i] = s.retired()    ? s.low
                                    : s.isSwitch() ? (number != 0 ? 1 : 0)
                                                   : std::clamp(number, s.low, s.high);
                any = true;
                break;
            }
        }
    }
    if (!any)
        return std::nullopt;
    return profile;
}

// A setting's name as its row in the SPIDY VR tab has it (the launcher's own rows too); a retired one,
// which has no row and never changes, by its key.
inline std::string profileSettingName(const ProfileSetting& s) {
    if (s.item != vr_settings::Item::none)
        for (const auto& row : vr_settings::rows())
            if (row.item == s.item)
                return row.title;
    return std::string(s.key);
}

// A value as the tab shows it: the choice nearest to it, or ON or OFF; the swing speed limit, which the
// launcher sets to any whole metre a second, as it is.
inline std::string profileSettingValue(const ProfileSetting& s, int value) {
    if (s.item == vr_settings::Item::swingSpeed)
        return std::to_string(value) + " M/S";
    std::span<const char* const> choices;
    if (s.item != vr_settings::Item::none)
        for (const auto& row : vr_settings::rows())
            if (row.item == s.item)
                choices = row.choices;
    const auto steps = vr_settings::steps(s.item);
    if (steps.empty())
        return choices.size() == 2 ? choices[value ? 1 : 0] : value ? "ON" : "OFF";
    size_t nearest = 0;
    for (size_t i = 1; i < steps.size(); ++i)
        if (std::abs(steps[i] - value) < std::abs(steps[nearest] - value))
            nearest = i;
    return nearest < choices.size() ? std::string(choices[nearest]) : std::to_string(value);
}

// A setting a profile would change: its name and its value before and after, as the tab shows them.
struct ProfileChange {
    std::string setting, from, to;
    bool operator==(const ProfileChange&) const = default;
};

// What using the profile would change in the settings, in the tab's order.
inline std::vector<ProfileChange> profileChanges(const Profile& profile, const PlaySettings& settings) {
    std::vector<ProfileChange> changes;
    for (size_t i = 0; i < std::size(profileSettings); ++i) {
        const auto& s = profileSettings[i];
        const int now = profileValue(s, settings);
        if (profile.values[i] && *profile.values[i] != now)
            changes.push_back({profileSettingName(s), profileSettingValue(s, now),
                               profileSettingValue(s, *profile.values[i])});
    }
    return changes;
}

} // namespace spidy::launcher
