#pragma once
// Built-in gameplay presets. Personal setup stays with the player; custom .spidy profiles keep
// their existing behavior. The table is explicit so switching presets also resets inactive tuning.
#include "launcher_profile.hpp"

namespace spidy::launcher {
struct PresetSetting {
    std::string_view key;
    std::array<int, 3> values; // Arcade, Balanced, Pro
};
inline constexpr PresetSetting gameplayPresetSettings[] = {
    {"arcade_webs", {1, 0, 0}},
    {"aim_markers", {1, 1, 0}},
    {"air_webs", {1, 0, 0}},
    {"smart_anchor", {0, 1, 0}},
    {"web_range_m", {150, 100, 100}},
    {"web_speed", {0, 3, 3}},
    {"swing_speed", {32, 40, 56}},
    {"weight", {80, 125, 200}},
    {"elastic_web", {2, 1, 0}},
    {"soft_release", {2, 1, 0}},
    {"swing_assist", {1, 1, 0}},
    {"web_slingshot", {0, 1, 1}},
    {"slingshot_speed", {40, 55, 80}},
    {"better_climbing", {1, 1, 1}},
    {"single_hand_yank_percent", {150, 125, 150}},
    {"two_hand_zip", {0, 1, 1}},
    {"rope_steering", {0, 1, 1}},
    {"flips", {0, 0, 0}},
    {"controlled_flips", {0, 1, 0}},
    {"flip_speed", {180, 240, 300}},
    {"hand_climb", {0, 0, 1}},
    {"joystick_wall_run_camera", {1, 1, 0}},
    {"wall_run_camera_tilt", {0, 1, 1}},
    {"half_punch_damage", {0, 0, 0}},
    {"web_tie", {0, 1, 1}},
    {"native_run", {1, 1, 1}},
    {"air_slingshot", {0, 0, 1}},
    {"dodge_tumble", {0, 0, 1}},
    {"run_curve", {1, 0, 0}},
    {"native_run_at_percent", {70, 70, 80}},
    {"running_leap", {0, 1, 1}},
    {"early_jump", {1, 1, 0}},
    {"soft_landing", {1, 1, 0}},
    {"spidy_run", {0, 1, 1}},
    {"spidy_run_from_standing", {0, 0, 1}},
    {"web_landing_smooth", {1, 1, 1}},
    {"web_landing_impact_speed", {24, 24, 24}},
    {"steady_states", {1, 1, 1}},
    {"combat_responsive", {1, 1, 1}},
    {"spidy_run_jump", {1, 1, 1}},
    {"web_lines", {0, 1, 1}},
    {"realistic_flips", {0, 0, 1}},
    {"rotation_strength", {25, 50, 25}},
    {"rotation_speed", {240, 240, 360}},
    {"flight_speed_min", {8, 8, 8}},
    {"swing_momentum", {0, 0, 1}},
    {"rotation_response_ms", {750, 500, 200}},
    {"hand_momentum", {100, 100, 100}},
    {"angular_drag_ms", {500, 1500, 1500}},
    {"hand_speed_min_cm", {200, 200, 100}},
    {"air_rotation_brake", {1, 1, 1}},
    {"flips_in_combat", {0, 0, 0}},
};
// Every non-retired profile key belongs to exactly one of these two lists (tested).
inline constexpr std::string_view personalProfileSettings[] = {
    "trigger_webs",
    "snap_turn",
    "smooth_turn",
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
inline const std::array<BuiltInProfile, 3>& builtInProfiles() {
    static const auto profiles = [] {
        std::array<BuiltInProfile, 3> result{};
        constexpr const char* names[] = {"Arcade", "Balanced", "Pro"};
        constexpr const char* descriptions[] = {
            "Light, assisted swings. Open-air webs; no flips.",
            "Real anchors, softer catches. Toggle manual flips with A in the air.",
            "Heavy, unassisted swings. Arms and momentum rotate you in the air.",
        };
        for (size_t p = 0; p < result.size(); ++p) {
            result[p].profile.name = names[p];
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
