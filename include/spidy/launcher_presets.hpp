#pragma once
// Built-in gameplay presets. Personal setup stays with the player; custom .spidy profiles keep
// their existing behavior. The table is explicit so switching presets also resets inactive tuning.
#include "launcher_profile.hpp"

namespace spidy::launcher {
// The presets' values are vr_settings' (the tab's PLAY STYLE applies them in the headset too).
using vr_settings::gameplayPresetSettings;
using vr_settings::PresetSetting;
// Every non-retired profile key belongs to exactly one of these two lists (tested).
inline constexpr std::string_view personalProfileSettings[] = {
    "trigger_webs",
    "snap_turn",
    "smooth_turn",
    "comfort_vignette",
    "haptics",
    "reel_haptics",
    "swing_haptics",
    "blow_sounds",
    "pc_media",
    "body_haptics",
    "body_haptics_webs",
    "body_haptics_wind",
    "body_haptics_combat",
    "body_haptics_movement",
    "screen_size",
    "screen_depth",
    "hud",
    "ui_style",
    "combat_visuals",
    "air_fists",
    "enemy_types", // the player's choice, not a preset's: a preset tunes how Spidy moves
    "near_clip_mm",
    "seated",
    "web_angle_degrees",
    "wrist_shooters",
    "web_aim_source",
    "forearm_blend",
    "forearm_aim_limit",
    "palm_shooters",
    "run_logs",
};
struct BuiltInProfile {
    Profile profile;
    const char* description;
};
inline const std::array<BuiltInProfile, vr_settings::presetCount>& builtInProfiles() {
    static const auto profiles = [] {
        std::array<BuiltInProfile, vr_settings::presetCount> result{};
        constexpr const char* descriptions[] = {
            "Light, assisted swings. Open-air webs; no flips.",
            "Real ropes, softer catches, open-air webs. Toggle manual flips with A in the air.",
            "Heavy, unassisted swings: webs need a surface. Arms and momentum rotate you in the air.",
        };
        for (size_t p = 0; p < result.size(); ++p) {
            result[p].profile.name = vr_settings::presetNames[p];
            result[p].description = descriptions[p];
            for (size_t i = 0; i < std::size(profileSettings); ++i)
                for (const auto& setting : gameplayPresetSettings)
                    if (profileSettings[i].key == setting.key)
                        result[p].profile.values[i] = setting.values[p];
        }
        return result;
    }();
    return profiles;
}
// Slash cannot be in a saved Windows filename: a player's "Arcade" stays separate from the preset.
inline std::string builtInProfileKey(std::string_view name) {
    return "@builtin/" + std::string(name);
}
inline const char* builtInProfileDescription(std::string_view name) {
    for (const auto& preset : builtInProfiles())
        if (preset.profile.name == name)
            return preset.description;
    return "";
}
} // namespace spidy::launcher
