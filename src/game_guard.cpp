// Enemy types: guards in the game (game_guard.hpp). The rules and the book it runs on are tested
// (tests/guard_tests.cpp); the game side of it is tried by playing. setEnemyTypes() - the launcher's Enemy
// types - is the on/off of the whole module, and it is off until the launcher says on.
#include "spidy/game_guard.hpp"
#include "spidy/native_bodies.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>
using namespace spidy;
using namespace spidy::game_guard;

extern "C" {
// The guard's numbers for the headset side (a separate module): a Report, filled in.
__declspec(dllexport) DWORD WINAPI SpidyGuardSample(void* out);
// The guard's log lines for the headset side, one a call: a Note, in and out.
__declspec(dllexport) DWORD WINAPI SpidyGuardNote(void* request);
}

namespace {
uintptr_t base{};
// (touchOn is off until guard.txt says `touch on`, runnersOn until it says `runners on`; detectOn - an enemy
// who has not detected the hero does not guard - and kickOn - a swing kick goes through a baton's guard - are
// on until it says `detect off` and `swingkick off`. The launcher's Enemy types is the on/off of all of it,
// see moduleStarted below.)
std::atomic<bool> on{}, touchOn{false}, poseOn{true}, runnersOn{false}, detectOn{true}, kickOn{true};
// `on` is these three together - the module has started (start() ran, stop() has not), guard.txt does not say
// `off`, and the launcher's Enemy types is on (setEnemyTypes; off until it says otherwise). refreshOn()
// brings `on` to what they say.
std::atomic<bool> moduleStarted{}, rulesAllow{}, launcherAllows{};
bool wantedOn() {
    return moduleStarted.load() && rulesAllow.load() && launcherAllows.load();
}
// Until `on` agrees with them: start() (the punch module's thread) and setEnemyTypes() (the settings') may
// change them at the same moment, and whichever comes last must leave `on` as the last of them said.
void refreshOn() {
    bool want;
    do {
        want = wantedOn();
        on.store(want);
    } while (want != wantedOn());
}
std::atomic<float> hpRule{0.f};
// guard.txt's recoil, in metres (the headset reads it in the Report).
std::atomic<float> recoilMetres{.03f};
SRWLOCK lock = SRWLOCK_INIT;
// Under `lock`: each enemy's guard, what the headset reads, the last web pull refused (GetTickCount64) and
// whose.
guards::Book book;
Report counters;
ULONGLONG lastRefusalAt{};
uint64_t lastRefusedRecord{};
double pruneAt{};
// The punch module's thread alone: when watch() last ran, and the dodge in progress - where it began and
// until when it counts.
double watchedAt{};
struct Dodge {
    bool on{};
    Vec3 from{};
    double until{};
} dodge;
// A dodge counts as over an enemy when it has carried the hero at least dodgeRun metres, the enemy lay ahead
// of where it began, the hero is now dodgePast metres beyond him along the way, and the way passed within
// dodgeReach of him (all on the ground plane); a dodge is on for dodgeWindow seconds.
constexpr float dodgeRun = 1.f, dodgePast = .3f, dodgeAhead = .15f, dodgeReach = 1.2f;
constexpr double dodgeWindow = 1.2;

double nowSeconds() {
    return static_cast<double>(GetTickCount64()) * .001;
}

// The log lines: a ring, as the first-person worker's (game_fp), for the headset module to read into the
// session log (this module has no log of its own). Several threads say lines (the punch thread, the web
// grab's, the shooter's): a line claims its number first, and the reader takes it only once its slot says it
// is written.
constexpr uint32_t noteSlots = 256;
char noteText[noteSlots][200];
std::atomic<uint32_t> noteSerial{};
std::atomic<uint32_t> noteSeq[noteSlots]{}; // slot n % noteSlots holds line n once this says n + 1

// The folder with the launcher in it (where Spidy is kept): guard.txt is there. This module is loaded
// from a copy under reports\bridge-modules\<hash>, so "beside the module" would be a folder nobody can find
// (phone_music.cpp's spidyHome finds the folder the same way).
std::wstring installRoot() {
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&installRoot), &self);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring folder = path, root;
    while (root.empty()) {
        const auto slash = folder.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            break;
        folder.erase(slash);
        for (const wchar_t* launcher : {L"\\Launch Spidy VR.cmd", L"\\Spidy Launcher.exe"})
            if (GetFileAttributesW((folder + launcher).c_str()) != INVALID_FILE_ATTRIBUTES)
                root = folder;
    }
    const std::wstring whole(path);
    return root.empty() ? whole.substr(0, whole.find_last_of(L"\\/")) : root;
}
bool readFile(const std::wstring& path, std::string& out) {
    out.clear();
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    char buffer[4096];
    DWORD got{};
    while (out.size() < 65536 && ReadFile(file, buffer, sizeof(buffer), &got, nullptr) && got)
        out.append(buffer, got);
    CloseHandle(file);
    return true;
}
// A file that was not there (never one that was: CREATE_NEW).
bool writeNew(const std::wstring& path, const char* text) {
    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD put{};
    const bool ok = WriteFile(file, text, static_cast<DWORD>(std::strlen(text)), &put, nullptr) != 0;
    CloseHandle(file);
    return ok;
}
const char* const fileText =
    "# Spidy enemy types: guard rules. Delete this file and it is written again with these defaults.\r\n"
    "#\r\n"
    "# The launcher's ENEMY TYPES switch is the on/off for all of this (off is the default): off there, "
    "nobody guards, as\r\n"
    "# before Enemy types, whatever this file says. (`off` below does the same from here.)\r\n"
    "#\r\n"
    "# Some enemies GUARD: they block the punches and web strikes you throw at them (a blocked punch does "
    "no\r\n"
    "# damage). That is a BATON enemy (an enemy with a baton or another melee weapon), a SWORDSMAN, a BRUTE "
    "(a heavy,\r\n"
    "# armored or brawling enemy) or a SHIELD enemy. Plain unarmed and gun enemies are left alone.\r\n"
    "# A guard OPENS, and your punches land, when:\r\n"
    "#   - something other than your fists hurts him: a gadget, a thrown prop or a thrown enemy (4 seconds), "
    "or\r\n"
    "#     he is stunned, knocked down, held or flying;\r\n"
    "#   - you DODGE OVER or past him (3 seconds).\r\n"
    "# An enemy who is WEBBED (struggling in a web, caught by a web pull) never guards, and his guard stays "
    "open for 4\r\n"
    "# seconds after the last moment he was seen webbed (the `webbed` line below) - unless he is running at "
    "you again by\r\n"
    "# then. A web ball alone does not open a guard: it takes about three in a row to web an enemy, and "
    "until then\r\n"
    "# he is not webbed, so a guarding enemy still guards.\r\n"
    "# An enemy in the middle of an attack guards too: your punch is blocked and he finishes his attack "
    "(with\r\n"
    "# `touch on` below your punch staggers him instead, so the attack stops if the game lets it).\r\n"
    "# Punching never breaks a guard, however many times. But a few blocked punches in a row make him swing "
    "back\r\n"
    "# at you: it does no damage, it only staggers you, and his arm is seen to swing. He puts his arms up "
    "for a\r\n"
    "# blocked punch or a refused web pull too.\r\n"
    "# No web drags an enemy whose guard stands. Bosses have their own fights.\r\n"
    "# An enemy who has not DETECTED you does not guard: your punches land on him and a web takes him. The "
    "game keeps\r\n"
    "# no flag of it the mod can read, so he counts as having detected you when he is fighting (a behavior "
    "or a state of a\r\n"
    "# fight on him), runs at you, loses hit points, or you hit or web him - and for 10 seconds after the "
    "last such sign.\r\n"
    "# (`detect off`: every enemy with a guard guards.)\r\n"
    "# A SWING KICK - the feet-first kick at the end of a swing, a zip, a jump or a fall - goes through a "
    "baton's guard.\r\n"
    "# A swordsman's, a brute's and a shield's guard stop it as they stop a punch (`swingkick off`: a "
    "baton's stops it too).\r\n"
    "#\r\n"
    "# The lines you can use (one a line; a # starts a note):\r\n"
    "#   off                   turn all of this off\r\n"
    "#   baton NAME            an enemy with a component class whose name contains NAME gets a baton's "
    "guard\r\n"
    "#   brute NAME            ... a brute's guard\r\n"
    "#   shield NAME           ... a shield's guard\r\n"
    "#   swordsman NAME        ... a swordsman's: a baton's guard with the sword's block sound\r\n"
    "#   exempt NAME           an enemy with such a class never guards\r\n"
    "#   hp 0                  an enemy with this many hit points or more guards as a brute (0: never, the "
    "default)\r\n"
    "#   runners on            any enemy running at you guards while he runs: your punch is blocked, no web "
    "drags him,\r\n"
    "#                         his arms come up. He does not stop (the mod cannot stop the game's AI); the "
    "moment he stops\r\n"
    "#                         running that guard is gone. (runners off is the default: only the enemies "
    "with a guard of\r\n"
    "#                         their own guard)\r\n"
    "#   runspeed 2            (with runners on) how fast, in metres a second, he must be coming at you to "
    "count as\r\n"
    "#                         running (a walk is about 1 to 1.5; 3 or 4 leaves only the fast runners; 2 is "
    "the default)\r\n"
    "#   detect off            an enemy who has not detected you guards all the same (on is the default: he "
    "does not)\r\n"
    "#   detecthold 10         seconds an enemy counts as having detected you after the last sign of it\r\n"
    "#   swingkick off         a baton's guard stops a swing kick as it stops a punch (on is the default: it "
    "does not)\r\n"
    "#   counter 3             blocked punches in a row (each within 3 seconds of the last) that make him "
    "swing\r\n"
    "#                         back and stagger you (0: he never does)\r\n"
    "#   pose off              his arms are not posed: no arms up for a block, no swing (pose on is the "
    "default;\r\n"
    "#                         the pose needs the BODY setting on)\r\n"
    "#   stagger 4             seconds a guard stays open after a gadget or a thrown prop hurts him (0: "
    "never)\r\n"
    "#   dodge 3               seconds a guard stays open after you dodge over him (0: never)\r\n"
    "#   webbed 4              seconds a guard stays open after he was last seen webbed (0: only while his "
    "state\r\n"
    "#                         says webbed)\r\n"
    "#   touch on             a blocked punch makes the enemy flinch (the game's own hit reaction, a stagger "
    "if he is\r\n"
    "#                         attacking). Off is the default: his arms come up and his body jolts "
    "instead\r\n"
    "#   recoil 3              a blocked punch knocks your view back this many centimetres, and it comes "
    "back\r\n"
    "#                         in a fifth of a second (0: not at all; 6 is twice as much). Only the view "
    "moves\r\n"
    "#\r\n"
    "# The block sounds come with the mod (the BLOW SOUNDS setting: OFF mutes them): a baton's for an "
    "ordinary\r\n"
    "# melee-weapon guard, a sword's for the swordsmen, a slayer's for the brutes and a shield's. A web\r\n"
    "# pull he refuses (the long web you yank with, not a web ball) has its own four, one for each of those, "
    "played\r\n"
    "# the moment the pull is refused. Which enemy has which guard and sound is written to the session log "
    "(lines\r\n"
    "# that start with GUARD) the first time you hit him.\r\n";

// Whether an enemy can guard at this moment: standing, free, on his feet. A staggered state opens the guard -
// unless he entered it within a second of a reaction the mod itself gave him (a blocked blow's touch, a
// counter's break of his attack): that may be a state of that name, and must not open the guard to the next
// punch. A WEBBED state opens it whatever the grace: a webbed enemy never guards. And so does having been
// webbed lately - the game may put a webbed enemy in a hit reaction between the web states, and that reaction
// is no webbed state by name. (Those seconds begin at a moment he is SEEN in a webbed state - a web ball
// alone does not begin them, see game_guard::ballHit - and they do not hold while he runs.)
constexpr double ownGrace = 1.0;
// Why a guard cannot stand at a moment (Stands: it can), for the log as well as for the answer.
// (Unaware: he has not detected the hero, see stanceOf.)
enum class Stance : uint8_t { Stands, Down, Held, Stunned, Flung, Webbed, WebbedLately, Staggered, Unaware };
const char* stanceName(Stance stance) {
    switch (stance) {
    case Stance::Unaware:
        return "open (he has not detected the hero)";
    case Stance::Stands:
        return "up";
    case Stance::Down:
        return "open (he is down)";
    case Stance::Held:
        return "open (held)";
    case Stance::Stunned:
        return "open (stunned)";
    case Stance::Flung:
        return "open (flung)";
    case Stance::Webbed:
        return "open (webbed)";
    case Stance::WebbedLately:
        return "open (webbed lately)";
    case Stance::Staggered:
        return "open (staggered)";
    }
    return "?";
}
Stance stanceOf(const game_targets::Candidate& c, double now) {
    if (!game_targets::up(base, c))
        return Stance::Down;
    if (game_targets::held(base, c))
        return Stance::Held;
    if (game_targets::isStunned(base, c))
        return Stance::Stunned;
    if (c.machine && native_bodies::flung(c.machine))
        return Stance::Flung;
    if (game_targets::inWebbedState(base, c))
        return Stance::Webbed;
    AcquireSRWLockShared(&lock);
    const double sinceTouched = book.sinceTouched(c.record, c.handle, now);
    const double sinceWebbed = book.sinceWebbed(c.record, c.handle, now);
    const double hold = static_cast<double>(book.tuning().webbedSeconds);
    const bool detected = book.noticed(c.record, c.handle, now);
    ReleaseSRWLockShared(&lock);
    // ... unless he is running again - a state of the chase, the go-to or the dash: he is free of the web,
    // and a guard that can be attacked or yanked while he runs is no guard.
    if (hold > 0 && sinceWebbed <= hold && !game_targets::inRunningState(base, c))
        return Stance::WebbedLately;
    if (game_targets::inOpenState(base, c) && sinceTouched > ownGrace)
        return Stance::Staggered;
    // He guards once he has detected the hero - watch() reads the signs of it, and the hero's own blow, web
    // ball or web at him is one (Book::notice).
    if (!detected && detectOn.load(std::memory_order_relaxed))
        return Stance::Unaware;
    return Stance::Stands;
}
bool standsFree(const game_targets::Candidate& c, double now) {
    return stanceOf(c, now) == Stance::Stands;
}
// An enemy who may guard while he runs at the hero though his classes give him no guard (guard.txt `runners
// on`): any enemy but a boss (his own fights), a neutral one, one on the hero's side (game_targets::enemy) or
// one guard.txt exempts.
bool runnerEligible(const game_targets::Candidate& c) {
    return on.load(std::memory_order_relaxed) && runnersOn.load(std::memory_order_relaxed) &&
           game_targets::enemy(c) &&
           !(c.traits & (game_targets::boss | game_targets::neutral | game_targets::guardExempt));
}
// The kind the book keeps an enemy under: the guard his classes (or his hit points) give him (`own`), else
// Runner - the guard every enemy has while he runs at the hero - for the enemy who may have it.
guards::Kind withRunner(const game_targets::Candidate& c, guards::Kind own) {
    if (own != guards::Kind::None)
        return own;
    return runnerEligible(c) ? guards::Kind::Runner : guards::Kind::None;
}
guards::Kind bookKind(const game_targets::Candidate& c) {
    return withRunner(c, game_guard::kindOf(c));
}
// A runner's guard stands only while he runs at the hero: watch() has seen him come at the hero fast enough
// within the last moments (the cheap test first: it is false for nearly every enemy at nearly every moment),
// and his state is still a running one - an attack, or standing, ends it at once.
bool runsNow(const game_targets::Candidate& c, double now) {
    AcquireSRWLockShared(&lock);
    const bool recent = book.runner(c.record, c.handle, now);
    ReleaseSRWLockShared(&lock);
    return recent && game_targets::inRunningState(base, c);
}
// What the log tells of a web and of the blows at guards: which state a webbed enemy was in. A web is written
// once for each enemy and web (not twice within 1.5 seconds), the blows for the first blowLogLimit of a run
// of the game, each with the state his state machine was in.
constexpr int webLogLimit = 60, blowLogLimit = 160;
std::atomic<int> websLogged{}, blowsLogged{};
void logWeb(const game_targets::Candidate& c, guards::Kind kind, const char* how) {
    if (websLogged.load(std::memory_order_relaxed) >= webLogLimit)
        return;
    websLogged.fetch_add(1, std::memory_order_relaxed);
    std::string state;
    game_targets::stateName(base, c, state);
    char line[200];
    std::snprintf(line, sizeof(line), "GUARD #%u %s guard open for the web (%s), his state: %s",
                  c.handle & 0xfffff, guards::kindName(kind), how, state.empty() ? "?" : state.c_str());
    game_guard::log(line);
}
// `closing`: for a runner's guard, how fast he was coming at the hero when the blow came, metres a second
// (the number his guard is judged by, so that a runner who was let in can be told from one who was too slow);
// not used for the others. The log of a web ball that hit an enemy with a guard of his own (once for each
// enemy and 1.5 seconds, within the web lines' limit) and of a web pull that took one up (once for each enemy
// and 3 seconds, the first takenLogLimit of a run of the game): the ball says that it opened nothing, the
// pull says why his guard was not up.
constexpr int takenLogLimit = 60;
std::atomic<int> takenLogged{};
std::atomic<uint64_t> lastBallRecord{}, lastTakenRecord{};
std::atomic<ULONGLONG> lastBallAt{}, lastTakenAt{};
void logBall(const game_targets::Candidate& c, guards::Kind kind) {
    const ULONGLONG t = GetTickCount64();
    const bool again = lastBallRecord.exchange(c.record, std::memory_order_relaxed) == c.record &&
                       t - lastBallAt.load(std::memory_order_relaxed) < 1500;
    lastBallAt.store(t, std::memory_order_relaxed);
    if (again || websLogged.load(std::memory_order_relaxed) >= webLogLimit)
        return;
    websLogged.fetch_add(1, std::memory_order_relaxed);
    std::string state;
    game_targets::stateName(base, c, state);
    char line[200];
    std::snprintf(line, sizeof(line),
                  "GUARD #%u %s guard hit by a web ball, his state: %s (his guard is open only once his "
                  "state says he is webbed)",
                  c.handle & 0xfffff, guards::kindName(kind), state.empty() ? "?" : state.c_str());
    game_guard::log(line);
}
// `kickThrough`: a swing kick (Strike::kick) met a baton's guard and went through it (guards::kickPasses).
void logBlow(const game_targets::Candidate& c, guards::Kind kind, const Strike& strike, Stance stance,
             const guards::Outcome& out, double closing, bool kickThrough = false) {
    if (blowsLogged.load(std::memory_order_relaxed) >= blowLogLimit)
        return;
    blowsLogged.fetch_add(1, std::memory_order_relaxed);
    std::string state;
    game_targets::stateName(base, c, state);
    char comes[40] = "";
    if (kind == guards::Kind::Runner)
        std::snprintf(comes, sizeof(comes), ", he comes at %.1f m/s", closing);
    char how[24] = "swing kick";
    if (!strike.kick)
        std::snprintf(how, sizeof(how), "hand %u", strike.hand);
    char line[200];
    std::snprintf(line, sizeof(line),
                  "GUARD #%u %s guard %s a blow (%s, strength %.2f%s), guard %s%s%s, his state: %s",
                  c.handle & 0xfffff, guards::kindName(kind),
                  out.verdict == guards::Verdict::Blocked ? "BLOCKED" : "let in", how, strike.strength,
                  strike.attacking ? ", he is attacking" : "", stanceName(stance),
                  kickThrough ? " (a baton's guard does not stop a swing kick)" : "", comes,
                  state.empty() ? "?" : state.c_str());
    game_guard::log(line);
}
// A blow landed on an enemy who has no guard but a runner's, and who was coming at the hero as fast as a
// runner does while his state is none of the running ones (runningState() is a guess from the game's list of
// states): his state is written down so that it can be added. The first runnerLogLimit of a run of the game.
constexpr int runnerLogLimit = 30;
std::atomic<int> fastLogged{};
void logFast(const game_targets::Candidate& c, double closing) {
    if (fastLogged.load(std::memory_order_relaxed) >= runnerLogLimit)
        return;
    fastLogged.fetch_add(1, std::memory_order_relaxed);
    std::string state;
    game_targets::stateName(base, c, state);
    char line[200];
    std::snprintf(
        line, sizeof(line),
        "GUARD #%u a blow landed on an enemy coming at the hero at %.1f m/s, his state is no running one: %s",
        c.handle & 0xfffff, closing, state.empty() ? "?" : state.c_str());
    game_guard::log(line);
}
// A change in whether an enemy with a guard has detected the hero (Book::detection), for finding out what the
// signs miss. Detected - written for an enemy followed a second and a half or more, so one met already in a
// fight is no news - says which sign it was; Lost says that no sign of the hero has been seen for the hold,
// and the guard is down. The first detectLogLimit of a run of the game.
constexpr int detectLogLimit = 60;
std::atomic<int> detectLogged{};
void logDetect(const game_targets::Candidate& c, guards::Kind kind, bool detected, const char* sign,
               double seconds) {
    if (detectLogged.load(std::memory_order_relaxed) >= detectLogLimit)
        return;
    detectLogged.fetch_add(1, std::memory_order_relaxed);
    std::string state;
    game_targets::stateName(base, c, state);
    char line[200];
    if (detected)
        std::snprintf(line, sizeof(line),
                      "GUARD #%u %s has detected the hero after %.0f s in sight (%s), his state: %s",
                      c.handle & 0xfffff, guards::kindName(kind), seconds, sign,
                      state.empty() ? "?" : state.c_str());
    else
        std::snprintf(
            line, sizeof(line),
            "GUARD #%u %s has shown no sign of the hero for %.0f s: his guard is down, his state: %s",
            c.handle & 0xfffff, guards::kindName(kind), seconds, state.empty() ? "?" : state.c_str());
    game_guard::log(line);
}
// One line, split at the commas into the lines the log can carry (at most `most` of them).
void logList(const char* head, const std::string& list, int most = 3) {
    std::string rest = list;
    for (int part = 0; part < most && !rest.empty(); ++part) {
        size_t cut = std::string::npos;
        if (rest.size() > 150)
            cut = rest.rfind(',', 150);
        const std::string piece = cut == std::string::npos ? rest : rest.substr(0, cut);
        rest = cut == std::string::npos ? std::string() : rest.substr(cut + 1);
        game_guard::log((std::string(head) + piece).c_str());
    }
}
// Once for each run of the game: the classes the game names that tell of a block, a guard or a reaction, so
// the game's own block pose (if it has one) can be found by name. A look at the game's type information,
// nothing more.
void logGameClasses(uintptr_t gameBase) {
    static std::atomic<bool> done{};
    if (done.exchange(true))
        return;
    std::string states, others;
    if (!game_targets::imageClasses(gameBase, states, others)) {
        game_guard::log("GUARD game class list: the game's type information could not be read");
        return;
    }
    game_guard::log("GUARD game class list follows (bot states without their BotState, then other block and "
                    "guard classes)");
    logList("GUARD game states: ", states, 24);
    logList("GUARD game other: ", others, 24);
}
// The census line of an enemy met for the first time: his guard, his hit points and the combat classes of his
// components, so a guard that is missing or should not be there can be given or taken by a class name.
void census(const game_targets::Candidate& c, guards::Kind kind) {
    float hp{}, most{};
    const bool haveHp = game_targets::health(c, hp), haveMost = game_targets::maxHealth(c, most);
    // The state he is in as well (the census comes with the first blow at him, so for an enemy who had not
    // detected the hero it shows what such an enemy is in and has on him), and in traits the bit 0x10000 is a
    // behavior of a fight (game_targets::guardEngaged), 0x200 a baton's guard class, 0x400 a brute's, 0x800 a
    // shield's, 0x4000 a swordsman's.
    std::string state;
    game_targets::stateName(base, c, state);
    char head[200];
    std::snprintf(
        head, sizeof(head), "GUARD #%u census: %s, guard=%s, sound=%s, hp %.0f/%.0f, traits %x, state %s",
        c.handle & 0xfffff, (c.traits & game_targets::thug) ? "thug" : "bot", guards::kindName(kind),
        kind == guards::Kind::None
            ? "-"
            : guards::soundName(guards::soundOf(kind, (c.traits & game_targets::guardSwordsman) != 0)),
        haveHp ? hp : -1.f, haveMost ? most : -1.f, c.traits, state.empty() ? "?" : state.c_str());
    game_guard::log(head);
    std::string classes;
    char label[48];
    std::snprintf(label, sizeof(label), "GUARD #%u classes: ", c.handle & 0xfffff);
    if (game_targets::combatClasses(c.record, classes))
        logList(label, classes);
    // His Behavior classes (the machinery left out), or none.
    std::string behaviors;
    std::snprintf(label, sizeof(label), "GUARD #%u behaviors: ", c.handle & 0xfffff);
    if (game_targets::behaviors(c.record, behaviors))
        logList(label, behaviors, 2);
    else
        game_guard::log((std::string(label) + "none").c_str());
}
} // namespace

void game_guard::log(const char* text) {
    // More than one thread says lines: a line claims its number first, and the reader takes it only once its
    // slot says it is written (with one counter read and then stored, two lines said together would take the
    // same slot).
    const uint32_t n = noteSerial.fetch_add(1, std::memory_order_acq_rel);
    std::snprintf(noteText[n % noteSlots], sizeof(noteText[0]), "%s", text);
    noteSeq[n % noteSlots].store(n + 1, std::memory_order_release);
}

bool game_guard::note(uint32_t& seen, char* out, size_t size) {
    const uint32_t serial = noteSerial.load(std::memory_order_acquire);
    if (serial == seen || !out || !size)
        return false;
    // Further behind than the ring holds (a reader that was away): skip to the oldest line still there.
    if (serial - seen > noteSlots)
        seen = serial - noteSlots;
    const uint32_t written = noteSeq[seen % noteSlots].load(std::memory_order_acquire);
    if (written != seen + 1) {
        if (written > seen + 1) // overwritten by a later line: that one is gone
            ++seen;
        return false; // not written yet: the next call
    }
    std::snprintf(out, size, "%s", noteText[seen % noteSlots]);
    ++seen;
    return true;
}

bool game_guard::running() {
    return on.load(std::memory_order_relaxed);
}

bool game_guard::touchWanted() {
    return on.load(std::memory_order_relaxed) && touchOn.load(std::memory_order_relaxed);
}

uint32_t game_guard::start(uintptr_t gameBase) {
    guards::Rules rules;
    std::string text;
    const std::wstring file = installRoot() + L"\\guard.txt";
    int unknown{};
    const bool fromFile = readFile(file, text);
    if (fromFile)
        unknown = guards::parseRules(text, rules);
    else
        writeNew(file, fileText);
    AcquireSRWLockExclusive(&lock);
    base = gameBase;
    book.clear();
    book.tune(rules.tuning);
    counters = {};
    counters.status = rules.enabled ? 1u : 0u;
    lastRefusalAt = 0;
    lastRefusedRecord = 0;
    pruneAt = nowSeconds() + 60;
    ReleaseSRWLockExclusive(&lock);
    hpRule.store(rules.hp, std::memory_order_relaxed);
    touchOn.store(rules.touch, std::memory_order_relaxed);
    runnersOn.store(rules.runners, std::memory_order_relaxed);
    detectOn.store(rules.detect, std::memory_order_relaxed);
    kickOn.store(rules.swingKick, std::memory_order_relaxed);
    poseOn.store(rules.pose, std::memory_order_relaxed);
    recoilMetres.store(rules.recoil * .01f, std::memory_order_relaxed);
    dodge = {};
    watchedAt = 0;
    // Every class is read again with the rules (the first scans may have used the built-in ones).
    game_targets::setGuardRules(rules);
    // guard.txt's `off` and the launcher's Enemy types are both ways to turn it off.
    rulesAllow.store(rules.enabled);
    moduleStarted.store(true);
    refreshOn();
    char line[200];
    std::snprintf(
        line, sizeof(line),
        "GUARD %s: guard.txt %s, %zu extra class parts, brute hp %s%.0f, recoil %.1f cm, counter after %.0f "
        "blocked punches, pose %s, touch %s, web hold %.0f s, %d line(s) not understood",
        rules.enabled ? "on" : "OFF", fromFile ? "read" : "written (defaults)",
        rules.weapon.size() + rules.brawler.size() + rules.shield.size() + rules.swordsman.size() +
            rules.exempt.size(),
        rules.hp > 0 ? "from " : "off ", rules.hp, rules.recoil, rules.tuning.counterBlows,
        rules.pose ? "on" : "off", rules.touch ? "on" : "off",
        static_cast<double>(rules.tuning.webbedSeconds), unknown);
    game_guard::log(line);
    // Its own line (a log line carries 200 characters): off unless guard.txt says `runners on`.
    if (rules.runners)
        std::snprintf(line, sizeof(line),
                      "GUARD runners on: an enemy running at the hero guards while he runs (from %.1f m/s)",
                      static_cast<double>(rules.tuning.runSpeed));
    else
        std::snprintf(line, sizeof(line),
                      "GUARD runners off: only the enemies with a guard of their own guard");
    game_guard::log(line);
    // Their own lines as well.
    if (rules.detect)
        std::snprintf(line, sizeof(line),
                      "GUARD detect on: an enemy who has not detected the hero does not guard (a sign of it "
                      "holds %.0f s)",
                      static_cast<double>(rules.tuning.detectSeconds));
    else
        std::snprintf(
            line, sizeof(line),
            "GUARD detect off: every enemy with a guard guards, whether he has detected the hero or not");
    game_guard::log(line);
    std::snprintf(line, sizeof(line), "GUARD swing kick %s: %s", rules.swingKick ? "on" : "off",
                  rules.swingKick
                      ? "a baton's guard does not stop it; a swordsman's, a brute's and a shield's do"
                      : "every guard stops it, as it stops a punch");
    game_guard::log(line);
    // The launcher's Enemy types is off until it says it is on (the settings usually come after the start).
    if (!launcherAllows.load())
        game_guard::log(
            "GUARD Enemy types is off: nobody guards until the launcher's Enemy types is turned on");
    if (rules.enabled)
        logGameClasses(gameBase);
    return 0;
}

void game_guard::setEnemyTypes(bool enabled) {
    const bool was = launcherAllows.exchange(enabled);
    refreshOn();
    if (was == enabled)
        return;
    game_guard::log(
        enabled ? "GUARD Enemy types is on in the launcher: the enemy types that block in the base game guard"
                : "GUARD Enemy types is OFF in the launcher: nobody guards, as before Enemy types");
}

void game_guard::stop() {
    moduleStarted.store(false);
    refreshOn();
    AcquireSRWLockExclusive(&lock);
    book.clear();
    counters.status = 0;
    ReleaseSRWLockExclusive(&lock);
}

guards::Kind game_guard::kindOf(const game_targets::Candidate& c) {
    if (!on.load(std::memory_order_relaxed) || !game_targets::enemy(c) ||
        (c.traits & (game_targets::boss | game_targets::neutral)))
        return guards::Kind::None;
    guards::Kind kind = game_targets::guardKind(c.traits);
    if (kind == guards::Kind::None && !(c.traits & game_targets::guardExempt)) {
        // A heavy one the class names do not give away: the tough ones are the heavy ones.
        const float rule = hpRule.load(std::memory_order_relaxed);
        float most{};
        if (rule > 0 && game_targets::maxHealth(c, most) && most >= rule)
            kind = guards::Kind::Brawler;
    }
    return kind;
}

bool game_guard::holds(const game_targets::Candidate& c) {
    const guards::Kind kind = bookKind(c);
    if (kind == guards::Kind::None)
        return false;
    const double now = nowSeconds();
    // A runner's guard stands only while he runs at the hero.
    if (kind == guards::Kind::Runner && !runsNow(c, now))
        return false;
    if (!standsFree(c, now))
        return false;
    AcquireSRWLockShared(&lock);
    const bool standing = book.standing(c.record, c.handle, kind, now);
    ReleaseSRWLockShared(&lock);
    return standing;
}

guards::Outcome game_guard::blow(const game_targets::Candidate& c, const Strike& strike) {
    if (!on.load(std::memory_order_relaxed))
        return {};
    // The guard his classes give him (the census says it), and the kind the book keeps him under: his own,
    // else a runner's, which stands only while he runs at the hero.
    const guards::Kind own = kindOf(c);
    const guards::Kind kind = withRunner(c, own);
    const double now = nowSeconds();
    AcquireSRWLockExclusive(&lock);
    const bool first = book.introduce(c.record, c.handle, now);
    ReleaseSRWLockExclusive(&lock);
    if (first)
        census(c, own);
    if (kind == guards::Kind::None)
        return {};
    guards::Blow b;
    b.mayCounter = strike.grounded;
    b.strength = strike.strength;
    b.touches = touchWanted(); // a block gives him a reaction of the mod's only with `touch on`
    // A guard that cannot stand lets the blow in: he is down, stunned, webbed, held or flying. (An enemy in
    // the middle of an attack guards - the blow is blocked, and with `touch on` the punch module staggers him
    // so the attack stops if the game lets it; otherwise he finishes it.)
    const Stance stance = stanceOf(c, now);
    // A swing kick - a dive's blow - goes through a baton's guard (guards::kickPasses; guard.txt
    // `swingkick`). The others stop it as they stop a punch; `kickThrough` is the case in which it was the
    // kick that let the blow in, for the log.
    const bool kickGoes = strike.kick && kickOn.load(std::memory_order_relaxed) &&
                          guards::kickPasses(kind, (c.traits & game_targets::guardSwordsman) != 0);
    const bool kickThrough = kickGoes && stance == Stance::Stands;
    b.open = stance != Stance::Stands || kickGoes;
    // A runner's guard is there only while he runs at the hero; a blow at him in any other state - or when
    // watch() has not seen him come at the hero fast enough - lands, and the log does not tell of it.
    const bool runState = kind == guards::Kind::Runner && game_targets::inRunningState(base, c);
    const bool visible = kind != guards::Kind::Runner || runState;
    bool newWeb{};
    double closing = -1., runFrom{};
    AcquireSRWLockExclusive(&lock);
    // A blow that finds him in a webbed state makes the web last (webbedSeconds) from this moment.
    if (stance == Stance::Webbed)
        newWeb = book.webbed(c.record, c.handle, kind, now);
    if (kind == guards::Kind::Runner) {
        closing = book.closing(c.record, c.handle);
        runFrom = static_cast<double>(book.tuning().runSpeed);
        if (!(runState && book.runner(c.record, c.handle, now)))
            b.open = true;
    }
    const guards::Outcome out = book.blow(c.record, c.handle, kind, b, now);
    // The hero's blow at him is a sign that he has detected the hero (after the verdict: the first blow at an
    // enemy who had not is the one that lands).
    book.notice(c.record, c.handle, kind, now);
    const uint32_t blocksNow = book.blocks(c.record, c.handle);
    if (out.verdict == guards::Verdict::Blocked) {
        ++counters.blocks;
        counters.lastHand = strike.hand;
        counters.lastKind = static_cast<uint32_t>(kind);
        counters.lastSound =
            static_cast<uint32_t>(guards::soundOf(kind, (c.traits & game_targets::guardSwordsman) != 0)) |
            (strike.attacking ? lastAttacking : 0u);
        counters.lastStrength = strike.strength;
        counters.lastTarget = c.record;
        counters.lastPoint = strike.point;
        // How many blocks in a row he has taken and how many make him swing back, for the BLOCKED mark.
        counters.lastStreak = static_cast<uint16_t>((std::min)(out.streak, 999u));
        const float after = book.tuning().counterBlows;
        counters.counterBlows = after >= 1.f ? static_cast<uint16_t>((std::min)(after, 999.f)) : uint16_t{0};
    }
    if (out.retaliates) {
        ++counters.counters;
        counters.counterTarget = c.record;
    }
    if (now > pruneAt) {
        book.prune(now);
        pruneAt = now + 60;
    }
    ReleaseSRWLockExclusive(&lock);
    if (newWeb && kind != guards::Kind::Runner)
        logWeb(c, kind, "a blow found him webbed");
    if (visible)
        logBlow(c, kind, strike, stance, out, closing, kickThrough);
    else if (out.verdict == guards::Verdict::Lands && closing >= runFrom)
        logFast(c, closing);
    char line[200];
    if (out.verdict == guards::Verdict::Blocked && blocksNow == 1) {
        std::snprintf(line, sizeof(line), "GUARD #%u %s guard blocked a blow (strength %.2f)",
                      c.handle & 0xfffff, guards::kindName(kind), strike.strength);
        game_guard::log(line);
    }
    if (out.retaliates) {
        std::snprintf(line, sizeof(line),
                      "GUARD #%u %s guard answers %u blocked punches in a row with a counter",
                      c.handle & 0xfffff, guards::kindName(kind), out.streak);
        game_guard::log(line);
    }
    return out;
}

void game_guard::dodgeBegan(Vec3 heroFeet) {
    if (!on.load(std::memory_order_relaxed))
        return;
    dodge = {true, heroFeet, nowSeconds() + dodgeWindow};
}

void game_guard::watch(const std::vector<game_targets::Candidate>& bots, Vec3 heroFeet, float worldScale) {
    if (!on.load(std::memory_order_relaxed))
        return;
    const double now = nowSeconds();
    if (now - watchedAt < .05)
        return;
    watchedAt = now;
    // Whether an enemy who has not detected the hero is to be told from one who has (guard.txt `detect`).
    const bool detecting = detectOn.load(std::memory_order_relaxed);
    // The dodge in progress: its way on the ground, once it has carried the hero far enough to be one.
    Vec3 way{};
    bool dodging{};
    if (dodge.on && now >= dodge.until)
        dodge.on = false;
    if (dodge.on) {
        way = heroFeet - dodge.from;
        way.y = 0;
        const float run = length(way);
        if (run >= dodgeRun) {
            way = way / run;
            dodging = true;
        }
    }
    for (const auto& c : bots) {
        // Every enemy is followed - a runner's guard is the one any enemy may have - but an enemy's guard
        // that is only a runner's is no guard to see: nothing about it goes to the headset or to the log.
        const guards::Kind kind = bookKind(c);
        if (kind == guards::Kind::None || !game_targets::up(base, c))
            continue;
        const bool seen = kind != guards::Kind::Runner;
        float hp{};
        const bool haveHp = game_targets::health(c, hp);
        Vec3 at{};
        const bool haveAt = game_targets::position(c, at);
        bool over{};
        if (dodging && haveAt) {
            Vec3 ahead = at - dodge.from, beyond = heroFeet - at;
            ahead.y = beyond.y = 0;
            const float along = dot(ahead, way), past = dot(beyond, way);
            const Vec3 off = ahead - way * along;
            over = along > dodgeAhead && past > dodgePast && length(off) < dodgeReach;
        }
        // Every moment he is seen in a webbed state begins the webbedSeconds again (a new web is written to
        // the log).
        const bool webbedNow = game_targets::inWebbedState(base, c);
        // A runner's run at the hero: whether his state is a running one, and where he is (Book::running
        // measures his own speed along the line to the hero from the samples). Followed for every enemy while
        // `detect` is on - a run at the hero is a sign that he has detected him - and the name of his state
        // is read once, for that and for the signs below.
        std::string state;
        if (detecting || !seen)
            game_targets::stateName(base, c, state);
        const bool moving = haveAt && guards::runningState(state);
        // Signs that he has detected the hero: a behavior of a fight on him (the scan's trait), a state of a
        // fight, and (below, with the book) a run at the hero or a fall of his hit points.
        const bool engaged = detecting && (c.traits & game_targets::guardEngaged) != 0;
        const bool fights = detecting && guards::fightState(state);
        bool hurtNow{}, dodgedNow{}, webbedFresh{}, ran{};
        guards::Book::Change change{};
        double followed{}, hold{};
        AcquireSRWLockExclusive(&lock);
        if (webbedNow)
            webbedFresh = book.webbed(c.record, c.handle, kind, now);
        if (haveHp)
            hurtNow = book.hurt(c.record, c.handle, kind, hp, now);
        if (over)
            dodgedNow =
                book.open(c.record, c.handle, kind, static_cast<double>(book.tuning().dodgeSeconds), now);
        if ((detecting || !seen) && haveAt)
            ran = book.running(c.record, c.handle, kind, static_cast<double>(at.x), static_cast<double>(at.z),
                               static_cast<double>(heroFeet.x), static_cast<double>(heroFeet.z), moving, now,
                               static_cast<double>(worldScale));
        if (detecting) {
            if (engaged || fights || ran)
                book.notice(c.record, c.handle, kind, now);
            change = book.detection(c.record, c.handle, now);
            followed = book.followedFor(c.record, c.handle, now);
            hold = static_cast<double>(book.tuning().detectSeconds);
        }
        if ((hurtNow || dodgedNow) && seen) {
            ++counters.breaks;
            counters.lastCause =
                static_cast<uint32_t>(dodgedNow && !hurtNow ? guards::Cause::Dodge : guards::Cause::Stagger);
            counters.openedTarget = c.record;
        }
        ReleaseSRWLockExclusive(&lock);
        if (change == guards::Book::Change::Detected && followed >= 1.5)
            logDetect(c, kind, true,
                      engaged  ? "a behavior of a fight on him"
                      : fights ? "a state of a fight"
                      : ran    ? "he runs at the hero"
                               : "a blow or a web, or a fall of his hit points",
                      followed);
        else if (change == guards::Book::Change::Lost)
            logDetect(c, kind, false, nullptr, hold);
        if (webbedFresh && seen)
            logWeb(c, kind, "seen webbed");
        if ((hurtNow || dodgedNow) && seen) {
            char line[200];
            std::snprintf(
                line, sizeof(line), "GUARD #%u %s guard opened by %s (hit points %.0f)", c.handle & 0xfffff,
                guards::kindName(kind),
                guards::causeName(dodgedNow && !hurtNow ? guards::Cause::Dodge : guards::Cause::Stagger),
                haveHp ? hp : -1.f);
            game_guard::log(line);
        }
    }
    // The book has an entry for every enemy met now, not only the ones with a guard of their own, and a fight
    // can go on for a long time without a blow that prunes it (blow() does): forget the ones not seen for a
    // while from here too.
    AcquireSRWLockExclusive(&lock);
    if (now > pruneAt) {
        book.prune(now);
        pruneAt = now + 60;
    }
    ReleaseSRWLockExclusive(&lock);
}

void game_guard::refused(const game_targets::Candidate& c) {
    const ULONGLONG t = GetTickCount64();
    // The sound of his guard goes to the headset with the count (a sound for "the web lands, the enemy is
    // blocking, and they cant pull"), one of the same four a blocked blow's is. (A runner who is refused has
    // the baton's, or the sword's if he is a swordsman: the sound of a guard that is no more than a
    // runner's.)
    const guards::Kind kind = bookKind(c);
    const guards::Sound sound = guards::soundOf(kind, (c.traits & game_targets::guardSwordsman) != 0);
    bool count{}, announce{};
    AcquireSRWLockExclusive(&lock);
    if (t - lastRefusalAt >= 500) {
        lastRefusalAt = t;
        ++counters.refusals;
        counters.refusedTarget = c.record;
        counters.refusedSound = static_cast<uint32_t>(sound);
        count = true;
        announce = lastRefusedRecord != c.record;
        lastRefusedRecord = c.record;
    }
    ReleaseSRWLockExclusive(&lock);
    if (count && announce) {
        char line[200];
        std::snprintf(line, sizeof(line),
                      "GUARD #%u a web pull on a guarding %s was refused (%s web-block sound)",
                      c.handle & 0xfffff, guards::kindName(kind), guards::soundName(sound));
        game_guard::log(line);
    }
}

void game_guard::own(const game_targets::Candidate& c) {
    if (!on.load(std::memory_order_relaxed))
        return;
    const guards::Kind kind = bookKind(c);
    if (kind == guards::Kind::None)
        return;
    const double now = nowSeconds();
    AcquireSRWLockExclusive(&lock);
    book.own(c.record, c.handle, kind, now);
    book.notice(c.record, c.handle, kind,
                now); // the mod's own reaction on him - he is in a fight with the hero
    ReleaseSRWLockExclusive(&lock);
}

void game_guard::ballHit(const game_targets::Candidate& c) {
    if (!on.load(std::memory_order_relaxed))
        return;
    // A web ball that hit him is the mod's own damage (the fall of his hit points it causes is no stagger
    // from elsewhere) and opens nothing: one or two balls only twitch an enemy, and he runs on. He is webbed
    // when his state says so (watch()).
    const guards::Kind kind = bookKind(c);
    if (kind == guards::Kind::None)
        return;
    const double now = nowSeconds();
    AcquireSRWLockExclusive(&lock);
    book.own(c.record, c.handle, kind, now);
    book.notice(c.record, c.handle, kind, now); // a web ball at him is a sign that he has detected the hero
    ReleaseSRWLockExclusive(&lock);
    if (kind != guards::Kind::Runner)
        logBall(c, kind);
}

void game_guard::taken(const game_targets::Candidate& c) {
    if (!on.load(std::memory_order_relaxed))
        return;
    const guards::Kind kind = kindOf(c);
    if (kind == guards::Kind::None || takenLogged.load(std::memory_order_relaxed) >= takenLogLimit)
        return;
    const ULONGLONG t = GetTickCount64();
    const bool again = lastTakenRecord.exchange(c.record, std::memory_order_relaxed) == c.record &&
                       t - lastTakenAt.load(std::memory_order_relaxed) < 3000;
    lastTakenAt.store(t, std::memory_order_relaxed);
    if (again)
        return;
    takenLogged.fetch_add(1, std::memory_order_relaxed);
    const double now = nowSeconds();
    const Stance stance = stanceOf(c, now);
    AcquireSRWLockShared(&lock);
    const bool standing = book.standing(c.record, c.handle, kind, now);
    ReleaseSRWLockShared(&lock);
    std::string state;
    game_targets::stateName(base, c, state);
    char line[200];
    std::snprintf(line, sizeof(line), "GUARD #%u a web pull took a %s guard: %s, his state: %s",
                  c.handle & 0xfffff, guards::kindName(kind),
                  stance != Stance::Stands ? stanceName(stance)
                  : !standing              ? "open (a dodge or a fall of hit points)"
                                           : "his guard was up (?)",
                  state.empty() ? "?" : state.c_str());
    game_guard::log(line);
}

void game_guard::counterLanded(const game_targets::Candidate& c) {
    AcquireSRWLockExclusive(&lock);
    ++counters.counterHits;
    ReleaseSRWLockExclusive(&lock);
    char line[200];
    std::snprintf(line, sizeof(line), "GUARD #%u counter reached the hero (a stagger, no damage)",
                  c.handle & 0xfffff);
    game_guard::log(line);
}

Report game_guard::report() {
    AcquireSRWLockShared(&lock);
    Report out = counters;
    ReleaseSRWLockShared(&lock);
    out.status = on.load(std::memory_order_relaxed) ? 1u : 0u;
    out.recoil = recoilMetres.load(std::memory_order_relaxed);
    out.pose = poseOn.load(std::memory_order_relaxed) ? 1u : 0u;
    return out;
}

extern "C" DWORD WINAPI SpidyGuardNote(void* request) {
    if (!request)
        return 1;
    auto* n = static_cast<game_guard::Note*>(request);
    return game_guard::note(n->seen, n->text, sizeof(n->text)) ? 0 : 1;
}

extern "C" DWORD WINAPI SpidyGuardSample(void* out) {
    const Report sample = game_guard::report();
    __try {
        std::memcpy(out, &sample, sizeof(sample));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 9603;
    }
    return 0;
}
