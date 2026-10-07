// The SPIDY VR tab in the game's own Settings (game_menu.hpp). Addresses are
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
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_menu;
using vr_settings::Item;

namespace {
// The settings system; +0x20 its config, whose tabs are at +0x168 (0x40 bytes
// each, a UISystemMenu) and +0x170 counts them.
constexpr uintptr_t settingsSystem = 0x5d96dd0;
// The pause menu's Settings build every tab and call Flash's OpenOptions.
constexpr uintptr_t openOptions = 0x7dd9f0;
// A tab's Flash object, rows and all: (out value, movie, tab) -> built.
constexpr uintptr_t buildTab = 0x807740;
// The UI movie Flash objects are made in.
constexpr uintptr_t uiMovie = 0x1d29690;
// "OpenOptions", the literal openOptions passes; the hash Flash's
// ResetAllOptionsForCurrentOptionsMenu callback arrives with (set at start-up).
constexpr uintptr_t openOptionsName = 0x389f4c0, resetAllHash = 0x6d8ed88;
// The option types of a setting item and of a section heading (vtables).
constexpr uintptr_t settingType = 0x3a7d6e8, headingType = 0x3a7d838;
// The tab's id: the game's run 0-10, and its RESET ALL ignores any above.
constexpr uint32_t tabId = 93;
// The rows' setting numbers: the game's run 0-122.
constexpr int firstSetting = 0x200;
constexpr size_t tabBytes = 0x40, itemBytes = 0x88, choiceBytes = 0x50, maxChoices = 9;
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

uintptr_t imageBase{};
SRWLOCK lock = SRWLOCK_INIT;
vr_settings::Values shown;
uint32_t pending{}; // bits 1 << Item changed in the tab and not taken yet
std::atomic<uint64_t> tabsBuilt{}, changesMade{};
std::atomic<uint32_t> failure{};
std::atomic<bool> hooked{}, created{};
// The tab and its items: 0 not built yet, 1 ready, 2 cannot be built.
std::atomic<int> built{};
alignas(16) unsigned char tab[tabBytes];
alignas(16) unsigned char items[rowCount][itemBytes];
alignas(16) unsigned char choices[rowCount][maxChoices][choiceBytes];

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
std::array<Text, 2 + rowCount * 2 + rowCount * maxChoices> texts{};
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
const Text *tabName{}, *tabHeading{};
const Text *titles[rowCount]{}, *helps[rowCount]{};
const Text* choiceTexts[rowCount][maxChoices]{};
void addTexts() {
    if (textCount)
        return;
    tabName = addText("SPIDY_VR_TAB", "SPIDY VR");
    tabHeading = addText("SPIDY_VR_HEADING", "SPIDY VR SETTINGS");
    char tag[24];
    for (size_t r = 0; r < rowCount; ++r) {
        const auto& row = vr_settings::rows()[r];
        sprintf_s(tag, "SPIDY_VR_%zu", r);
        titles[r] = addText(tag, row.title);
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
        std::memcpy(tab, reinterpret_cast<const void*>(menu), tabBytes);
        put<uint32_t>(tab, 8, tabId);
        putText(tab, 0x10, tabName);
        putText(tab, 0x20, tabHeading);
        put<uint64_t>(tab, 0x30, reinterpret_cast<uint64_t>(items));
        put<uint32_t>(tab, 0x38, static_cast<uint32_t>(rowCount));
        put<uint32_t>(tab, 0x3c, static_cast<uint32_t>(rowCount));
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
// The game's main thread is building the SPIDY VR tab.
thread_local bool buildingTab{};
// Adds the SPIDY VR tab, built by the game's own code, to the tabs the pause
// menu's Settings hand to Flash.
void addTab(GfxValue& tabs) {
    if (!ready())
        return;
    __try {
        GfxValue value{};
        std::memset(value.head, 0xff, sizeof(value.head));
        void* movie = reinterpret_cast<void*(__fastcall*)()>(imageBase + uiMovie)();
        if (!movie)
            return;
        buildingTab = true;
        const bool made =
            reinterpret_cast<bool(__fastcall*)(GfxValue*, void*, const void*)>(imageBase + buildTab)(&value, movie, tab);
        buildingTab = false;
        if (made && pushBack(tabs, value))
            ++tabsBuilt;
        releaseValue(value);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Not again this session; the game's own tabs go on.
        buildingTab = false;
        failure = 9503;
        built = 2;
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
        pending |= 1u << static_cast<unsigned>(item);
    ReleaseSRWLockExclusive(&lock);
    if (changed)
        ++changesMade;
}

bool __fastcall invokeHook(void* ui, const char* name, GfxValue* args, uint32_t count, uint64_t a, float b,
                           uint32_t c, uint64_t d) {
    if (name == reinterpret_cast<const char*>(imageBase + openOptionsName) && args && count >= 1 &&
        (args[0].type & typeMask) == arrayType && args[0].objects)
        addTab(args[0]);
    return originalInvoke(ui, name, args, count, a, b, c, d);
}
void __fastcall callbackHook(void* ui, uint32_t hash, GfxValue* args, uint32_t count) {
    // Y RESET ALL on the SPIDY VR tab: the game's RESET ALL takes only its own tabs.
    if (built == 1 && args && count >= 1 && (args[0].type & typeMask) == intType &&
        static_cast<uint32_t>(args[0].data) == tabId && hash == get<uint32_t>(imageBase + resetAllHash)) {
        for (size_t r = 0; r < rowCount; ++r)
            change(itemOf(static_cast<int>(r)), vr_settings::defaultChoice(itemOf(static_cast<int>(r))));
        // Built again, every tab shows its values, as after the game's own RESET ALL.
        reinterpret_cast<void(__fastcall*)(void*)>(imageBase + openOptions)(ui);
        return;
    }
    originalCallback(ui, hash, args, count);
}
// What the game asks of a setting: its kind (0 a list of choices, 5 a heading).
int __fastcall typeHook(void* system, int setting) {
    const int r = rowOf(setting);
    if (r < 0)
        return originalType(system, setting);
    return itemOf(r) == Item::none ? 5 : 0;
}
// Its value: the choice shown.
float __fastcall valueHook(void* system, int setting) {
    const int r = rowOf(setting);
    if (r < 0)
        return originalValue(system, setting);
    AcquireSRWLockShared(&lock);
    const int index = vr_settings::choice(itemOf(r), shown);
    ReleaseSRWLockShared(&lock);
    return static_cast<float>(index);
}
// Whether it can be changed now: always.
bool __fastcall activeHook(void* system, int setting) {
    const int r = rowOf(setting);
    return r < 0 ? originalActive(system, setting) : true;
}
// Its default, as a value and as a choice: what X RESET puts back.
float __fastcall defaultHook(void* system, int setting) {
    const int r = rowOf(setting);
    return r < 0 ? originalDefault(system, setting) : static_cast<float>(vr_settings::defaultChoice(itemOf(r)));
}
int __fastcall defaultChoiceHook(void* system, int setting) {
    const int r = rowOf(setting);
    return r < 0 ? originalDefaultChoice(system, setting) : vr_settings::defaultChoice(itemOf(r));
}
// A change from Flash (left or right on a row), and X RESET.
void __fastcall setHook(void* system, int setting, float value, bool apply, bool force) {
    const int r = rowOf(setting);
    if (r < 0)
        return originalSet(system, setting, value, apply, force);
    if (std::isfinite(value) && std::abs(value) < 64)
        change(itemOf(r), static_cast<int>(std::lround(value)));
}
void __fastcall resetHook(void* system, int setting, bool apply) {
    const int r = rowOf(setting);
    if (r < 0)
        return originalReset(system, setting, apply);
    change(itemOf(r), vr_settings::defaultChoice(itemOf(r)));
}
// The tab's texts, in English whatever the game's language.
const char* __fastcall textHook(uint32_t hash, const char* fallback) {
    if (buildingTab)
        if (const char* text = textFor(hash))
            return text;
    return originalText(hash, fallback);
}
bool __fastcall hasTextHook(uint32_t hash) {
    return (buildingTab && textFor(hash)) || originalHasText(hash);
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
};
bool matches(uintptr_t at, const unsigned char* bytes, size_t count) {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(at), bytes, count) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
vr_settings::Values copyItems(const vr_settings::Values& from, vr_settings::Values to, uint32_t which) {
    for (unsigned i = 1; i <= static_cast<unsigned>(vr_settings::lastItem); ++i)
        if (which & (1u << i))
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
        created = true;
    }
    // All at once: the tab never reaches the game before the answers about its rows.
    for (const auto& h : hooks)
        if (const auto status = MH_QueueEnableHook(reinterpret_cast<void*>(imageBase + h.offset)); status != MH_OK)
            return 9440 + status;
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
    MH_ApplyQueued();
    hooked = false;
}
void game_menu::publish(const vr_settings::Values& values) {
    AcquireSRWLockExclusive(&lock);
    // A change from the tab not taken yet stays as the tab left it.
    shown = pending ? copyItems(shown, values, pending) : values;
    ReleaseSRWLockExclusive(&lock);
}
bool game_menu::take(vr_settings::Values& values) {
    AcquireSRWLockExclusive(&lock);
    const uint32_t which = pending;
    if (which)
        values = copyItems(shown, values, which);
    pending = 0;
    ReleaseSRWLockExclusive(&lock);
    return which != 0;
}
Telemetry game_menu::telemetry() {
    Telemetry t;
    t.tabs = tabsBuilt;
    t.changes = changesMade;
    t.installed = hooked;
    t.status = failure;
    return t;
}

namespace {
// Headless probes (tools/probe_menu.py) show the tab without a VR session.
// The settings as XrData reports them: flags 1 web grab, 2 punch, 4 body,
// 8 webs in open air, 16 web shooter, 32 aim markers.
struct ProbeSettings {
    uint32_t magic = 0x554e4d53, version = 2, bytes = sizeof(ProbeSettings), flags{};
    uint32_t snapTurn{}, haptics{}, screenSize{};
    float swingSpeed{};
    uint32_t smoothTurn{};
};
static_assert(sizeof(ProbeSettings) == 36);
struct ProbeSample {
    uint32_t magic = 0x554e4d53, version = 2, bytes = sizeof(ProbeSample), installed{};
    uint64_t tabs{}, changes{};
    uint32_t status{}, flags{}, snapTurn{}, haptics{}, screenSize{};
    float swingSpeed{};
    uint32_t smoothTurn{}, reserved{};
};
static_assert(sizeof(ProbeSample) == 64);
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
    v.snapTurn = static_cast<int>(s.snapTurn);
    v.smoothTurn = static_cast<int>(s.smoothTurn);
    v.haptics = static_cast<int>(s.haptics);
    v.screenSize = static_cast<int>(s.screenSize);
    v.swingSpeed = s.swingSpeed;
    if (const auto code = install(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))))
        return code;
    publish(vr_settings::sanitized(v));
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
              (v.webShooter ? 16u : 0u) | (v.aimMarkers ? 32u : 0u);
    s.snapTurn = static_cast<uint32_t>(v.snapTurn);
    s.smoothTurn = static_cast<uint32_t>(v.smoothTurn);
    s.haptics = static_cast<uint32_t>(v.haptics);
    s.screenSize = static_cast<uint32_t>(v.screenSize);
    s.swingSpeed = v.swingSpeed;
    __try {
        if (reinterpret_cast<uintptr_t>(output) < 0x10000)
            return 9601;
        std::memcpy(output, &s, sizeof(s));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 9601;
    }
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyMenuStop(void*) {
    uninstall();
    return 0;
}
