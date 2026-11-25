// The VR category tabs in the game's own Settings (game_menu.hpp). Addresses are
// image offsets in the supported Spider-Man.exe; docs/REFERENCE.md lists them.
//
// The game's Settings are Scaleform (Flash) screens fed by its settings system
// (UISettingsMenuSystem) from configs/uiconfig/uisystemmenu.config: tabs of
// items, each naming one of the game's 123 settings by number. Opening them,
// the pause menu builds every tab's Flash object with its own row code and
// hands the list to Flash (OpenOptions). Spidy builds one more tab with that
// same code, from items copied from the game's own, and adds it to the list;
// its items name settings 0x200 and up, which only Spidy answers for.
#include "spidy/game_menu.hpp"
#include "spidy/game_prompts.hpp"
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <intrin.h>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_menu;
using vr_settings::Item;

// A probe's record of the menus' Flash calls and callbacks (SpidyMenuTrace),
// off in a session: what a menu sends Flash and what Flash answers, to find
// the game's own menus by.
struct MenuTraceEntry {
    uint32_t kind{}, hash{}, count{}, pad{}; // kind 1 a call into Flash, 2 a callback from it
    uint64_t ui{}, caller{}; // the code that called (its image offset)
    char name[48]{};
    uint32_t types[4]{};
    uint64_t values[4]{};
    char texts[4][24]{};
};
struct MenuTrace {
    uint32_t magic = 0x54524d53, version = 1, bytes = sizeof(MenuTrace), on{};
    uint32_t next{}, pad{};
    MenuTraceEntry entries[256]{};
};
static_assert(sizeof(MenuTraceEntry) == 224 && sizeof(MenuTrace) == 24 + 256 * 224);
extern "C" {
__declspec(dllexport) MenuTrace SpidyMenuTraceData;
}

namespace {
// The settings system; +0x20 its config, whose tabs are at +0x168 (0x40 bytes
// each, a UISystemMenu) and +0x170 counts them.
constexpr uintptr_t settingsSystem = 0x5d96dd0;
// The pause menu's Settings build every tab and call Flash's OpenOptions.
constexpr uintptr_t openOptions = 0x7dd9f0;
// A tab's Flash object, rows and all: (out value, movie, tab) -> built.
constexpr uintptr_t buildTab = 0x807740;
// One item's row onto a Flash array, the tabs' rows (807740) and the pause
// menu's first page alike: (array, movie, item, flag). The first page
// (7dd7c0, the menu at the settings config's +0x128, its items at +0x158, as
// many as +0x160 says) adds its items in a loop; this is where its call
// returns to.
constexpr uintptr_t addItem = 0x80a220, pageItemReturn = 0x7dd8ae, pageItems = 0x158;
// The first page's own build (7dd7c0: its items, then Flash's Refresh), and
// the callback Flash sends when A selects one of its items, with the item's
// number (+8) - for VR VIEW, Spidy's setting number. The page shows its items
// as buttons, so VR VIEW is one: A switches the view, and its name says what
// A does next.
constexpr uintptr_t pageRefresh = 0x7dd7c0;
constexpr uint32_t pageSelectHash = 0xd0dbcfa5;
constexpr unsigned char pageRefreshBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                                              0x24, 0x10, 0x48, 0x89, 0x7c, 0x24, 0x18, 0x55};
const char* const pageTitles[] = {"SWITCH TO GAME SCREEN", "SWITCH TO VR"};
// The first-time setup's pause flow (beginSetup; unused since the worker's pad presses were dropped, November
// 23). In the pause menu, A on SETTINGS (Select 5, 7de150) builds and opens its Settings (7dd9f0) once Flash
// has moved to them, and the Settings get the setup's tab alone. Building them from native code alone (the screen's +0x174 countdown,
// tried November 22) fills them without Flash's move: half the page stays over them. CloseMenu (7dd270,
// what Select 0, RESUME, does) has Flash close the pause menu and play go on. Flash's CloseOptions callback
// (B in the Settings) and its close (the menu closed) arrive with the hashes at these addresses, set at
// start-up.
constexpr uintptr_t closeMenu = 0x7dd270;
constexpr unsigned char closeMenuBytes[] = {0x48, 0x83, 0xec, 0x48, 0x33, 0xc0, 0x48, 0x8d, 0x15};
constexpr uintptr_t closeOptionsHash = 0x6d8ed7c, closeHash = 0x6d8ed40;
// The setup's Settings: one tab, its heading row saying how to leave it.
const char* const setupTitle = "SET UP SPIDY VR";
const char* const setupNote = "PRESS B TWICE TO START PLAYING";
// The title screen's Settings (the main menu's SETTINGS): one screen whose pages are built by 7aa9d0 (and
// listed again by 7ad940), page 1 its root (the root menus at the config's +0x88/+0xc0/+0xf8, by platform,
// then +0x110: language rows, then an entry per page, GAME and the rest), any other page the config's tab of
// that id. A root menu: +8 its entries (0x18 bytes: +8 the item, +0x10 its type, +8 there 0xa37411c7 for an
// item), +0x10 their count. A page's entry is an item whose option (+0x48, type submenuOption) holds the
// page's id at +8. Both builds get the config from 72dc70 (mov rax, [rcx+0x20]; ret); asked from them, Spidy
// answers with a copy of it whose tabs end with VR SETUP's and whose root has a SPIDY VR SETUP entry before
// GAME: a page of the title's Settings like the game's own, with the same rows as the pause menu's VR SETUP.
constexpr uintptr_t titleBuild = 0x7aa9d0, configGetter = 0x72dc70;
constexpr uintptr_t titleBuildReturn = 0x7aaa62, titleListReturn = 0x7ad957;
constexpr unsigned char titleBuildBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10};
constexpr unsigned char configGetterBytes[] = {0x48, 0x8b, 0x41, 0x20, 0xc3};
constexpr uintptr_t submenuOption = 0x3a7d7c8, titleRoot = 0x110, configTabs = 0x168, configTabCount = 0x170;
constexpr size_t configBytes = 0x180, entryBytes = 0x18, optionBytes = 0x10, titleMost = 64;
constexpr uint32_t itemEntryType = 0xa37411c7;
// The title Settings' screen: its page (+0x4c8; 1 the root), the cursor it opens at (+0x4cc), and the way
// back, OptionsAccept's stack of {page, cursor} (8 a level) from +0x484, its top's index at +0x4c4 (-1
// empty, 7 at most): what B (OptionsBack) goes back to. And Flash's OnSaveDataLoaded, which the title calls
// as a profile's main menu comes up (7ad240).
constexpr uintptr_t titlePage = 0x4c8, titleCursor = 0x4cc, titleBackTop = 0x4c4, titleBack = 0x484;
constexpr int titleBackMost = 8;
constexpr uintptr_t saveDataLoadedName = 0x389b348;
// The title screen's callbacks from Flash (7a8980: ui, name hash, arguments, count). B on the Settings' first
// level closes them (OnOptionsClose, its hash at this address): a refresh that leaves the page as it is.
// Opened on VR SETUP's page, the Settings close from it, so Spidy puts the root back (and the way back it
// added), or the title's Settings would open on that page alone from then on.
constexpr uintptr_t titleCallback = 0x7a8980, titleCloseHash = 0x6d88414;
constexpr unsigned char titleCallbackBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18};
// The title screen's update (7a8320, its class's vtable at 389b0c0, +0x18): each frame the title shows, on
// the thread that calls into its Flash. The first-time setup's title flow opens the title's Settings from
// there, as a mouse click on SETTINGS would (TitleMenu.OnMouseEvent(CLICK, index, 0), Flash's own public
// way in: on the selected item of its main menu it presses it, and on SETTINGS that opens the Settings),
// once the menu says it takes input: GetMode 1 (MODE_LOBBY), not GetAnimating, GetAllowOptionInput, not
// GetOpeningSave. Earlier presses on the virtual pad were lost while the menu came in and pressed QUIT
// GAME instead (2026-11-23). SETTINGS' place in the menu: the main menu's list as 7aa7a0 makes it, the
// lobby config's items (config [5d96dd0+0x20] +0x28: items at +0x30, 0x88 bytes each, count +0x38) that
// 80cdc0 shows, in order; SETTINGS has id 58 (TitleMenu's OPTID_OPTIONS) at +8. GetMode 6 (MODE_OPTIONS)
// after the click: the Settings are open. The MouseEvent is made in the UI movie (uiMovie, the title's
// too: its host's movie getter jumps there) by the movie's CreateObject (319f7d0).
constexpr uintptr_t titleUpdate = 0x7a8320, titleVtableUpdate = 0x389b0d8;
constexpr unsigned char titleUpdateBytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10};
constexpr uintptr_t lobbyConfig = 0x5d96dd0, itemShown = 0x80cdc0, createObject = 0x319f7d0;
constexpr unsigned char itemShownBytes[] = {0x8b, 0x49, 0x08, 0xe9};
constexpr unsigned char createObjectBytes[] = {0x48, 0x8b, 0x49, 0x18, 0x4c, 0x8b, 0x11, 0x49, 0xff, 0xa2, 0x70, 0x01};
constexpr uint32_t optionsId = 58, lobbyMode = 1, optionsMode = 6;
const char* const titleEntryTitle = "SPIDY VR SETUP";
// The entry's setting number: one of its own, as the title's page switch (in 7a8980) takes the last item of
// the page with the number Flash sends. The settings system's getters answer for it as for the page entry it
// was copied from (titleEntryAs; GAME's 0x33).
const char* const titleEntryHelp = "Spidy's VR choices: play style, web button, comfort, turning and your body.";
// The UI movie Flash objects are made in.
constexpr uintptr_t uiMovie = 0x1d29690;
// "OpenOptions", the literal openOptions passes; the hash Flash's
// ResetAllOptionsForCurrentOptionsMenu callback arrives with (set at start-up).
constexpr uintptr_t openOptionsName = 0x389f4c0, resetAllHash = 0x6d8ed88;
// The option types of a setting item and of a section heading (vtables).
constexpr uintptr_t settingType = 0x3a7d6e8, headingType = 0x3a7d838;
// The tab's id: the game's run 0-10, and its RESET ALL ignores any above.
constexpr uint32_t tabId = 93; // first VR category; one native tab per category
constexpr size_t screenCount = vr_settings::categoryCount;
// The rows' setting numbers: the game's run 0-122.
constexpr int firstSetting = 0x200;
constexpr size_t tabBytes = 0x40, itemBytes = 0x88, choiceBytes = 0x50;
using vr_settings::maxChoices;
constexpr size_t rowCount = std::tuple_size_v<std::remove_cvref_t<decltype(vr_settings::rows())>>;

// A Scaleform GFx::Value as the game lays it out: 16 bytes of 0xff, the
// object interface (for objects and arrays), the type, the data.
struct ObjectInterface {
    void** vtable;
};
struct GfxValue {
    unsigned char head[16];
    ObjectInterface* objects;
    uint32_t type, reserved;
    uint64_t data, extra;
};
static_assert(sizeof(GfxValue) == 0x30);
constexpr uint32_t typeMask = 0x8f, managedType = 0x40, intType = 3, arrayType = 9;
void releaseValue(GfxValue& v) {
    if ((v.type & managedType) && v.objects)
        reinterpret_cast<void(__fastcall*)(ObjectInterface*, GfxValue*, uint64_t)>(v.objects->vtable[2])(
            v.objects, &v, v.data);
}
bool pushBack(const GfxValue& array, const GfxValue& v) {
    return reinterpret_cast<bool(__fastcall*)(ObjectInterface*, uint64_t, const GfxValue*)>(
        array.objects->vtable[15])(array.objects, array.data, &v);
}

// Every hook, with the bytes its function starts with in the supported game.
using InvokeFn = bool(__fastcall*)(void*, const char*, GfxValue*, uint32_t, uint64_t, float, uint32_t, uint64_t);
using CallbackFn = void(__fastcall*)(void*, uint32_t, GfxValue*, uint32_t);
using TypeFn = int(__fastcall*)(void*, int);
using ValueFn = float(__fastcall*)(void*, int);
using ActiveFn = bool(__fastcall*)(void*, int);
using SetFn = void(__fastcall*)(void*, int, float, bool, bool);
using AddItemFn = bool(__fastcall*)(GfxValue*, void*, const void*, bool);
using ResetFn = void(__fastcall*)(void*, int, bool);
using TextFn = const char*(__fastcall*)(uint32_t, const char*);
using HasTextFn = bool(__fastcall*)(uint32_t);
InvokeFn originalInvoke{};
CallbackFn originalCallback{};
TypeFn originalType{}, originalDefaultChoice{};
ValueFn originalValue{}, originalDefault{};
ActiveFn originalActive{};
SetFn originalSet{};
ResetFn originalReset{};
TextFn originalText{};
HasTextFn originalHasText{};
AddItemFn originalAddItem{};

uintptr_t imageBase{};
SRWLOCK lock = SRWLOCK_INIT;
vr_settings::Values shown;
// A bit per Item changed in the tab and not taken yet, one for every value an
// Item can have: a 32-bit mask overflowed once the haptics' groups took Item
// past 31 (x86 wrapped the shift onto another setting's bit), and a 64-bit
// one ended at Item 63 (WEB TIE is 64).
constexpr size_t itemBits = size_t{1} << (8 * sizeof(Item));
using ItemBits = std::bitset<itemBits>;
ItemBits pending;
static_assert(static_cast<size_t>(vr_settings::lastItem) < itemBits, "more settings than pending's bits");
std::atomic<uint64_t> tabsBuilt{}, changesMade{};
std::atomic<uint32_t> failure{};
std::atomic<bool> hooked{}, created{};
// The tab and its items: 0 not built yet, 1 ready, 2 cannot be built.
std::atomic<int> built{};
alignas(16) unsigned char tab[screenCount][tabBytes];
alignas(16) unsigned char items[rowCount][itemBytes];
// The tab's own items (its Place's rows), and the row on the first page.
alignas(16) unsigned char tabItems[screenCount][rowCount][itemBytes];
size_t tabRows[screenCount]{};
int pageRow = -1;
std::atomic<uint64_t> pagesBuilt{};
// The page's own build is this game's (checked at install): without it VR
// VIEW stays off the page.
bool pageReady{};
alignas(16) unsigned char choices[rowCount][maxChoices][choiceBytes];
// The first-time setup: the code it relies on is this game's (checked at install), its state, the pause
// screen it opened on, and its tab: the setup category's rows under a heading of its own.
bool setupReady{};
std::atomic<Setup> setupState{Setup::off};
std::atomic<SetupFlow> setupFlow{SetupFlow::pause};
std::atomic<uint64_t> mainMenusShown{};
std::atomic<uint32_t> setupBuilt{}, setupFailure{};
void* setupUi{};
constexpr auto setupCategory = static_cast<size_t>(vr_settings::Category::setup);
alignas(16) unsigned char setupTab[tabBytes];
alignas(16) unsigned char setupItems[rowCount + 1][itemBytes];
size_t setupRows{};
// The title screen's Settings: the code is this game's (checked at install), and the copy of the config
// they get (titleConfig), its tabs and its root's entries, Spidy's entry among them with its item and option.
bool titleReady{};
std::atomic<uint64_t> titleBuilds{};
constexpr int titleEntrySetting = firstSetting + static_cast<int>(rowCount);
std::atomic<int> titleEntryAs{-1};
alignas(16) unsigned char titleConfig[configBytes];
alignas(16) unsigned char titleTabs[titleMost + 1][tabBytes];
alignas(16) unsigned char titleEntries[titleMost + 1][entryBytes];
alignas(16) unsigned char titleItem[itemBytes];
alignas(16) unsigned char titleOption[optionBytes];

// The tab's texts, by the hashes the game looks texts up with: the tab's
// name and heading, each row's title and help, each list's choices. Each is
// the CRC-32 of a tag of Spidy's own. They answer only while Spidy builds its
// tab (buildingTab), when the game turns them into the tab's Flash strings, so
// a game text that shares a hash with one of them is never replaced.
struct Text {
    char tag[24];
    uint32_t hash;
    const char* text;
};
std::array<Text, screenCount * 2 + rowCount * 2 + rowCount * maxChoices + 4> texts{};
size_t textCount{};
// Indices into texts, sorted by hash.
std::array<uint16_t, std::size(texts)> byHash{};
uint32_t crc32(std::string_view s) {
    uint32_t c = 0xffffffff;
    for (const unsigned char ch : s) {
        c ^= ch;
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xedb88320 & (0u - (c & 1)));
    }
    return ~c;
}
const Text* addText(const char* tag, const char* text) {
    Text& t = texts[textCount];
    strncpy_s(t.tag, tag, _TRUNCATE);
    t.hash = crc32(t.tag);
    t.text = text;
    byHash[textCount] = static_cast<uint16_t>(textCount);
    ++textCount;
    return &t;
}
const Text *tabName[screenCount]{}, *tabHeading[screenCount]{};
const Text *setupName{}, *setupHeading{}, *titleName{}, *titleHelp{};
const Text *titles[rowCount]{}, *helps[rowCount]{};
// The first page's button's name, which says what A does (pageTitles).
Text* pageTitle{};
const Text* choiceTexts[rowCount][maxChoices]{};
void addTexts() {
    if (textCount)
        return;
    char tag[24];
    for (size_t s = 0; s < screenCount; ++s) {
        sprintf_s(tag, "SPIDY_VR_TAB_%zu", s);
        tabName[s] = addText(tag, vr_settings::screens()[s].tabTitle);
        sprintf_s(tag, "SPIDY_VR_HEAD_%zu", s);
        tabHeading[s] = addText(tag, vr_settings::screens()[s].tabTitle);
    }
    setupName = addText("SPIDY_VR_SETUP", setupTitle);
    setupHeading = addText("SPIDY_VR_SETUP_NOTE", setupNote);
    titleName = addText("SPIDY_VR_TITLE", titleEntryTitle);
    titleHelp = addText("SPIDY_VR_TITLE_HELP", titleEntryHelp);
    for (size_t r = 0; r < rowCount; ++r) {
        const auto& row = vr_settings::rows()[r];
        sprintf_s(tag, "SPIDY_VR_%zu", r);
        titles[r] = addText(tag, row.title);
        if (row.place == vr_settings::Place::page)
            pageTitle = const_cast<Text*>(titles[r]);
        if (row.help) {
            sprintf_s(tag, "SPIDY_VR_%zu_HELP", r);
            helps[r] = addText(tag, row.help);
        }
        for (size_t k = 0; k < row.choices.size() && k < maxChoices; ++k) {
            sprintf_s(tag, "SPIDY_VR_%zu_%zu", r, k);
            choiceTexts[r][k] = addText(tag, row.choices[k]);
        }
    }
    std::sort(byHash.begin(), byHash.begin() + static_cast<std::ptrdiff_t>(textCount),
              [](uint16_t a, uint16_t b) { return texts[a].hash < texts[b].hash; });
}
const char* textFor(uint32_t hash) {
    const auto end = byHash.begin() + static_cast<std::ptrdiff_t>(textCount);
    const auto at =
        std::lower_bound(byHash.begin(), end, hash, [](uint16_t i, uint32_t h) { return texts[i].hash < h; });
    return at != end && texts[*at].hash == hash ? texts[*at].text : nullptr;
}

template <class T> void put(unsigned char* at, size_t offset, T value) {
    std::memcpy(at + offset, &value, sizeof(T));
}
template <class T> T get(uintptr_t at) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof(T));
    return value;
}
// A tag the game keeps beside its hash: the tag, its length, the hash
// (LocString: +0 pointer, +8 length, +0xc hash).
void putText(unsigned char* at, size_t offset, const Text* t) {
    put<uint64_t>(at, offset, t ? reinterpret_cast<uint64_t>(t->tag) : 0);
    put<uint32_t>(at, offset + 8, t ? static_cast<uint32_t>(std::strlen(t->tag)) : 0);
    put<uint32_t>(at, offset + 0xc, t ? t->hash : 0);
}
// The tab and its items, copied from a tab, a setting and a heading of the
// game's config. Runs on the game's main thread, as its Settings do.
uint32_t buildItems() {
    __try {
        const auto config = get<uintptr_t>(imageBase + settingsSystem + 0x20);
        if (!config)
            return 9501;
        const auto tabs = get<uintptr_t>(config + 0x168);
        const auto tabCount = get<uint32_t>(config + 0x170);
        if (!tabs || !tabCount || tabCount > 64)
            return 9501;
        uintptr_t menu{}, setting{}, heading{};
        for (uint32_t t = 0; t < tabCount; ++t) {
            const uintptr_t at = tabs + t * tabBytes;
            const auto list = get<uintptr_t>(at + 0x30);
            const auto count = get<uint32_t>(at + 0x38);
            if (!list || !count || count > 256)
                continue;
            // The GAME tab, else the first with items: id, texts and items are replaced.
            if (!menu || get<uint32_t>(at + 8) == 4)
                menu = at;
            for (uint32_t i = 0; i < count; ++i) {
                const uintptr_t item = list + i * itemBytes;
                const auto option = get<uintptr_t>(item + 0x48);
                const auto type = option ? get<uintptr_t>(option) - imageBase : 0;
                // A setting with choices of its own: its first choice is the
                // choices' template too.
                if (!setting && type == settingType && get<uintptr_t>(item + 0x70) && get<uint32_t>(item + 0x78))
                    setting = item;
                if (!heading && type == headingType)
                    heading = item;
            }
        }
        if (!menu || !setting || !heading)
            return 9502;
        const auto choiceTemplate = get<uintptr_t>(setting + 0x70);
        for (size_t r = 0; r < rowCount; ++r) {
            const auto& row = vr_settings::rows()[r];
            unsigned char* item = items[r];
            std::memcpy(item, reinterpret_cast<const void*>(row.item == Item::none ? heading : setting), itemBytes);
            put<uint64_t>(item, 8, static_cast<uint64_t>(firstSetting + r));
            putText(item, 0x10, titles[r]);
            putText(item, 0x20, helps[r]);
            // No choices: the game's own OFF and ON.
            const auto count = static_cast<uint32_t>(std::min(row.choices.size(), maxChoices));
            put<uint64_t>(item, 0x70, count ? reinterpret_cast<uint64_t>(choices[r]) : 0);
            put<uint32_t>(item, 0x78, count);
            put<uint32_t>(item, 0x7c, count);
            for (uint32_t k = 0; k < count; ++k) {
                unsigned char* c = choices[r][k];
                std::memcpy(c, reinterpret_cast<const void*>(choiceTemplate), choiceBytes);
                // The name; no help or preview image of its own (the row's help shows).
                put<uint64_t>(c, 8, reinterpret_cast<uint64_t>(choiceTexts[r][k]->tag));
                put<uint32_t>(c, 0x10, static_cast<uint32_t>(std::strlen(choiceTexts[r][k]->tag)));
                put<uint32_t>(c, 0x14, choiceTexts[r][k]->hash);
                put<uint64_t>(c, 0x18, 0);
                put<uint64_t>(c, 0x20, 0);
                put<uint64_t>(c, 0x28, 0);
            }
        }
        // Stable setting numbers, grouped in short category tabs. Legacy section
        // headings stay in the catalog for compatibility but are not repeated here.
        pageRow = -1;
        for (size_t r = 0; r < rowCount; ++r)
            if (vr_settings::rows()[r].place == vr_settings::Place::page)
                pageRow = static_cast<int>(r);
        for (size_t s = 0; s < screenCount; ++s) {
            tabRows[s] = 0;
            for (Item wanted : vr_settings::screens()[s].items)
                for (size_t r = 0; r < rowCount; ++r)
                    if (vr_settings::rows()[r].item == wanted &&
                        vr_settings::rows()[r].place == vr_settings::Place::tab)
                        std::memcpy(tabItems[s][tabRows[s]++], items[r], itemBytes);
            std::memcpy(tab[s], reinterpret_cast<const void*>(menu), tabBytes);
            put<uint32_t>(tab[s], 8, tabId + static_cast<uint32_t>(s));
            putText(tab[s], 0x10, tabName[s]);
            putText(tab[s], 0x20, tabHeading[s]);
            put<uint64_t>(tab[s], 0x30, reinterpret_cast<uint64_t>(tabItems[s]));
            put<uint32_t>(tab[s], 0x38, static_cast<uint32_t>(tabRows[s]));
            put<uint32_t>(tab[s], 0x3c, static_cast<uint32_t>(tabRows[s]));
        }
        // The first-time setup's tab: the setup category's id (its RESET ALL), a name of its own, and a
        // heading row over its rows, copied from the catalog's first heading (a heading's setting number
        // answers as a heading) with the setup's note for its title.
        size_t headingRow = 0;
        while (headingRow < rowCount && vr_settings::rows()[headingRow].item != Item::none)
            ++headingRow;
        setupRows = 0;
        if (headingRow < rowCount) {
            std::memcpy(setupItems[setupRows], items[headingRow], itemBytes);
            putText(setupItems[setupRows], 0x10, setupHeading);
            ++setupRows;
        }
        std::memcpy(setupItems[setupRows], tabItems[setupCategory], tabRows[setupCategory] * itemBytes);
        setupRows += tabRows[setupCategory];
        std::memcpy(setupTab, tab[setupCategory], tabBytes);
        putText(setupTab, 0x10, setupName);
        putText(setupTab, 0x20, setupName);
        put<uint64_t>(setupTab, 0x30, reinterpret_cast<uint64_t>(setupItems));
        put<uint32_t>(setupTab, 0x38, static_cast<uint32_t>(setupRows));
        put<uint32_t>(setupTab, 0x3c, static_cast<uint32_t>(setupRows));
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 9501;
    }
}
bool ready() {
    if (!built) {
        const auto code = buildItems();
        failure = code;
        // A config not loaded yet is looked for again at the next opening.
        built = !code ? 1 : code == 9501 ? 0 : 2;
    }
    return built == 1;
}
// The game's main thread is building the SPIDY VR tab (or VR VIEW on the
// pause menu's first page).
thread_local bool buildingTab{};
// Each row's help: why it does nothing now, or what it does.
void updateHelps() {
    AcquireSRWLockShared(&lock);
    for (size_t r = 0; r < rowCount; ++r)
        if (helps[r]) {
            const auto& row = vr_settings::rows()[r];
            const char* reason = vr_settings::inactiveReason(row.item, shown);
            const_cast<Text*>(helps[r])->text = reason ? reason : row.help;
        }
    ReleaseSRWLockShared(&lock);
}
// One of Spidy's tabs built by the game's own code and added to the list Flash gets.
bool pushTab(GfxValue& tabs, void* movie, const void* menu) {
    GfxValue value{};
    std::memset(value.head, 0xff, sizeof(value.head));
    buildingTab = true;
    const bool made =
        reinterpret_cast<bool(__fastcall*)(GfxValue*, void*, const void*)>(imageBase + buildTab)(&value, movie, menu);
    buildingTab = false;
    const bool added = made && pushBack(tabs, value);
    releaseValue(value);
    return added;
}
// A Flash array's length, and its length set (Scaleform's ObjectInterface: GetArraySize, SetArraySize,
// beside PushBack at 15).
unsigned arraySize(const GfxValue& array) {
    return reinterpret_cast<unsigned(__fastcall*)(ObjectInterface*, uint64_t)>(array.objects->vtable[10])(
        array.objects, array.data);
}
bool resizeArray(const GfxValue& array, unsigned size) {
    return reinterpret_cast<bool(__fastcall*)(ObjectInterface*, uint64_t, unsigned)>(array.objects->vtable[11])(
        array.objects, array.data, size);
}
// The first-time setup's Settings: the game's tabs taken out of the list and the setup's tab alone in it.
// False, with the list as it was, when the game's tabs would not come out.
bool addSetupTab(GfxValue& tabs) {
    if (!ready())
        return false;
    __try {
        void* movie = reinterpret_cast<void*(__fastcall*)()>(imageBase + uiMovie)();
        const unsigned before = movie ? arraySize(tabs) : 0;
        if (!movie || before > 64 || !resizeArray(tabs, 0) || arraySize(tabs) != 0) {
            setupFailure = 9505;
            return false;
        }
        updateHelps();
        if (!pushTab(tabs, movie, setupTab)) {
            setupFailure = 9506;
            return false;
        }
        ++setupBuilt;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buildingTab = false;
        setupFailure = 9507;
        return false;
    }
}
// Adds the SPIDY VR tab, built by the game's own code, to the tabs the pause
// menu's Settings hand to Flash.
void addTab(GfxValue& tabs) {
    if (!ready())
        return;
    __try {
        void* movie = reinterpret_cast<void*(__fastcall*)()>(imageBase + uiMovie)();
        if (!movie)
            return;
        updateHelps();
        bool allMade = true;
        for (size_t s = 0; s < screenCount; ++s)
            if (!pushTab(tabs, movie, tab[s])) {
                allMade = false;
                failure = 9503;
                break;
            }
        // Count completed Settings builds, as before (the diagnostics protocol is unchanged).
        if (allMade)
            ++tabsBuilt;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Not again this session; the game's own tabs go on.
        buildingTab = false;
        failure = 9503;
        built = 2;
    }
}

// The pause menu's first page: under its first item (RESUME), VR VIEW, built
// by the same code as the page's own rows and answered as the tab's are.
bool __fastcall addItemHook(GfxValue* array, void* movie, const void* item, bool flag) {
    const bool made = originalAddItem(array, movie, item, flag);
    if (_ReturnAddress() != reinterpret_cast<void*>(imageBase + pageItemReturn) || !pageReady || !ready() ||
        pageRow < 0 || !pageTitle)
        return made;
    AcquireSRWLockShared(&lock);
    pageTitle->text = pageTitles[shown.flatScreen ? 1 : 0];
    ReleaseSRWLockShared(&lock);
    __try {
        const auto config = get<uintptr_t>(imageBase + settingsSystem + 0x20);
        if (!config || reinterpret_cast<uintptr_t>(item) != get<uintptr_t>(config + pageItems))
            return made;
        buildingTab = true;
        if (originalAddItem(array, movie, items[pageRow], false))
            ++pagesBuilt;
        buildingTab = false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buildingTab = false;
        failure = 9504;
    }
    return made;
}
// The pause menu's first page, built as the menu opens (7dbc80's last jump) and again when VR VIEW changes:
// with the pause flow armed, the player's SETTINGS opens its Settings, and they get the setup's tab alone
// (invokeHook).
using PageRefreshFn = void(__fastcall*)(void*);
PageRefreshFn originalPageRefresh{};
void __fastcall pageRefreshHook(void* ui) {
    originalPageRefresh(ui);
    auto armed = Setup::armed;
    if (!ui || !setupReady || setupFlow != SetupFlow::pause || !ready() ||
        !setupState.compare_exchange_strong(armed, Setup::opening))
        return;
    setupUi = ui;
}
// The title Settings' copy of the config (titleConfig): the real one's first bytes, its tabs with VR
// SETUP's after them, and its root's entries with Spidy's before the first page's (GAME), made from that
// one's entry, item and option, its title and help Spidy's and its page VR SETUP's id. The real config when
// any of it is not as expected.
uintptr_t titleConfigFor(uintptr_t config) {
    __try {
        const auto tabs = get<uintptr_t>(config + configTabs);
        const auto tabCount = get<uint32_t>(config + configTabCount);
        const auto entries = get<uintptr_t>(config + titleRoot + 8);
        const auto entryCount = get<uint32_t>(config + titleRoot + 0x10);
        if (!tabs || !tabCount || tabCount > titleMost || !entries || !entryCount || entryCount > titleMost)
            return config;
        uint32_t at = entryCount;
        for (uint32_t i = 0; i < entryCount && at == entryCount; ++i) {
            const uintptr_t entry = entries + i * entryBytes;
            const auto type = get<uintptr_t>(entry + 0x10);
            const auto item = get<uintptr_t>(entry + 8);
            if (!type || !item || get<uint32_t>(type + 8) != itemEntryType)
                continue;
            const auto option = get<uintptr_t>(item + 0x48);
            if (option && get<uintptr_t>(option) - imageBase == submenuOption)
                at = i;
        }
        if (at == entryCount)
            return config;
        const uintptr_t page = entries + at * entryBytes;
        const auto pageItem = get<uintptr_t>(page + 8);
        std::memcpy(titleItem, reinterpret_cast<const void*>(pageItem), itemBytes);
        titleEntryAs = static_cast<int>(get<uint32_t>(pageItem + 8));
        put<uint64_t>(titleItem, 8, static_cast<uint64_t>(titleEntrySetting));
        std::memcpy(titleOption, reinterpret_cast<const void*>(get<uintptr_t>(pageItem + 0x48)), optionBytes);
        put<uint32_t>(titleOption, 8, tabId + static_cast<uint32_t>(setupCategory));
        putText(titleItem, 0x10, titleName);
        putText(titleItem, 0x20, titleHelp);
        put<uint64_t>(titleItem, 0x48, reinterpret_cast<uint64_t>(titleOption));
        std::memcpy(titleEntries, reinterpret_cast<const void*>(entries), at * entryBytes);
        std::memcpy(titleEntries[at], reinterpret_cast<const void*>(page), entryBytes);
        put<uint64_t>(titleEntries[at], 8, reinterpret_cast<uint64_t>(titleItem));
        std::memcpy(titleEntries[at + 1], reinterpret_cast<const void*>(page), (entryCount - at) * entryBytes);
        std::memcpy(titleTabs, reinterpret_cast<const void*>(tabs), tabCount * tabBytes);
        std::memcpy(titleTabs[tabCount], tab[setupCategory], tabBytes);
        std::memcpy(titleConfig, reinterpret_cast<const void*>(config), configBytes);
        put<uint64_t>(titleConfig, configTabs, reinterpret_cast<uint64_t>(titleTabs));
        put<uint32_t>(titleConfig, configTabCount, tabCount + 1);
        put<uint64_t>(titleConfig, titleRoot + 8, reinterpret_cast<uint64_t>(titleEntries));
        put<uint32_t>(titleConfig, titleRoot + 0x10, entryCount + 1);
        ++titleBuilds;
        return reinterpret_cast<uintptr_t>(titleConfig);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        titleReady = false;
        return config;
    }
}
using ConfigFn = uintptr_t(__fastcall*)(void*);
ConfigFn originalConfig{};
uintptr_t __fastcall configHook(void* system) {
    const uintptr_t config = originalConfig(system);
    if (!titleReady || !config)
        return config;
    const auto from = reinterpret_cast<uintptr_t>(_ReturnAddress()) - imageBase;
    if ((from != titleBuildReturn && from != titleListReturn) || !ready())
        return config;
    return titleConfigFor(config);
}
// A page of the title's Settings built: Spidy's texts answer while it is (its entry, VR SETUP's rows).
using TitleBuildFn = uint64_t(__fastcall*)(void*, void*);
TitleBuildFn originalTitleBuild{};
// The title flow of the first-time setup: the root page about to be built is VR SETUP's instead, with the
// root and its top as the way back, as OptionsAccept leaves them for a page entry.
void openTitleSetup(void* screen) {
    __try {
        auto* at = static_cast<unsigned char*>(screen);
        if (get<int32_t>(reinterpret_cast<uintptr_t>(at + titlePage)) != 1)
            return;
        const int top = get<int32_t>(reinterpret_cast<uintptr_t>(at + titleBackTop));
        if (top < -1 || top + 1 >= titleBackMost)
            return;
        put<int32_t>(at, titleBackTop, top + 1);
        put<uint64_t>(at, titleBack + 8 * static_cast<size_t>(top + 1), uint64_t{1});
        put<int32_t>(at, titlePage, static_cast<int32_t>(tabId + setupCategory));
        put<int32_t>(at, titleCursor, 0);
        setupState = Setup::opening;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setupFailure = 9510;
        setupState = Setup::off;
    }
}
int titlePageOf(void* screen) {
    __try {
        return get<int32_t>(reinterpret_cast<uintptr_t>(screen) + titlePage);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
uint64_t __fastcall titleBuildHook(void* screen, void* data) {
    const bool title = titleReady && screen && ready();
    if (title) {
        updateHelps();
        if (setupFlow == SetupFlow::title && setupState == Setup::armed)
            openTitleSetup(screen);
    }
    buildingTab = true;
    const uint64_t result = originalTitleBuild(screen, data);
    buildingTab = false;
    // The setup shows while its page does; another page built after it (B back to the root) ends it.
    if (title && setupFlow == SetupFlow::title) {
        const bool setupPage = titlePageOf(screen) == static_cast<int>(tabId + setupCategory);
        const Setup state = setupState;
        if (setupPage && (state == Setup::opening || state == Setup::showing)) {
            setupState = Setup::showing;
            ++setupBuilt;
        } else if (!setupPage && state == Setup::showing) {
            setupState = Setup::done;
        }
    }
    return result;
}
void trace(uint32_t kind, void* ui, uint32_t hash, const char* name, const GfxValue* args, uint32_t count,
           void* caller);
// The title Settings closed on VR SETUP's page: the root again, the way back to it taken off, and the
// title flow of the setup done.
void restoreTitleRoot(void* screen) {
    __try {
        auto* at = static_cast<unsigned char*>(screen);
        const auto base = reinterpret_cast<uintptr_t>(at);
        if (get<int32_t>(base + titlePage) != static_cast<int32_t>(tabId + setupCategory))
            return;
        const int top = get<int32_t>(base + titleBackTop);
        if (top >= 0 && top < titleBackMost && get<int32_t>(base + titleBack + 8 * static_cast<size_t>(top)) == 1)
            put<int32_t>(at, titleBackTop, top - 1);
        put<int32_t>(at, titlePage, 1);
        put<int32_t>(at, titleCursor, 0);
        auto showing = Setup::showing;
        if (setupFlow == SetupFlow::title)
            setupState.compare_exchange_strong(showing, Setup::done);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setupFailure = 9511;
    }
}
using TitleCallbackFn = void(__fastcall*)(void*, uint32_t, GfxValue*, uint32_t);
TitleCallbackFn originalTitleCallback{};
void __fastcall titleCallbackHook(void* screen, uint32_t hash, GfxValue* args, uint32_t count) {
    trace(2, screen, hash, nullptr, args, count, _ReturnAddress());
    if (titleReady && screen && hash == get<uint32_t>(imageBase + titleCloseHash))
        restoreTitleRoot(screen);
    originalTitleCallback(screen, hash, args, count);
}
// The title flow's opening of the title's Settings (titleUpdate): waiting from the setup's arming, clicked
// once SETTINGS was clicked, opened when the Settings show. The title screen (the ui OnSaveDataLoaded came
// to), the frames the menu has taken input for, the frames since the click, and the clicks.
enum class TitleOpen : uint32_t { idle, waiting, clicked, opened, failed };
std::atomic<TitleOpen> titleOpen{TitleOpen::idle};
std::atomic<void*> titleUi{};
std::atomic<uint64_t> titleUpdates{};
bool titleUpdateReady{};
uint32_t titleSteady{}, titleSinceClick{}, titleClicks{};
using TitleUpdateFn = void(__fastcall*)(void*);
TitleUpdateFn originalTitleUpdate{};
GfxValue emptyValue() {
    GfxValue v{};
    std::memset(v.head, 0xff, sizeof(v.head));
    return v;
}
// A TitleMenu getter's answer (GetMode and the like): an int, or a bool as 0/1; -1 when it gave none.
int64_t titleGet(void* screen, const char* name) {
    GfxValue result = emptyValue();
    int64_t answer = -1;
    if (originalInvoke(screen, name, nullptr, 0, reinterpret_cast<uint64_t>(&result), 0.f, 0, 0)) {
        const uint32_t type = result.type & typeMask;
        if (type == 2)
            answer = (result.data & 0xff) ? 1 : 0;
        else if (type == intType)
            answer = static_cast<int32_t>(result.data);
        else if (type == 4)
            answer = static_cast<uint32_t>(result.data);
        else if (type == 5)
        {
            double number{};
            std::memcpy(&number, &result.data, sizeof(number));
            answer = static_cast<int64_t>(number);
        }
    }
    releaseValue(result);
    return answer;
}
// SETTINGS' place in the main menu's list (as 7aa7a0 lists it), or -1.
int settingsIndex() {
    const auto config = get<uintptr_t>(imageBase + lobbyConfig + 0x20);
    if (!config)
        return -1;
    const auto list = get<uintptr_t>(config + 0x28 + 0x30);
    const auto count = get<uint32_t>(config + 0x28 + 0x38);
    if (!list || count > 64)
        return -1;
    int index = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uintptr_t item = list + i * 0x88;
        if (!reinterpret_cast<bool(__fastcall*)(uintptr_t)>(imageBase + itemShown)(item))
            continue;
        if (get<uint32_t>(item + 8) == optionsId)
            return index;
        ++index;
    }
    return -1;
}
// SETTINGS clicked: the MouseEvent made in the UI movie and handed to TitleMenu.OnMouseEvent with SETTINGS'
// place. False when any of it was not there.
bool clickSettings(void* screen, int index) {
    void* movie = reinterpret_cast<void*(__fastcall*)()>(imageBase + uiMovie)();
    if (!movie)
        return false;
    GfxValue type = emptyValue();
    type.type = 6; // a string: the event's type
    type.data = reinterpret_cast<uint64_t>("click");
    GfxValue event = emptyValue();
    reinterpret_cast<void(__fastcall*)(void*, GfxValue*, const char*, const GfxValue*, uint32_t)>(
        imageBase + createObject)(movie, &event, "flash.events.MouseEvent", &type, 1);
    if ((event.type & typeMask) != 8 || !event.objects) {
        releaseValue(event);
        return false;
    }
    GfxValue args[3] = {event, emptyValue(), emptyValue()};
    args[1].type = intType;
    args[1].data = static_cast<uint32_t>(index);
    args[2].type = intType;
    GfxValue result = emptyValue();
    const bool called = originalInvoke(screen, "OnMouseEvent", args, 3, reinterpret_cast<uint64_t>(&result), 0.f, 0, 0);
    releaseValue(result);
    releaseValue(event);
    return called;
}
// Each frame of the title screen while the setup waits for its Settings: SETTINGS clicked once the main menu
// has taken input for a quarter of a second, then the Settings looked for; clicked again after two seconds
// without them, three times at most.
void openTitleSettings(void* screen) {
    if (screen != titleUi.load() || setupFlow != SetupFlow::title)
        return;
    const Setup state = setupState;
    if (state == Setup::off || state == Setup::done) {
        titleOpen = TitleOpen::idle;
        return;
    }
    // Its page is in the title's Settings once they were built with it (titleBuildHook).
    if (state != Setup::showing)
        return;
    __try {
        if (titleOpen == TitleOpen::clicked) {
            if (titleGet(screen, "GetMode") == optionsMode) {
                titleOpen = TitleOpen::opened;
            } else if (++titleSinceClick > 120) {
                titleSteady = 0;
                if (titleClicks >= 3) {
                    setupFailure = 9522;
                    titleOpen = TitleOpen::failed;
                } else {
                    titleOpen = TitleOpen::waiting;
                }
            }
            return;
        }
        const bool takesInput = titleGet(screen, "GetMode") == lobbyMode && titleGet(screen, "GetAnimating") == 0 &&
                                titleGet(screen, "GetAllowOptionInput") == 1 &&
                                titleGet(screen, "GetOpeningSave") == 0;
        titleSteady = takesInput ? titleSteady + 1 : 0;
        if (titleSteady < 15)
            return;
        const int index = settingsIndex();
        if (index < 0) {
            setupFailure = 9520;
            titleOpen = TitleOpen::failed;
            return;
        }
        ++titleClicks;
        titleSinceClick = 0;
        if (!clickSettings(screen, index)) {
            setupFailure = 9521;
            titleOpen = TitleOpen::failed;
            return;
        }
        titleOpen = TitleOpen::clicked;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setupFailure = 9523;
        titleOpen = TitleOpen::failed;
    }
}
void __fastcall titleUpdateHook(void* screen) {
    originalTitleUpdate(screen);
    ++titleUpdates;
    if (const TitleOpen now = titleOpen; now == TitleOpen::waiting || now == TitleOpen::clicked)
        openTitleSettings(screen);
}
// RESUME, as the page's own first item has it done.
void closePauseMenu(void* ui) {
    __try {
        reinterpret_cast<void(__fastcall*)(void*)>(imageBase + closeMenu)(ui);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setupFailure = 9509;
    }
}
// The row a setting number is, or -1 for the game's own settings.
int rowOf(int setting) {
    const int r = setting - firstSetting;
    return r >= 0 && r < static_cast<int>(rowCount) && built == 1 ? r : -1;
}
Item itemOf(int row) {
    return vr_settings::rows()[static_cast<size_t>(row)].item;
}
void change(Item item, int index) {
    if (item == Item::none)
        return;
    AcquireSRWLockExclusive(&lock);
    const bool changed = vr_settings::choose(item, index, shown);
    if (changed)
        pending.set(static_cast<size_t>(item));
    ReleaseSRWLockExclusive(&lock);
    if (changed)
        ++changesMade;
}

void trace(uint32_t kind, void* ui, uint32_t hash, const char* name, const GfxValue* args, uint32_t count,
           void* caller) {
    auto& t = SpidyMenuTraceData;
    if (!t.on)
        return;
    auto& e = t.entries[static_cast<uint32_t>(InterlockedIncrement(reinterpret_cast<volatile LONG*>(&t.next)) - 1) %
                        std::size(t.entries)];
    e = {};
    e.kind = kind;
    e.hash = hash;
    e.count = count;
    e.ui = reinterpret_cast<uint64_t>(ui);
    e.caller = reinterpret_cast<uint64_t>(caller) - imageBase;
    __try {
        if (name)
            strncpy_s(e.name, name, _TRUNCATE);
        for (uint32_t i = 0; args && i < count && i < 4; ++i) {
            e.types[i] = args[i].type;
            e.values[i] = args[i].data;
            if ((args[i].type & typeMask) == 6) {
                const char* s = (args[i].type & managedType) ? *reinterpret_cast<const char* const*>(args[i].data)
                                                             : reinterpret_cast<const char*>(args[i].data);
                if (s)
                    strncpy_s(e.texts[i], s, _TRUNCATE);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
bool __fastcall invokeHook(void* ui, const char* name, GfxValue* args, uint32_t count, uint64_t a, float b,
                           uint32_t c, uint64_t d) {
    trace(1, ui, 0, name, args, count, _ReturnAddress());
    if (name == reinterpret_cast<const char*>(imageBase + saveDataLoadedName)) {
        ++mainMenusShown;
        titleUi = ui;
    }
    if (name == reinterpret_cast<const char*>(imageBase + openOptionsName) && args && count >= 1 &&
        (args[0].type & typeMask) == arrayType && args[0].objects) {
        // The first-time setup's Settings (each build: a change in them builds them all again).
        const Setup state = setupState;
        if ((state == Setup::opening || state == Setup::showing) && setupFlow == SetupFlow::pause &&
            ui == setupUi && addSetupTab(args[0]))
            setupState = Setup::showing;
        else
            addTab(args[0]);
    }
    // MinHook takes this target once, so the prompts are read from here
    // rather than from a second hook of their own (game_prompts.hpp).
    game_prompts::observeCall(imageBase, name, args, count);
    // The throw marker over a prop drawn as X in VR play.
    GfxValue changed[8];
    if (count <= 8 && game_prompts::rewriteCall(imageBase, name, args, count, changed))
        return originalInvoke(ui, name, changed, count, a, b, c, d);
    return originalInvoke(ui, name, args, count, a, b, c, d);
}
void __fastcall callbackHook(void* ui, uint32_t hash, GfxValue* args, uint32_t count) {
    trace(2, ui, hash, nullptr, args, count, _ReturnAddress());
    // The first-time setup: B in its Settings leaves the pause menu too, as RESUME would, and play starts;
    // the menu closed any other way ends it as well.
    if (setupReady && setupFlow == SetupFlow::pause && ui && ui == setupUi) {
        const Setup state = setupState;
        if (state == Setup::showing && hash == get<uint32_t>(imageBase + closeOptionsHash)) {
            originalCallback(ui, hash, args, count);
            setupState = Setup::done;
            setupUi = nullptr;
            closePauseMenu(ui);
            return;
        }
        if ((state == Setup::opening || state == Setup::showing) && hash == get<uint32_t>(imageBase + closeHash)) {
            setupState = Setup::done;
            setupUi = nullptr;
        }
    }
    // A on VR VIEW, on the pause menu's first page: the other view, and the
    // page built again to name the next one.
    if (built == 1 && pageReady && pageRow >= 0 && hash == pageSelectHash && args && count >= 1 &&
        ((args[0].type & typeMask) == intType || (args[0].type & typeMask) == intType + 1) &&
        static_cast<uint32_t>(args[0].data) == static_cast<uint32_t>(firstSetting + pageRow)) {
        AcquireSRWLockShared(&lock);
        const bool screen = shown.flatScreen;
        ReleaseSRWLockShared(&lock);
        change(Item::view, screen ? 0 : 1);
        reinterpret_cast<void(__fastcall*)(void*)>(imageBase + pageRefresh)(ui);
        return;
    }
    // Y RESET ALL on the SPIDY VR tab: the game's RESET ALL takes only its own tabs.
    if (built == 1 && args && count >= 1 && (args[0].type & typeMask) == intType &&
        static_cast<uint32_t>(args[0].data) >= tabId &&
        static_cast<uint32_t>(args[0].data) < tabId + screenCount &&
        hash == get<uint32_t>(imageBase + resetAllHash)) {
        const size_t category = static_cast<uint32_t>(args[0].data) - tabId;
        AcquireSRWLockExclusive(&lock);
        const auto reset = vr_settings::resetCategory(static_cast<vr_settings::Category>(category), shown);
        for (Item item : vr_settings::screens()[category].items)
            if (const auto* row = vr_settings::rowFor(item); row && row->place == vr_settings::Place::tab)
                if (vr_settings::choose(item, vr_settings::choice(item, reset), shown)) {
                    pending.set(static_cast<size_t>(item));
                    ++changesMade;
                }
        ReleaseSRWLockExclusive(&lock);
        // Built again, every tab shows its values, as after the game's own RESET ALL.
        reinterpret_cast<void(__fastcall*)(void*)>(imageBase + openOptions)(ui);
        return;
    }
    originalCallback(ui, hash, args, count);
}
// What the game asks of a setting: its kind (0 a list of choices, 5 a heading).
// The game's own setting a number is asked as: the title entry's as the entry it was copied from.
int asGame(int setting) {
    const int as = titleEntryAs;
    return setting == titleEntrySetting && as >= 0 ? as : setting;
}
int __fastcall typeHook(void* system, int setting) {
    const int r = rowOf(setting);
    if (r < 0)
        return originalType(system, asGame(setting));
    return itemOf(r) == Item::none ? 5 : 0;
}
// Its value: the choice shown.
float __fastcall valueHook(void* system, int setting) {
    const int r = rowOf(setting);
    if (r < 0)
        return originalValue(system, asGame(setting));
    AcquireSRWLockShared(&lock);
    const int index = vr_settings::choice(itemOf(r), shown);
    ReleaseSRWLockShared(&lock);
    return static_cast<float>(index);
}
// Dependent controls retain their values while their parent or overriding mode is inactive.
bool __fastcall activeHook(void* system, int setting) {
    const int r = rowOf(setting);
    if (r < 0)
        return originalActive(system, asGame(setting));
    AcquireSRWLockShared(&lock);
    const bool active = vr_settings::inactiveReason(itemOf(r), shown) == nullptr;
    ReleaseSRWLockShared(&lock);
    return active;
}
// Its default, as a value and as a choice: what X RESET puts back.
float __fastcall defaultHook(void* system, int setting) {
    const int r = rowOf(setting);
    return r < 0 ? originalDefault(system, asGame(setting))
                 : static_cast<float>(vr_settings::defaultChoice(itemOf(r)));
}
int __fastcall defaultChoiceHook(void* system, int setting) {
    const int r = rowOf(setting);
    return r < 0 ? originalDefaultChoice(system, asGame(setting)) : vr_settings::defaultChoice(itemOf(r));
}
// A change from Flash (left or right on a row), and X RESET.
void __fastcall setHook(void* system, int setting, float value, bool apply, bool force) {
    const int r = rowOf(setting);
    if (setting == titleEntrySetting)
        return;
    if (r < 0)
        return originalSet(system, setting, value, apply, force);
    if (std::isfinite(value) && std::abs(value) < 64)
        change(itemOf(r), static_cast<int>(std::lround(value)));
}
void __fastcall resetHook(void* system, int setting, bool apply) {
    const int r = rowOf(setting);
    if (setting == titleEntrySetting)
        return;
    if (r < 0)
        return originalReset(system, setting, apply);
    change(itemOf(r), vr_settings::defaultChoice(itemOf(r)));
}
// The tab's texts, in English whatever the game's language.
const char* __fastcall textHook(uint32_t hash, const char* fallback) {
    if (buildingTab)
        if (const char* text = textFor(hash))
            return text;
    // The same for the texts: a prompt's own names its buttons, and in VR it
    // names what the player presses instead.
    return game_prompts::observeText(originalText(hash, fallback));
}
bool __fastcall hasTextHook(uint32_t hash) {
    return (buildingTab && textFor(hash)) || originalHasText(hash);
}
// The game's text table itself (the table, a tag's hash): under the lookup by
// hash above and the one by tag (0x1749a20, which help lines and hint cards
// take, and which never passes 0x1749970). In a VR session a text the game
// teaches its controls with comes back in VR wording (game_prompts::tableText).
using TableTextFn = const char*(__fastcall*)(void*, uint32_t);
TableTextFn originalTableText{};
const char* __fastcall tableTextHook(void* table, uint32_t hash) {
    return game_prompts::tableText(hash, originalTableText(table, hash));
}
// The HUD's prompt list: its widget showing a line of `kind` (0-20) with a
// text, and hiding one - what game_prompts keeps as asked for while it is up
// (promptShown), and the line it shows in VR play. The show takes eight
// arguments, four on the stack (7793d0 reads them at rbp+480 to rbp+498),
// all passed on as they came.
using PromptShowFn = bool(__fastcall*)(void*, int, const char*, float, uint64_t, uint64_t, uint64_t, uint64_t);
using PromptHideFn = bool(__fastcall*)(void*, int, const char*, uint64_t);
PromptShowFn originalPromptShow{};
PromptHideFn originalPromptHide{};
bool __fastcall promptShowHook(void* widget, int kind, const char* text, float wait, uint64_t a, uint64_t b,
                               uint64_t c, uint64_t d) {
    const char* line = kind >= 0 ? game_prompts::promptShown(static_cast<unsigned>(kind), text) : text;
    return originalPromptShow(widget, kind, line, wait, a, b, c, d);
}
bool __fastcall promptHideHook(void* widget, int kind, const char* text, uint64_t now) {
    if (kind >= 0)
        game_prompts::promptHidden(static_cast<unsigned>(kind));
    return originalPromptHide(widget, kind, text, now);
}

struct Hook {
    uintptr_t offset;
    void* detour;
    void** original;
    // The function's first bytes in the supported game.
    unsigned char bytes[10];
    size_t count;
};
const Hook hooks[] = {
    {0x1d1cf30, reinterpret_cast<void*>(&invokeHook), reinterpret_cast<void**>(&originalInvoke),
     {0x80, 0xb9, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x75, 0x03}, 9},
    {0x7dc400, reinterpret_cast<void*>(&callbackHook), reinterpret_cast<void**>(&originalCallback),
     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10}, 10},
    {0x72d640, reinterpret_cast<void*>(&typeHook), reinterpret_cast<void**>(&originalType),
     {0x48, 0x63, 0xc2, 0x48, 0x8d, 0x14, 0x80, 0x8b, 0x84, 0xd1}, 10},
    {0x72d650, reinterpret_cast<void*>(&valueHook), reinterpret_cast<void**>(&originalValue),
     {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18}, 10},
    {0x72e1d0, reinterpret_cast<void*>(&activeHook), reinterpret_cast<void**>(&originalActive),
     {0x48, 0x63, 0xc2, 0x48, 0x8d, 0x14, 0x80, 0x0f, 0xb6, 0x84}, 10},
    {0x72d5f0, reinterpret_cast<void*>(&defaultHook), reinterpret_cast<void**>(&originalDefault),
     {0x48, 0x63, 0xc2, 0x48, 0x8d, 0x14, 0x80, 0xf3, 0x0f, 0x10}, 10},
    {0x72d610, reinterpret_cast<void*>(&defaultChoiceHook), reinterpret_cast<void**>(&originalDefaultChoice),
     {0x48, 0x83, 0xec, 0x28, 0x48, 0x63, 0xc2, 0x0f, 0x57, 0xc9}, 10},
    {0x730ee0, reinterpret_cast<void*>(&setHook), reinterpret_cast<void**>(&originalSet),
     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10}, 10},
    {0x730ae0, reinterpret_cast<void*>(&resetHook), reinterpret_cast<void**>(&originalReset),
     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10}, 10},
    {0x1749970, reinterpret_cast<void*>(&textHook), reinterpret_cast<void**>(&originalText),
     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10}, 10},
    {0x1749ae0, reinterpret_cast<void*>(&hasTextHook), reinterpret_cast<void**>(&originalHasText),
     {0x48, 0x83, 0xec, 0x28, 0x8b, 0xd1, 0x48, 0x8b, 0x0d}, 9},
    {addItem, reinterpret_cast<void*>(&addItemHook), reinterpret_cast<void**>(&originalAddItem),
     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10}, 10},
    {0x7793d0, reinterpret_cast<void*>(&promptShowHook), reinterpret_cast<void**>(&originalPromptShow),
     {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70}, 10},
    {0x77a1e0, reinterpret_cast<void*>(&promptHideHook), reinterpret_cast<void**>(&originalPromptHide),
     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10}, 10},
    {0x1748d90, reinterpret_cast<void*>(&tableTextHook), reinterpret_cast<void**>(&originalTableText),
     {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x44, 0x8b, 0x41, 0x30}, 10},
};
bool matches(uintptr_t at, const unsigned char* bytes, size_t count) {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(at), bytes, count) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
vr_settings::Values copyItems(const vr_settings::Values& from, vr_settings::Values to,
                              const ItemBits& which) {
    for (unsigned i = 1; i <= static_cast<unsigned>(vr_settings::lastItem); ++i)
        if (which.test(i))
            vr_settings::choose(static_cast<Item>(i), vr_settings::choice(static_cast<Item>(i), from), to);
    return to;
}
} // namespace

uint32_t game_menu::install(uintptr_t base) {
    if (hooked)
        return 0;
    if (!created) {
        for (size_t i = 0; i < std::size(hooks); ++i)
            if (!matches(base + hooks[i].offset, hooks[i].bytes, hooks[i].count))
                return 9301 + static_cast<uint32_t>(i);
        imageBase = base;
        addTexts();
        pageReady = matches(base + pageRefresh, pageRefreshBytes, sizeof(pageRefreshBytes));
        auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
            return 9400 + status;
        for (size_t i = 0; i < std::size(hooks); ++i)
            if ((status = MH_CreateHook(reinterpret_cast<void*>(base + hooks[i].offset), hooks[i].detour,
                                        hooks[i].original)) != MH_OK) {
                while (i--)
                    MH_RemoveHook(reinterpret_cast<void*>(base + hooks[i].offset));
                return 9420 + status;
            }
        // The first-time setup's own hook, only where its code is this game's: without it the setup stays
        // off and the rest works as before.
        setupReady = pageReady && matches(base + closeMenu, closeMenuBytes, sizeof(closeMenuBytes)) &&
                     MH_CreateHook(reinterpret_cast<void*>(base + pageRefresh), reinterpret_cast<void*>(&pageRefreshHook),
                                   reinterpret_cast<void**>(&originalPageRefresh)) == MH_OK;
        // The title Settings' VR SETUP page: both builds call the config's getter where expected.
        const auto calls = [&](uintptr_t returnAt) {
            __try {
                const auto at = base + returnAt - 5;
                return *reinterpret_cast<const unsigned char*>(at) == 0xe8 &&
                       at + 5 + *reinterpret_cast<const int32_t*>(at + 1) == base + configGetter;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        };
        titleReady = matches(base + titleBuild, titleBuildBytes, sizeof(titleBuildBytes)) &&
                     matches(base + configGetter, configGetterBytes, sizeof(configGetterBytes)) &&
                     matches(base + titleCallback, titleCallbackBytes, sizeof(titleCallbackBytes)) &&
                     calls(titleBuildReturn) && calls(titleListReturn) &&
                     MH_CreateHook(reinterpret_cast<void*>(base + titleBuild), reinterpret_cast<void*>(&titleBuildHook),
                                   reinterpret_cast<void**>(&originalTitleBuild)) == MH_OK;
        if (titleReady && MH_CreateHook(reinterpret_cast<void*>(base + configGetter), reinterpret_cast<void*>(&configHook),
                                        reinterpret_cast<void**>(&originalConfig)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void*>(base + titleBuild));
            titleReady = false;
        }
        if (titleReady &&
            MH_CreateHook(reinterpret_cast<void*>(base + titleCallback), reinterpret_cast<void*>(&titleCallbackHook),
                          reinterpret_cast<void**>(&originalTitleCallback)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void*>(base + titleBuild));
            MH_RemoveHook(reinterpret_cast<void*>(base + configGetter));
            titleReady = false;
        }
        // The title flow's own opening of the title's Settings: the title's update where its vtable has it,
        // and the menu's item check and the movie's CreateObject as expected. Without it the title's Settings
        // still open on VR SETUP when the player chooses SETTINGS.
        titleUpdateReady = titleReady && matches(base + titleUpdate, titleUpdateBytes, sizeof(titleUpdateBytes)) &&
                           matches(base + itemShown, itemShownBytes, sizeof(itemShownBytes)) &&
                           matches(base + createObject, createObjectBytes, sizeof(createObjectBytes)) &&
                           get<uintptr_t>(base + titleVtableUpdate) == base + titleUpdate &&
                           MH_CreateHook(reinterpret_cast<void*>(base + titleUpdate),
                                         reinterpret_cast<void*>(&titleUpdateHook),
                                         reinterpret_cast<void**>(&originalTitleUpdate)) == MH_OK;
        created = true;
    }
    // All at once: the tab never reaches the game before the answers about its rows.
    for (const auto& h : hooks)
        if (const auto status = MH_QueueEnableHook(reinterpret_cast<void*>(imageBase + h.offset)); status != MH_OK)
            return 9440 + status;
    if (setupReady && MH_QueueEnableHook(reinterpret_cast<void*>(imageBase + pageRefresh)) != MH_OK)
        setupReady = false;
    if (titleReady && (MH_QueueEnableHook(reinterpret_cast<void*>(imageBase + titleBuild)) != MH_OK ||
                       MH_QueueEnableHook(reinterpret_cast<void*>(imageBase + configGetter)) != MH_OK ||
                       MH_QueueEnableHook(reinterpret_cast<void*>(imageBase + titleCallback)) != MH_OK))
        titleReady = false;
    if (titleUpdateReady && (!titleReady || MH_QueueEnableHook(reinterpret_cast<void*>(imageBase + titleUpdate)) != MH_OK))
        titleUpdateReady = false;
    if (const auto status = MH_ApplyQueued(); status != MH_OK)
        return 9460 + status;
    hooked = true;
    return 0;
}
void game_menu::uninstall() {
    if (!hooked)
        return;
    for (const auto& h : hooks)
        MH_QueueDisableHook(reinterpret_cast<void*>(imageBase + h.offset));
    if (setupReady)
        MH_QueueDisableHook(reinterpret_cast<void*>(imageBase + pageRefresh));
    if (titleReady) {
        MH_QueueDisableHook(reinterpret_cast<void*>(imageBase + titleBuild));
        MH_QueueDisableHook(reinterpret_cast<void*>(imageBase + configGetter));
        MH_QueueDisableHook(reinterpret_cast<void*>(imageBase + titleCallback));
    }
    if (titleUpdateReady)
        MH_QueueDisableHook(reinterpret_cast<void*>(imageBase + titleUpdate));
    MH_ApplyQueued();
    hooked = false;
    setupState = Setup::off;
    setupUi = nullptr;
    titleOpen = TitleOpen::idle;
}
bool game_menu::setupAvailable(SetupFlow flow) {
    return hooked && built != 2 && (flow == SetupFlow::title ? titleReady : setupReady);
}
void game_menu::beginSetup(SetupFlow flow) {
    if (const Setup state = setupState; state == Setup::off || state == Setup::done) {
        setupFlow = flow;
        setupUi = nullptr;
        // The title flow opens the title's Settings by itself (titleUpdateHook) where it can.
        titleSteady = titleSinceClick = titleClicks = 0;
        titleOpen = flow == SetupFlow::title && titleUpdateReady ? TitleOpen::waiting : TitleOpen::idle;
        setupState = Setup::armed;
    }
}
bool game_menu::cancelSetup() {
    // The title flow shows its page in the title's Settings data until it is left there; a game loaded
    // meanwhile ends it too (the page stays one of the Settings', its way back kept).
    auto armed = Setup::armed, opening = Setup::opening, showing = Setup::showing;
    titleOpen = TitleOpen::idle;
    return setupState.compare_exchange_strong(armed, Setup::off) ||
           setupState.compare_exchange_strong(opening, Setup::off) ||
           (setupFlow == SetupFlow::title && setupState.compare_exchange_strong(showing, Setup::off));
}
void game_menu::endSetup() {
    auto showing = Setup::showing, opening = Setup::opening;
    if (setupState.compare_exchange_strong(showing, Setup::done) ||
        setupState.compare_exchange_strong(opening, Setup::done))
        setupUi = nullptr;
}
Setup game_menu::setup() {
    return setupState;
}
void game_menu::publish(const vr_settings::Values& values) {
    AcquireSRWLockExclusive(&lock);
    // A change from the tab not taken yet stays as the tab left it.
    shown = pending.any() ? copyItems(shown, values, pending) : values;
    ReleaseSRWLockExclusive(&lock);
}
bool game_menu::take(vr_settings::Values& values) {
    AcquireSRWLockExclusive(&lock);
    const ItemBits which = pending;
    if (which.any())
        values = copyItems(shown, values, which);
    pending.reset();
    ReleaseSRWLockExclusive(&lock);
    return which.any();
}
Telemetry game_menu::telemetry() {
    Telemetry t;
    t.tabs = tabsBuilt;
    t.changes = changesMade;
    t.installed = hooked;
    t.status = failure;
    t.pages = pagesBuilt;
    t.setup = setupState;
    t.setupFlow = setupFlow;
    t.setupBuilds = setupBuilt;
    t.setupStatus = setupFailure;
    t.mainMenus = mainMenusShown;
    t.titleBuilds = titleBuilds;
    t.titleUpdates = titleUpdates;
    t.titleOpen = static_cast<uint32_t>(titleOpen.load());
    t.titleClicks = titleClicks;
    return t;
}

namespace {
// Headless probes (tools/probe_menu.py) show the tab without a VR session.
// The settings as XrData reports them: flags 1 web grab, 2 punch, 4 body,
// 8 webs in open air, 16 web shooter, 32 aim markers; 64 a T-pose
// calibration wanted (CALIBRATE BODY: ON RESUME); 128 flips (experimental);
// 256 the trigger webs (WEB BUTTON: TRIGGER); 512 STAND ON WALLS; 1024 (in the
// sample) VR VIEW: GAME SCREEN; 2048 arcade webs (WEB STYLE: ARCADE). hud: the
// HUD row (0 off, 1-3); flipSpeed: FLIP SPEED, degrees a second. Version 6:
// bodyHaptics, BODY HAPTICS in percent, and a spare word: in the settings its
// bit 0 arms the first-time setup (beginSetup), bit 1 its title flow instead of
// the pause menu's; in the sample, the setup's state (Setup) in its low byte,
// its builds (at most 255) in the next, its status less 9500 in the third and
// the title's main menus shown (at most 255) in the fourth.
struct ProbeSettings {
    uint32_t magic = 0x554e4d53, version = 6, bytes = sizeof(ProbeSettings), flags{};
    uint32_t snapTurn{}, haptics{}, screenSize{};
    float swingSpeed{};
    uint32_t smoothTurn{}, weight{}, hud{}, flipSpeed{}, bodyHaptics{}, spare{};
};
static_assert(sizeof(ProbeSettings) == 56);
struct ProbeSample {
    uint32_t magic = 0x554e4d53, version = 6, bytes = sizeof(ProbeSample), installed{};
    uint64_t tabs{}, changes{};
    uint32_t status{}, flags{}, snapTurn{}, haptics{}, screenSize{};
    float swingSpeed{};
    uint32_t smoothTurn{}, weight{}, hud{}, flipSpeed{}, bodyHaptics{}, spare{};
};
static_assert(sizeof(ProbeSample) == 80);
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidyMenuStart(void* input) {
    ProbeSettings s;
    __try {
        if (reinterpret_cast<uintptr_t>(input) < 0x10000)
            return 9601;
        std::memcpy(&s, input, sizeof(s));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 9601;
    }
    const ProbeSettings expected;
    if (s.magic != expected.magic || s.version != expected.version || s.bytes != expected.bytes)
        return 9602;
    vr_settings::Values v;
    v.webGrab = s.flags & 1;
    v.punch = s.flags & 2;
    v.body = s.flags & 4;
    v.airWebs = s.flags & 8;
    v.webShooter = s.flags & 16;
    v.aimMarkers = s.flags & 32;
    v.calibrate = s.flags & 64;
    v.flips = s.flags & 128;
    v.triggerWebs = s.flags & 256;
    v.standOnWalls = s.flags & 512;
    v.arcadeWebs = s.flags & 2048;
    v.snapTurn = static_cast<int>(s.snapTurn);
    v.smoothTurn = static_cast<int>(s.smoothTurn);
    v.haptics = static_cast<int>(s.haptics);
    v.screenSize = static_cast<int>(s.screenSize);
    v.swingSpeed = s.swingSpeed;
    v.weight = static_cast<int>(s.weight);
    v.hud = static_cast<int>(s.hud);
    v.flipSpeed = static_cast<int>(s.flipSpeed);
    v.bodyHaptics = static_cast<int>(s.bodyHaptics);
    if (const auto code = install(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))))
        return code;
    publish(vr_settings::sanitized(v));
    if (s.spare & 1)
        beginSetup(s.spare & 2 ? SetupFlow::title : SetupFlow::pause);
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyMenuSample(void* output) {
    ProbeSample s;
    const auto t = telemetry();
    s.installed = t.installed;
    s.tabs = t.tabs;
    s.changes = t.changes;
    s.status = t.status;
    AcquireSRWLockShared(&lock);
    const auto v = shown;
    ReleaseSRWLockShared(&lock);
    s.flags = (v.webGrab ? 1u : 0u) | (v.punch ? 2u : 0u) | (v.body ? 4u : 0u) | (v.airWebs ? 8u : 0u) |
              (v.webShooter ? 16u : 0u) | (v.aimMarkers ? 32u : 0u) | (v.calibrate ? 64u : 0u) |
              (v.flips ? 128u : 0u) | (v.triggerWebs ? 256u : 0u) | (v.standOnWalls ? 512u : 0u) |
              (v.flatScreen ? 1024u : 0u) | (v.arcadeWebs ? 2048u : 0u);
    s.snapTurn = static_cast<uint32_t>(v.snapTurn);
    s.smoothTurn = static_cast<uint32_t>(v.smoothTurn);
    s.haptics = static_cast<uint32_t>(v.haptics);
    s.screenSize = static_cast<uint32_t>(v.screenSize);
    s.swingSpeed = v.swingSpeed;
    s.weight = static_cast<uint32_t>(v.weight);
    s.hud = static_cast<uint32_t>(v.hud);
    s.flipSpeed = static_cast<uint32_t>(v.flipSpeed);
    s.bodyHaptics = static_cast<uint32_t>(v.bodyHaptics);
    s.spare = static_cast<uint32_t>(t.setup) | (t.titleOpen & 0xf) << 4 | std::min<uint32_t>(t.setupBuilds, 0xff) << 8 |
              (t.setupStatus ? t.setupStatus - 9500 : 0) << 16 |
              static_cast<uint32_t>(std::min<uint64_t>(t.mainMenus, 0xff)) << 24;
    __try {
        if (reinterpret_cast<uintptr_t>(output) < 0x10000)
            return 9601;
        std::memcpy(output, &s, sizeof(s));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 9601;
    }
    return 0;
}
// SpidyMenuTraceData from now (1, cleared first) or no more (0).
extern "C" __declspec(dllexport) DWORD WINAPI SpidyMenuTrace(void* on) {
    if (on)
        SpidyMenuTraceData.next = 0;
    SpidyMenuTraceData.on = on ? 1 : 0;
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyMenuStop(void*) {
    uninstall();
    return 0;
}
