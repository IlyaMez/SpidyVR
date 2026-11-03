#pragma once
#include "prompt_buttons.hpp"
#include <cstdint>
#include <string_view>

// Research (October 11), not part of a VR session yet: how the game's button
// prompts get their icons, to show the VR controllers' own controls where the
// game shows the virtual Xbox controller's (its LB, RB, X and triggers are
// none of the VR controllers' in play). Image offsets in the supported
// Spider-Man.exe. Found so far:
//
// - The icons are fixed: 203 icon ids (their names at 5ec64d0: PSButton...,
//   XBButton..., SteamButton..., SwitchProButton..., GenericButton...,
//   MkbKeyboard..., MkbMouse..., gadgets, emoji), and one icon map a device
//   family in the UI's boot config (X360IconMap, XOneIconMap,
//   DualShockIconMap, DualSenseIconMap, SteamDeckIconMap, SwitchProIconMap,
//   GenericIconMap, MkbIconMap, SteamIconMap), each entry an IconId with a
//   Texture or a VectorIconName, Width, Height and Vspace. There is no set
//   for a VR controller, and none of the ids is a grip.
// - Which family shows is Flash's choice, from what the game tells every
//   movie when the device in use changes: `_root.SetInputDeviceVariables`
//   (device, a flag, the pad's kind), sent from 1d2d850 to each of the 32
//   movies at 7be3e30, the device from 16c2c60 and the kind from 16c2c40.
// - A text handed back by Flash with markup in it goes to the game's markup
//   resolver: Flash's `resolveMarkupForTextField` callback (dispatcher
//   2104cc0), the object at 7be5ba0, slot 5 of its table, 802a70 (self, text
//   field, text); slot 6 is `addImageToTextField`. Hooked in the running game,
//   no text passed it at the main menu or in play, with the hint line
//   "LB + RB (Hold) Throw" and an LB/RB world marker on screen: those do not
//   come this way.
// - The game's calls into Flash through the UI's wrapper (1d1cf30, which
//   game_menu hooks in a VR session) in that scene: PingPOIs,
//   POIInteractAvailable(3, 1, ''), AddPOI, UpdatePOI, ShowText and HideText
//   (subtitles), the fades and the saving and loading icons. A world marker
//   is a POI of a kind; its icon is not in the call. The hint line is in none
//   of them.
//
// Found on October 24: the hint line is the HUD's own prompt list. Its
// manager (e36300, 50 slots) looks each line's text up in the localized
// texts as it appears - "[ACTION_WEB_YANK] (Hold) Throw" - and hands it to
// the list's widget (show 7793d0, hide 77a1e0, 21 kinds of line), which
// resolves the tokens itself; a text changed in the lookup before the line
// appears is what the line draws. The LB/RB world marker is a POI (AddPOI's
// third argument 1 for a throw target) whose labels are Flash's own.
namespace spidy::game_prompts {
// Hooks the resolver. 0 when hooked; 9601 while the game's UI has none yet
// (try again), 9602-9603 when what is there is not this game's, 9700-9799
// when hooking failed.
uint32_t install(uintptr_t base);
void uninstall();
// The lines the research hooks kept, newest last (texts that passed the
// resolver; with SpidyPromptsCalls the calls into Flash, one line a name;
// with SpidyPromptsTexts the localized texts with markup, one a tag), and the
// research rule: a text for the resolver that holds `find` passes with it
// replaced by `replace`.
constexpr unsigned traceEntries = 48, traceChars = 256, ruleChars = 96;
struct Trace {
    uint32_t magic = 0x53505254, version = 1, bytes = sizeof(Trace), status{};
    uint64_t resolved{}, rewritten{};
    // The resolver's function (image offset), once hooked.
    uint64_t function{};
    uint32_t count{}, reserved{};
    char texts[traceEntries][traceChars]{};
};
struct Rule {
    char find[ruleChars]{}, replace[ruleChars]{};
};
static_assert(sizeof(Trace) == 48 + traceEntries * traceChars && sizeof(Rule) == 2 * ruleChars);

// ---- What the game is asking for, read in a VR session (October 13) ----
//
// The research above asked where a prompt's icons come from. For a session
// the useful question is narrower: which button is the game asking for right
// now? It names one through the UI's own interface, and the names are
// literals of the image, so a pointer comparison is the whole cost:
//
//   placeablePromptManager  ShowPlaceablePrompt, HidePlaceablePrompt,
//                           SetPlaceablePromptProgress (the door, the vent,
//                           the grate; ui/export/HUD/PlaceablePrompts.gfx)
//   qteManager              AddQTE(button, subtype, miscVal, miscValTwo),
//                           UpdateQTE, ClearQTE (ui/export/HUD/QTEs.gfx)
//   the world markers       POIInteractAvailable, UpdateInWorldPrompt
//
// All of them pass the one call wrapper at 0x1d1cf30, and their texts the
// localized lookup at 0x1749970. A VR session already hooks both for the
// SPIDY VR tab (game_menu), and MinHook takes a target once, so game_menu
// hands the calls and the texts on to the two functions here instead of this
// hooking them a second time (the research exports do that, and so are only
// for a game with no session running).
//
// What AddQTE's `button` argument holds is not known: no trace has run while
// a prompt was up. So the demand comes from the text, which spells its
// buttons out ("LB + RB (Hold) Throw") and carries their icon ids, and the
// calls say when a prompt is up and when it is gone, so a demand cannot
// outlive its prompt. A session that reads nothing is no worse off than
// before: reading only lets the controls of the names the game shows send
// those buttons (game_pad::NamedPress).
//
// A demand stays live for this long after the last text that named it or
// call that raised it; its manager's Hide or Clear ends it at once. A text
// shown on a channel of its own (ShowText: help lines, subtitles) is the
// exception: what it names stands until HideText of that channel or another
// text on it (prompt_buttons::LiveDemands, which keeps both).
constexpr uint32_t liveMs = static_cast<uint32_t>(prompt_buttons::LiveDemands::liveMs);
// Whether this session reads the prompts at all (the launcher's
// --no-prompt-help turns it off). Reading is NOT limited to VR play: the game
// looks a prompt's text up when its UI builds it, which happens while it owns
// the screen, and the October 13 session - which only read during play - saw
// not one text (`named` 0). Reading everywhere is also how that session's
// question gets answered: if `named` stays 0 the localized lookup is not the
// route a prompt's text takes, as the markup resolver was not on October 11.
// Whether the UI's calls are looked at at all (the census, the timeline and a
// prompt call's text). Every session since October 20, unless the launcher's
// --no-prompt-help says otherwise: see the note in game_xr.
void setObserving(bool);
void setReading(bool);
// Whether a text may be rewritten with the VR controls. Only in VR play: on
// the game screen and in the menus the game's own button names are the right
// ones, and its KEY MAPPING tab lists them.
void setRewriting(bool);
// A call of the game's into Flash, from game_menu's hook on the wrapper.
// `name` is the pointer the game passed; `imageBase` the module's base;
// `args` its Scaleform values (game_menu's GfxValue, the same layout) and
// `count` how many.
void observeCall(uintptr_t imageBase, const char* name, const void* args, uint32_t count);
// A call the same hook makes otherwise in VR play (while rewriting): the
// throw marker the HUD sets over a prop, AddPOI of an interact point with the
// throwable's subtype, takes the left face button's instead and draws X, the
// button that sends the game's LB + RB there (prompt_buttons::markerSubtype).
// Fills `copy`, room for `count` values, with the call to make and returns
// true; false to make the call as it came. The copy is the caller's values
// as they are, nothing taken or released, for the length of the one call.
// Counted with the rewritten texts (Live::rewritten).
bool rewriteCall(uintptr_t imageBase, const char* name, const void* args, uint32_t count, void* copy);
// The eight names of the prompt and QTE interface, and the room kept for the
// newest of their calls and for the census of every name the game uses.
constexpr unsigned callNames = 10, callLines = 32, callChars = 128;
constexpr unsigned censusNames = 48, censusChars = 160;
// What the game's UI actually calls, so the next session answers this without
// another guess.
//
// October 13, the first session that read anything (224 s, report
// game-vr-20261013-141354): 630 calls into Flash, 107 of them these eight
// names - so the names are right - and **not one prompt text reached the
// localized lookup** (`named` 0) although reading was on throughout VR play
// and the player sees the game's prompts there. So a prompt's text does not
// come that way, any more than it came through the markup resolver on October
// 11, and what a prompt asks for has to be read from these calls.
//
// Hence two lists, both for the session report, neither read for input yet:
// the newest prompt calls with their arguments, and a census of every name
// the game calls with one example of its arguments. The census is the part
// that needs no guess: if the text or the button arrives in a call this does
// not yet know about, the census names it. AddQTE's arguments are called
// `button`, `subtype`, `miscVal` and `miscValTwo` in the game's own UI, and
// POIInteractAvailable's third was a string.
struct Calls {
    // The newest prompt calls, oldest first, and how often each of the eight
    // names came.
    uint32_t count{}, perName[callNames]{};
    char line[callLines][callChars]{};
    // Every distinct name the game called, in the order first seen: how many
    // times, and one call of each with its arguments.
    uint32_t names{}, calls[censusNames]{};
    char example[censusNames][censusChars]{};
};
void recentCalls(Calls&);
// A line of this session's own, put in the same list as the game's calls so
// the report reads as one timeline: what the game asked for and what the
// player pressed, in order, each stamped with the milliseconds since the
// first call. This is how to tell whether the game thought an interaction was
// available at the moment a button arrived - the question left after October
// 13, when the game announced an interact 8 times, took 12 presses of its own
// Y from a wall, and did nothing.
// Since v4 each line goes into the session's diagnostics as well (diag_log:
// the launcher's console and reports/<session>-diag.log), whole, unless
// `diagnostics` is false (WEB AIM's "forearm aim:", which goes there only as
// it changes).
void note(const char* text, bool diagnostics = true);
// One of the game's localized texts, from game_menu's hook on the lookup.
// Returns the text to show in its place: the same pointer, or in VR play
// (setRewriting) a kept copy where the text names the game's LB + RB, with
// them as [BTN_X] (prompt_buttons::pairOnX) - X sends them there
// (game_pad::NamedPress), and the game then draws its own X in the prompt.
// The copy is the game's to read for the rest of the session; what it named
// before stays known to the readers here, so it still asks for LB + RB.
const char* observeText(const char* text);
// The HUD's prompt list (the throw's "[ACTION_WEB_YANK] (Hold) Throw" on a
// street, "[BTN_Y] Interact" by a passer-by), from game_menu's hooks on its
// widget: a line of `kind` (0-20, the widget's own) shown with `text`, which
// is what it asks for until hidden. Returns the text to show, as
// observeText does: a line the lookup already gave its copy is that copy.
const char* promptShown(unsigned kind, const char* text);
void promptHidden(unsigned kind);
// What the game is asking for now; {} for nothing (no prompt, one whose
// button could not be read, or a session that is not reading).
prompt_buttons::Demand demand();
struct Live {
    // The game's calls into Flash, and those of the prompt and QTE interface
    // among them: the ones that raised a prompt and the ones that took one
    // away. `calls` at 0 with `invokes` rising says the names are wrong.
    uint64_t invokes{}, calls{}, raised{}, cleared{};
    // Texts that named a button, and texts rewritten for a VR player (with
    // the throw markers drawn as X since October 24: rewriteCall).
    uint64_t named{}, rewritten{};
    // The demand now and the last one named (prompt_buttons::pack).
    uint32_t demand{}, lastDemand{};
    // Demands named that VR play could not have sent on its own - what the
    // interact button now carries - and texts that came with the keyboard's
    // or the mouse's icons, which say nothing about a pad button (the game
    // shows those when it last saw that device, and a player then reads
    // "click f to decipher" with no key to press).
    uint32_t beyondPlay{}, keyboardTexts{};
};
Live live();
// Everything the live reading knows, for a probe to read out of the game
// without a VR session: `SpidyPromptsWatch` turns the reading on (the menu's
// hooks carry it, so SpidyMenuStart must have run) and `SpidyPromptsWatched`
// copies this out. That is how the prompts can be measured in the game with
// no headset, which is how this should have been tested from the start.
struct Watched {
    uint32_t magic = 0x5750524d, version = 1, bytes = sizeof(Watched), reading{};
    Live live{};
    Calls calls{};
};
// The probe reads this by offset, so its shape is pinned here: Live at 16
// (six counters then four words) and Calls at 80 (count, perName[10],
// line[32][128], names, calls[48], example[48][160]).
static_assert(sizeof(Live) == 64 && sizeof(Calls) == 12016 && sizeof(Watched) == 12096);
} // namespace spidy::game_prompts
