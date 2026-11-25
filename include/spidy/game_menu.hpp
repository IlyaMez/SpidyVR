#pragma once
#include "vr_settings.hpp"
#include <cstdint>

// Spidy's VR settings as seven category tabs after the game's own Settings.
// The list wraps: Up from GAME reaches VR DISPLAY. VR VIEW stays
// on the pause menu's own first page, under RESUME, which the game builds from
// the same kind of items (vr_settings::Place::page). The game
// builds the tab's rows with its own option code, from settings items Spidy
// adds beside the ones its config holds, so they look, move and reset like the
// game's own: switches ON/OFF, lists stepped with left and right, X RESET and
// Y RESET ALL for the current category. Spidy answers the game's questions about those items (their
// texts and values) and takes the changes; the game's own settings and save
// never see them. The title screen's Settings get VR SETUP as a page of their
// own (SPIDY VR SETUP, before GAME), built the same way.
namespace spidy::game_menu {
// Hooks the game's settings menu. 0 when hooked; 9301-9312 when the code at
// a hook is not this game's, 9400-9499 when hooking failed.
uint32_t install(uintptr_t base);
// Unhooks. A tab still open shows its rows until the game rebuilds it; then
// they are gone, and a change made in them meanwhile does nothing.
void uninstall();
// What the tab shows: the session's values. A change made in the tab and
// not yet taken is kept.
void publish(const vr_settings::Values&);
// A change made in the tab since the last call: true, with `values` holding it.
bool take(vr_settings::Values&);

// The first-time setup: the game's own Settings opened on VR SETUP for a new
// player, before the first game. The XR worker arms it as the title screen
// shows; nothing presses buttons for the player.
// - title: the title Settings' root page, as it is built, is swapped for VR
//   SETUP's, its way back to the root kept; once the main menu takes input,
//   the title's update clicks SETTINGS the way a mouse would (Flash's
//   TitleMenu.OnMouseEvent), and the Settings open on VR SETUP. B goes back to
//   the root. The player's own SETTINGS opens them there too.
// - pause: still in the code, no longer used: the pause menu's Settings with
//   the setup's tab alone (SET UP SPIDY VR); B leaves them and the pause menu.
enum class Setup : uint32_t {
    off,     // not asked for, or cancelled
    armed,   // waiting for the menu
    opening, // the menu is up; its Settings are on their way
    showing, // the Settings show the setup
    done,    // left
};
enum class SetupFlow : uint32_t { pause, title };
// Whether this game's menus can be opened onto the setup: the hooks are in and
// the code the flow relies on is this game's.
bool setupAvailable(SetupFlow);
// Arms the setup (from off or done).
void beginSetup(SetupFlow);
// Back to off while armed or opening; true when it was.
bool cancelSetup();
// The XR worker saw play again while the setup was up: done.
void endSetup();
Setup setup();

struct Telemetry {
    // The game built its Settings with all VR categories (each opening, and
    // each refresh after a change), and the settings changed in it (RESET and
    // RESET ALL included).
    uint64_t tabs{}, changes{};
    // installed: hooked. status: 0, or why the tab is missing: 9501 the game's
    // settings config is not loaded (looked for again at the next opening),
    // 9502 it has no item to build rows from, 9503 the game's code faulted
    // building the tab (not tried again), 9504 it faulted adding VR VIEW to
    // the first page.
    uint32_t installed{}, status{};
    // The pause menu's first pages built with VR VIEW on them, and the title
    // screen's main menus shown (OnSaveDataLoaded).
    uint64_t pages{}, mainMenus{};
    // The first-time setup: its state now, the Settings built with it alone,
    // and why it fell back to every tab (9505: the game's tabs could not be
    // taken out of the list).
    Setup setup{};
    SetupFlow setupFlow{};
    uint32_t setupBuilds{}, setupStatus{};
    // The title screen's Settings built with VR SETUP's page in them.
    uint64_t titleBuilds{};
    // The title screen's updates (each frame it shows), and the title flow's
    // opening of its Settings: 0 idle, 1 waiting for the main menu, 2 SETTINGS
    // clicked, 3 open, 4 failed (setupStatus says why: 9520 no SETTINGS in the
    // menu, 9521 the click could not be made, 9522 no Settings after three
    // clicks, 9523 the game's code faulted).
    uint64_t titleUpdates{};
    uint32_t titleOpen{}, titleClicks{};
};
Telemetry telemetry();
} // namespace spidy::game_menu
