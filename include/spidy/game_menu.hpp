#pragma once
#include "vr_settings.hpp"
#include <cstdint>

// Spidy's VR settings as a tab of the game's own Settings: pause, Settings,
// then SPIDY VR after KEY MAPPING (the list wraps: Up from GAME). The game
// builds the tab's rows with its own option code, from settings items Spidy
// adds beside the ones its config holds, so they look, move and reset like the
// game's own: switches ON/OFF, lists stepped with left and right, X RESET and
// Y RESET ALL. Spidy answers the game's questions about those items (their
// texts and values) and takes the changes; the game's own settings and save
// never see them. The title screen's Options have no such tab.
namespace spidy::game_menu {
// Hooks the game's settings menu. 0 when hooked; 9301-9311 when the code at
// a hook is not this game's, 9400-9499 when hooking failed.
uint32_t install(uintptr_t base);
// Unhooks. A tab still open shows its rows until the game rebuilds it; then
// they are gone, and a change made in them meanwhile does nothing.
void uninstall();
// What the tab shows: the session's values (X switches the aim markers
// during play). A change made in the tab and not yet taken is kept.
void publish(const vr_settings::Values&);
// A change made in the tab since the last call: true, with `values` holding it.
bool take(vr_settings::Values&);
struct Telemetry {
    // The game built its Settings with the SPIDY VR tab (each opening, and
    // each refresh after a change), and the settings changed in it (RESET and
    // RESET ALL included).
    uint64_t tabs{}, changes{};
    // installed: hooked. status: 0, or why the tab is missing: 9501 the game's
    // settings config is not loaded (looked for again at the next opening),
    // 9502 it has no item to build rows from, 9503 the game's code faulted
    // building the tab (not tried again).
    uint32_t installed{}, status{};
};
Telemetry telemetry();
} // namespace spidy::game_menu
