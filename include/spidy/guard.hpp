#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

// Enemy types: the enemies that block in the base game block here too - the ones with a baton, the shield
// bearers, the brutes and the swordsmen. The block is the mod's own rule, in plain code with no dependence on
// the game's block internals:
//
//   - Some enemies are guarding: a baton's guard (an enemy with a baton or another melee weapon:
//     BotMeleeWeaponManager), a brute's (a heavy, armored or brawling one), a shield's, a swordsman's (a
//     baton's guard with a sword's sound). Which ones come from the class names of an enemy's components
//     (game_targets reads them from the game's own type information) and, if guard.txt asks for it, from his
//     hit points.
//   - A plain blow on a guarding enemy (a fist, a web strike's) is blocked: it does nothing to him.
//   - Nothing the fists do breaks a guard, however long they keep punching: a guard opens when something
//   other than the
//     hero's fists hurts the enemy - a gadget, a thrown prop, a thrown enemy: his hit points fall and none of
//     the mod's own blows is on him - and when the hero's dodge carries him over or past the enemy. An opened
//     guard stays open a few seconds, then he guards again.
//   - Blocked punches in a row make him answer. After `counterBlows` of them (3) within `counterWindow`
//   seconds of
//     one another he swings back at the hero, as the base game's guarding enemies do: it does no damage and
//     only staggers the hero (game_punch). Book::blow says so in Outcome::retaliates.
//   - Nobody guards who is down, stunned, webbed, held or in the air. An enemy in the middle of an attack
//   DOES guard -
//     a blow at him is blocked and he finishes his attack (`touch on`: the blow staggers him and the attack
//     is stopped if the game lets it be) - and a webbed one never does, however lately a blow of the hero's
//     reached him, and not for webbedSeconds after the last moment he was seen webbed either (a blow that
//     lands on a webbed enemy may put him in a hit reaction between the web states, and the next punch must
//     not find him free).
//   - A guarding enemy cannot be dragged by a web (game_grab's takes()).
//   - Each guard has its sound (Sound): a baton's for the ordinary melee-weapon guards, a sword's for the
//   swordsmen, a
//     slayer's for the brutes and a shield's.
//   - An enemy who has not DETECTED the hero does not guard - a blow at him lands and a web pull takes him.
//   The game
//     keeps no flag of it that the mod can read, so it is read from what he does and has (Book::notice): a
//     behavior of a fight on his actor (fightBehavior: BehaviorMeleeGame, BehaviorRangedCombat,
//     BehaviorStandAndShoot, BehaviorReactGame ...), a state that is no calm one (fightState: Melee, Shuffle,
//     Aim, a hit reaction, a web ...), a run at the hero, a fall of his hit points, and the hero's own blow,
//     web ball or web at him. Each keeps him "detected" for Tuning::detectSeconds (10) after the last time it
//     was seen; with none he has not detected the hero (and an enemy the book has met but never seen a sign
//     of has not either). guard.txt `detect off` has every enemy with a guard guard from the first moment.
//   - A swing kick - the dive, the feet-first blow of the hero's own flight at an enemy (WebStrike::dive;
//   game_punch's
//     land() knows it by its hand, WebStrike::diveHand) - goes through the guard of an enemy whose block is
//     the baton's (kickPasses). The brutes', the swordsmen's and the shields' guards stop it as they stop a
//     punch. guard.txt `swingkick off` has the baton guards stop it too.
//   - An enemy RUNNING AT THE HERO may guard for as long as he runs, whether or not his classes give him a
//   guard
//     (Kind::Runner; guard.txt `runners on`, off by default): a blow at him is blocked and a web pull
//     refused, and he runs on, for the mod cannot make the game's AI stop a run. Which states are running
//     ones is runningState(); how fast he must be coming at the hero for it (a walk is no run) is
//     Tuning::runSpeed, measured from his own movement by Book::running().
//
// The logic here is plain C++ with no game in it (tests/guard_tests.cpp): the classes, the rules file, and
// the book of each enemy's guard. game_guard.cpp runs it in the game.
namespace spidy::guards {
// What an enemy's guard is. The stronger kind wins when an enemy has more than one. (Runner is no guard of an
// enemy's own but the one any enemy has while he runs at the hero; it is the weakest, whatever its number:
// the numbers of the others stay as they are, for the report the headset reads.)
enum class Kind : std::uint8_t { None = 0, Weapon = 1, Brawler = 2, Shield = 3, Runner = 4 };
inline const char* kindName(Kind kind) {
    switch (kind) {
    case Kind::Weapon:
        return "baton";
    case Kind::Brawler:
        return "brute";
    case Kind::Shield:
        return "shield";
    case Kind::Runner:
        return "runner";
    default:
        return "none";
    }
}
// How strong a guard is, for stronger(): none, a runner's, a baton's, a brute's, a shield's.
inline int guardRank(Kind kind) {
    switch (kind) {
    case Kind::Runner:
        return 1;
    case Kind::Weapon:
        return 2;
    case Kind::Brawler:
        return 3;
    case Kind::Shield:
        return 4;
    default:
        return 0;
    }
}
inline Kind stronger(Kind a, Kind b) {
    return guardRank(a) >= guardRank(b) ? a : b;
}
// Which of the four block sounds an enemy's guard makes (batonblock, swordblock, slayerblock and
// shieldblock). A Shield guard has the shield's. A swordsman (the classes of SwordsmanBot and the like) has
// the sword's whatever else he is. A brute (a Brawler guard: a brute, a gauntlet, armored, a slayer, or a lot
// of hit points) has the slayer's; every other guard is an ordinary melee guard with a baton's.
enum class Sound : std::uint8_t { Baton = 0, Sword = 1, Slayer = 2, Shield = 3 };
inline const char* soundName(Sound sound) {
    switch (sound) {
    case Sound::Sword:
        return "sword";
    case Sound::Slayer:
        return "slayer";
    case Sound::Shield:
        return "shield";
    default:
        return "baton";
    }
}
inline Sound soundOf(Kind kind, bool swordsman) {
    if (kind == Kind::Shield)
        return Sound::Shield;
    if (swordsman)
        return Sound::Sword;
    return kind == Kind::Brawler ? Sound::Slayer : Sound::Baton;
}
// A swing kick - the dive, the blow of the hero's own flight at an enemy, feet first - goes through the guard
// of an enemy whose block is the baton's: an ordinary melee-weapon guard (a Weapon guard that is no
// swordsman's). A brute's, a swordsman's, a shield's and a runner's stop it as they stop a punch.
inline bool kickPasses(Kind kind, bool swordsman) {
    return kind == Kind::Weapon && soundOf(kind, swordsman) == Sound::Baton;
}
// Why a guard opened (other than the enemy's own state).
enum class Cause : std::uint8_t { None = 0, Stagger = 1, Dodge = 2 };
inline const char* causeName(Cause cause) {
    switch (cause) {
    case Cause::Stagger:
        return "stagger";
    case Cause::Dodge:
        return "dodge";
    default:
        return "none";
    }
}

// The numbers a guard runs on (guard.txt can change them).
struct Tuning {
    // His counter. This many blocked blows in a row (each no more than counterWindow seconds after the one
    // before) make him swing back at the hero; once he has, counterCooldown seconds pass before he can again.
    // 0: never counters.
    float counterBlows = 3.f, counterWindow = 3.f, counterCooldown = 2.5f;
    // A guard stays open this long after something other than the hero's fists takes staggerDamage hit points
    // or more from the enemy (a gadget, a thrown prop or enemy), and after the hero dodges over or past him.
    // 0: never.
    float staggerSeconds = 4.f, dodgeSeconds = 3.f;
    float staggerDamage = 3.f;
    // An enemy seen in a webbed state stays open this long after the last time he was seen in one: a blow
    // that lands on a webbed enemy may make the game put him in a hit reaction between the web states, and
    // that reaction is no webbed state by name, so without the hold he looks free to the very next punch. 0:
    // no hold. The hold begins at a moment he is SEEN in a webbed state (a web ball that only twitches him is
    // no web: game_guard::ballHit), and does not hold while he is running (runningState).
    float webbedSeconds = 4.f;
    // The mod's own blow on an enemy keeps the fall of his hit points that follows from counting as a stagger
    // this long (its damage reaches him a physics step later).
    float ownSeconds = .6f;
    // An enemy is running at the hero when his state is a running one (runningState) and his own movement
    // along the line to the hero is at least runSpeed metres a second (a walk is 1 to 1.5, a thug's run
    // several times that), seen in the last runGrace seconds (the samples come every twentieth of a second or
    // so).
    float runSpeed = 2.f, runGrace = .35f;
    // An enemy counts as having detected the hero for this many seconds after the last sign of it
    // (Book::notice).
    float detectSeconds = 10.f;
};

// What guard.txt says, and what is built in. The words are parts of the undecorated class name of a component
// (BotMeleeWeaponManager), case counted.
struct Rules {
    bool enabled = true;
    // A blocked blow makes the enemy react with the game's own hit reaction (the smallest, Twitch, with no
    // damage to speak of; a Stagger when he is in the middle of an attack). OFF by default: a guard that
    // reels at every punch does not look like one that blocks, and the mod's own block animation
    // (guard_pose.hpp: the arms up, the body jolted by the blow) is what shows the punch meeting him. `touch
    // on` brings the reaction back.
    bool touch = false;
    // The enemy's guard is seen. A blocked punch or a refused web pull puts his arms up, and his counter is a
    // swing of his arm (guard_pose.hpp): the mod's own pose on his skeleton. `pose off`: not at all.
    bool pose = true;
    // An enemy running at the hero guards while he runs, whatever his classes give him (Kind::Runner). OFF by
    // default: `runners on` turns it on. Off, only the enemies with a guard of their own guard.
    bool runners = false;
    // An enemy who has not detected the hero does not guard (Book::noticed). `detect off`: he guards all the
    // same.
    bool detect = true;
    // A swing kick (the dive) goes through a baton's guard (kickPasses). `swingkick off`: it is blocked like
    // a punch.
    bool swingKick = true;
    // A blocked blow knocks the player's view back this far, in centimetres (recoil.hpp): a tiny bit, for the
    // feel of it. `recoil 0` turns it off, `recoil 6` doubles it.
    float recoil = 3.f;
    // Extra parts of a class name that give an actor a guard of that kind, and parts that take every guard
    // from an actor with such a class.
    std::vector<std::string> weapon, brawler, shield, exempt;
    // Parts of a class name that mark a swordsman, whose block has the sword's sound (and who guards as a
    // baton's).
    std::vector<std::string> swordsman;
    // An enemy of this many hit points or more (his health component's maximum) guards as a Brawler, whatever
    // his classes say: the heavy ones are the tough ones. 0 (the default): never - until the census lines of
    // the game's hit points say which number tells the heavy ones from the rest.
    float hp = 0.f;
    Tuning tuning;
};

// One class: the kind of guard it gives its actor, and whether it takes every guard from it.
struct ClassVerdict {
    Kind kind = Kind::None;
    bool exempt = false;
    // The class marks a swordsman (soundOf, kickPasses).
    bool swordsman = false;
};
inline bool contains(std::string_view name, std::string_view part) {
    return !part.empty() && name.find(part) != std::string_view::npos;
}
// A class by its name without the decoration ("BotMeleeWeaponManager"). The built-in parts: a melee weapon's
// manager and a baton for a baton's guard (the enemies with BotMeleeWeaponManager are the ones with batons
// and other melee weapons), a brawler, a brute, armored health, a gauntlet and a slayer (SlayerBot, the heavy
// with the ground pound) for a brute's, a shield for a shield's, a swordsman for a swordsman's (SwordsmanBot:
// a baton's guard with the sword's sound). A human shield (DamageModifierHostageShield: a thug who holds a
// hostage) is no shield's guard.
inline ClassVerdict classOf(std::string_view name, const Rules& rules) {
    static constexpr std::string_view weapon[] = {"MeleeWeaponManager", "Baton"};
    static constexpr std::string_view brawler[] = {"Brawl", "Brute", "ArmoredHealth", "Gauntlet", "Slayer"};
    static constexpr std::string_view shield[] = {"Shield"};
    static constexpr std::string_view swordsman[] = {"Swordsman"};
    static constexpr std::string_view exempt[] = {"HostageShield"};
    ClassVerdict out;
    for (const auto part : exempt)
        if (contains(name, part))
            out.exempt = true;
    for (const auto& part : rules.exempt)
        if (contains(name, part))
            out.exempt = true;
    for (const auto part : swordsman)
        if (contains(name, part)) {
            out.swordsman = true;
            out.kind = stronger(out.kind, Kind::Weapon);
        }
    for (const auto& part : rules.swordsman)
        if (contains(name, part)) {
            out.swordsman = true;
            out.kind = stronger(out.kind, Kind::Weapon);
        }
    for (const auto part : weapon)
        if (contains(name, part))
            out.kind = stronger(out.kind, Kind::Weapon);
    for (const auto& part : rules.weapon)
        if (contains(name, part))
            out.kind = stronger(out.kind, Kind::Weapon);
    for (const auto part : brawler)
        if (contains(name, part))
            out.kind = stronger(out.kind, Kind::Brawler);
    for (const auto& part : rules.brawler)
        if (contains(name, part))
            out.kind = stronger(out.kind, Kind::Brawler);
    for (const auto part : shield)
        if (contains(name, part))
            out.kind = stronger(out.kind, Kind::Shield);
    for (const auto& part : rules.shield)
        if (contains(name, part))
            out.kind = stronger(out.kind, Kind::Shield);
    return out;
}
// A bot state class (BotState...) that has him staggered, stunned, webbed or knocked about - his guard is
// open in it. The names are the words any such state would carry. Not "Shock": a BotStateShockwaveAttack or a
// BotStateShockJump is the enemy's own slam attack, and a guard holds in the middle of an attack. A shocked
// enemy is in a Stun state (BotStateElectricStun).
inline bool opensGuard(std::string_view name) {
    static constexpr std::string_view parts[] = {"Stagger",     "Stun",   "Knock",  "Ragdoll", "GetUp",
                                                 "WebStruggle", "Webbed", "Flinch", "Dazed",   "Stumble"};
    if (name.substr(0, 8) != "BotState")
        return false;
    for (const auto part : parts)
        if (name.find(part) != std::string_view::npos)
            return true;
    return false;
}
// A bot state class (BotState...) of an enemy on the move at the hero: the game's chase states
// (BotStateChaseRunTo, ChaseJumpTo, ChaseWallRun, ChaseSlideTo, ChaseSpecialGoTo, ChaseVaultOver,
// ChaseHookshotTo, and their Local and Remote halves), its go-to states (BotStateGoto, GotoGame, GotoLocal,
// GotoRemote) and go-direction ones (BotStateGoDirection and the Game, Local and Remote ones), and the plain
// dash (BotStateDash). The names are those the game's type information lists. Not the attacks that run at him
// - BotStateDashAttack, Lunge, Pounce, a charge: an attack is an attack, and a runner who has begun one is no
// runner. The class's own name is the one to give, not those of the classes it derives from (a dash attack
// may derive from the dash). Whether he is coming at the hero is another matter: Book::running.
inline bool runningState(std::string_view name) {
    if (name.substr(0, 8) != "BotState")
        return false;
    name.remove_prefix(8);
    return name.substr(0, 5) == "Chase" || name.substr(0, 4) == "Goto" ||
           name.substr(0, 11) == "GoDirection" || name == "Dash";
}
// A bot state class that has him webbed or caught in a web (WebStruggle, WebAttached, WebYank, ... every
// state of the game with Web in its name). A webbed enemy never guards, whatever the mod's own blows did
// lately.
inline bool webbedState(std::string_view name) {
    return name.substr(0, 8) == "BotState" && name.find("Web") != std::string_view::npos;
}
// A class of an enemy's actor that is a behavior of a fight. The game adds and drops these as an enemy goes
// into and out of one: BehaviorMeleeGame, BehaviorRangedCombat, BehaviorStandAndShoot, BehaviorUseCombo,
// BehaviorUseLosBlocker, BehaviorReactGame and BehaviorAttackResponse - and not BehaviorGotoGame, which a
// patrol's go-to is as well, nor the machinery every actor's behaviors run on (BehaviorMap, BehaviorHost,
// BehaviorHostCache, BehaviorEventQueue), which are no sign of anything. By the name without the decoration:
// a name with Behavior in it and a word of a fight (so a slayer's SlayerBotBehaviorMelee is one too).
inline bool fightBehavior(std::string_view name) {
    static constexpr std::string_view words[] = {
        "Melee", "Combat", "Ranged", "Shoot", "Combo",   "Attack", "React", "LosBlocker",
        "Fight", "Brawl",  "Charge", "Throw", "Grenade", "Cover",  "Chase", "Aim"};
    if (!contains(name, "Behavior"))
        return false;
    for (const auto word : words)
        if (contains(name, word))
            return true;
    return false;
}
// A class that is one of the game's behaviors, for the census line (what an enemy who has not detected the
// hero has on him, and what one who has): every name with Behavior in it but the machinery the behaviors of
// every actor run on.
inline bool isBehavior(std::string_view name) {
    if (!contains(name, "Behavior"))
        return false;
    return !(name == "BehaviorMap" || name == "BehaviorEventQueue" || name.substr(0, 12) == "BehaviorHost");
}
// A bot state class that an enemy who has not detected the hero can be in: standing, playing an animation,
// looking about, pointing at something, going somewhere (how fast he comes at the hero is judged apart:
// Book::running), facing, picking something up. Not StandUp: getting up is a knocked down enemy's. Every
// other state is a sign of a fight (fightState): a melee or a ranged one, shuffling round the hero, an
// attack, a block, a hit reaction, a stun, a web, a chase.
inline bool calmState(std::string_view name) {
    static constexpr std::string_view exact[] = {"Stand",       "Initial",       "Base",       "Null",
                                                 "PlayAnim",    "LookAtTargets", "PointAtPOI", "PickUpItem",
                                                 "PutDownItem", "RecoverHealth"};
    static constexpr std::string_view starts[] = {
        "Goto",          "GoDirection", "Face", "PlayRelativeAnim", "PlaySyncedAnimGuest",
        "PlayCinematic", "TreadWater"};
    if (name.substr(0, 8) != "BotState")
        return false;
    name.remove_prefix(8);
    for (const auto part : exact)
        if (name == part)
            return true;
    for (const auto part : starts)
        if (name.substr(0, part.size()) == part)
            return true;
    return false;
}
inline bool fightState(std::string_view name) {
    return name.size() > 8 && name.substr(0, 8) == "BotState" && !calmState(name);
}
// A decorated class name (".?AVBotMeleeWeaponManager@@") to the plain one.
inline std::string_view plainName(std::string_view decorated) {
    if (decorated.size() > 4 && decorated[0] == '.' && decorated[1] == '?' && decorated[2] == 'A' &&
        (decorated[3] == 'V' || decorated[3] == 'U'))
        decorated.remove_prefix(4);
    const auto end = decorated.find('@');
    return end == std::string_view::npos ? decorated : decorated.substr(0, end);
}

namespace detail {
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}
inline bool sameWord(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        if (x != b[i])
            return false;
    }
    return true;
}
inline bool number(std::string_view s, float& out) {
    const std::string text(s);
    char* end{};
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != 0 || !(v == v) || v > 1e6f || v < -1e6f)
        return false;
    out = v;
    return true;
}
} // namespace detail

// guard.txt. One setting a line; '#' starts a comment; words are not case-sensitive, names are:
//   off | on               the whole guard system
//   touch on | off         a blocked blow makes the enemy flinch (the game's own hit reaction; a stagger if
//   he is
//                          attacking). Off by default: his arms come up and his body jolts instead
//   pose on | off          the enemy puts his arms up for a blocked punch or a refused web pull, and swings
//                          his arm for the counter (on by default)
//   counter N              this many blocked punches in a row make him swing back at the hero: no damage,
//   only
//                          a stagger; 0: never (3 by default)
//   runners on | off       an enemy running at you guards while he runs, whatever he is (off by default)
//   runspeed N             how fast he must be coming at you, metres a second, to count as running (2 by
//   default; a
//                          walk is about 1 to 1.5)
//   detect on | off        an enemy who has not detected you does not guard (on by default)
//   detecthold SECONDS     how long an enemy counts as having detected you after the last sign of it (10 by
//   default) swingkick on | off     a swing kick (the dive) goes through a baton's guard; a brute's, a
//   swordsman's and a
//                          shield's stop it all the same (on by default)
//   stagger SECONDS        how long a guard stays open after a gadget or a thrown prop hurts him; 0: never
//   dodge SECONDS          how long a guard stays open after the hero dodges over him; 0: never
//   webbed SECONDS         how long a webbed enemy stays open after the last moment he was seen webbed; 0:
//   only
//                          while his state says webbed
//   baton NAME             a component class whose name has NAME gives its enemy a baton's guard
//   brute NAME             ... a brute's guard
//   shield NAME            ... a shield's guard
//   swordsman NAME         ... a swordsman's guard: a baton's with the sword's sound
//   exempt NAME            an enemy with such a class never guards
//   hp NUMBER              the hit points from which an enemy guards as a brute; 0 turns it off (the default)
//   recoil N              how far a blocked blow knocks your view back, centimetres (3 by default; 0: not at
//   all)
// Returns how many lines were not understood (the rules read up to them stand).
inline int parseRules(std::string_view text, Rules& rules) {
    int unknown{};
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (const auto hash = line.find('#'); hash != std::string_view::npos)
            line = line.substr(0, hash);
        line = detail::trim(line);
        if (line.empty())
            continue;
        const auto space = line.find_first_of(" \t");
        const std::string_view key = line.substr(0, space);
        const std::string_view value =
            space == std::string_view::npos ? std::string_view{} : detail::trim(line.substr(space + 1));
        float n{};
        if (detail::sameWord(key, "off") && value.empty()) {
            rules.enabled = false;
        } else if (detail::sameWord(key, "on") && value.empty()) {
            rules.enabled = true;
        } else if (detail::sameWord(key, "touch") &&
                   (detail::sameWord(value, "on") || detail::sameWord(value, "off"))) {
            rules.touch = detail::sameWord(value, "on");
        } else if (detail::sameWord(key, "pose") &&
                   (detail::sameWord(value, "on") || detail::sameWord(value, "off"))) {
            rules.pose = detail::sameWord(value, "on");
        } else if (detail::sameWord(key, "counter") && detail::number(value, n) &&
                   (n == 0 || (n >= 1 && n <= 20))) {
            rules.tuning.counterBlows = n;
        } else if (detail::sameWord(key, "runners") &&
                   (detail::sameWord(value, "on") || detail::sameWord(value, "off"))) {
            rules.runners = detail::sameWord(value, "on");
        } else if (detail::sameWord(key, "runspeed") && detail::number(value, n) && n >= .5f && n <= 12) {
            rules.tuning.runSpeed = n;
        } else if (detail::sameWord(key, "detect") &&
                   (detail::sameWord(value, "on") || detail::sameWord(value, "off"))) {
            rules.detect = detail::sameWord(value, "on");
        } else if (detail::sameWord(key, "detecthold") && detail::number(value, n) && n >= .5f && n <= 600) {
            rules.tuning.detectSeconds = n;
        } else if (detail::sameWord(key, "swingkick") &&
                   (detail::sameWord(value, "on") || detail::sameWord(value, "off"))) {
            rules.swingKick = detail::sameWord(value, "on");
        } else if (detail::sameWord(key, "recoil") && detail::number(value, n) && n >= 0 && n <= 15) {
            rules.recoil = n;
        } else if (detail::sameWord(key, "stagger") && detail::number(value, n) && n >= 0 && n <= 120) {
            rules.tuning.staggerSeconds = n;
        } else if (detail::sameWord(key, "dodge") && detail::number(value, n) && n >= 0 && n <= 120) {
            rules.tuning.dodgeSeconds = n;
        } else if (detail::sameWord(key, "webbed") && detail::number(value, n) && n >= 0 && n <= 120) {
            rules.tuning.webbedSeconds = n;
        } else if ((detail::sameWord(key, "baton") || detail::sameWord(key, "weapon")) && !value.empty()) {
            rules.weapon.emplace_back(value);
        } else if ((detail::sameWord(key, "brute") || detail::sameWord(key, "brawler")) && !value.empty()) {
            rules.brawler.emplace_back(value);
        } else if (detail::sameWord(key, "shield") && !value.empty()) {
            rules.shield.emplace_back(value);
        } else if (detail::sameWord(key, "swordsman") && !value.empty()) {
            rules.swordsman.emplace_back(value);
        } else if (detail::sameWord(key, "exempt") && !value.empty()) {
            rules.exempt.emplace_back(value);
        } else if (detail::sameWord(key, "hp") && detail::number(value, n) && n >= 0) {
            rules.hp = n;
        } else {
            ++unknown;
        }
    }
    return unknown;
}

// A blow of the hero's at an enemy, as the book judges it.
struct Blow {
    // The hero is in a state his guard's counter can stagger (on his feet). Off, a counter that is due waits
    // for the next blocked blow.
    bool mayCounter = true;
    // His guard is open at this moment: he is down, stunned, webbed, held or in the air. (Not in the middle
    // of an attack: he guards then too.)
    bool open{};
    // The mod gives him a reaction of its own for this blow if it is blocked (touch). Off (`touch off`, the
    // default), a block leaves no reaction of the mod's behind, and the state he is in is the game's.
    bool touches = true;
    // How hard the fist was thrown, 0..1.
    float strength{};
};
enum class Verdict : std::uint8_t {
    Lands,  // the blow is his to take
    Blocked // the guard stopped it
};
struct Outcome {
    Verdict verdict = Verdict::Lands;
    // This blocked blow is the one that sets off his counter.
    bool retaliates{};
    // Blocked blows in a row, this one included (0 when it was not blocked).
    std::uint32_t streak{};
};

// Each enemy's guard, by his actor record and the handle of his marker component (a record's address can be
// the next enemy's: a different handle starts over).
class Book {
  public:
    struct Entry {
        std::uint64_t record{};
        std::uint32_t handle{};
        double brokenUntil{}, seen{}, ownAt = -1e9;
        // When the mod itself last gave him a reaction (a blocked blow's touch, a counter's break): a state
        // he enters just then is its own and does not open his guard. Blows that landed leave this alone.
        double touchAt = -1e9;
        // When he was last seen in a webbed state (webbed()).
        double webbedAt = -1e9;
        // The blocked blows in a row and when the latest was, and when his last counter began.
        double streakAt = -1e9, counterAt = -1e9;
        // Where he stood on the ground at the latest sample (running()) and when, and when he was last seen
        // running at the hero.
        double pastX{}, pastZ{}, pastAt = -1e9, runAt = -1e9;
        // How fast he comes at the hero, metres a second, smoothed.
        float closing{};
        float hp = -1.f;
        std::uint32_t blocks{}, streak{}, counters{};
        bool introduced{};
        // When the book first followed him, and when a sign that he has detected the hero was last seen
        // (notice()).
        double metAt{}, noticedAt = -1e9;
        // Whether detection() last found him detected (for the log of the changes).
        bool aware{};
    };
    explicit Book(Tuning tuning = {}) : tuning_(tuning) {}
    void tune(const Tuning& tuning) {
        tuning_ = tuning;
    }
    const Tuning& tuning() const {
        return tuning_;
    }
    // His guard is up now (his kind gives one, and it is not broken).
    bool standing(std::uint64_t record, std::uint32_t handle, Kind kind, double now) const {
        if (kind == Kind::None)
            return false;
        const Entry* e = find(record, handle);
        return !e || now >= e->brokenUntil;
    }
    // Seconds since the mod's own blow last reached him (1e9: none). A state he enters just after one may be
    // the blow's own flinch, not a stagger from elsewhere.
    double sinceOwn(std::uint64_t record, std::uint32_t handle, double now) const {
        const Entry* e = find(record, handle);
        return e ? now - e->ownAt : 1e9;
    }
    // Seconds since the mod last gave him a reaction of its own - a blow his guard blocked (the flinch it
    // gets), or own() (1e9: none). A state of the open kind that he enters just after one is that reaction,
    // not a stagger.
    double sinceTouched(std::uint64_t record, std::uint32_t handle, double now) const {
        const Entry* e = find(record, handle);
        return e ? now - e->touchAt : 1e9;
    }
    // He is in a webbed state at this moment (the state's name has Web in it). True when this is a new web:
    // he had not been seen in one for webbedGap seconds (the log says so once for each).
    bool webbed(std::uint64_t record, std::uint32_t handle, Kind kind, double now, double webbedGap = 1.5) {
        if (kind == Kind::None)
            return false;
        Entry& e = touch(record, handle, now);
        const bool fresh = now - e.webbedAt > webbedGap;
        e.webbedAt = now;
        e.streak = 0;
        return fresh;
    }
    // Seconds since he was last seen in a webbed state (1e9: never). While it is within Tuning::webbedSeconds
    // he is still webbed, whatever the state he is in just now.
    double sinceWebbed(std::uint64_t record, std::uint32_t handle, double now) const {
        const Entry* e = find(record, handle);
        return e ? now - e->webbedAt : 1e9;
    }
    // Where an enemy stands on the ground (x, z, metres) and where the hero does, at this moment - every
    // sample of game_guard::watch(). From the samples the book keeps how fast he is coming at the hero: his
    // own movement along the line to the hero, in metres a second of the world's time (worldScale: the game's
    // time scale, 1 or less in slow motion, so that a runner is one then too), smoothed over about a quarter
    // of a second. `moving`: his state is a running one (runningState). True - and he counts as running at
    // the hero (runner()) for Tuning::runGrace seconds from now - when he is moving and that speed is at
    // least Tuning::runSpeed. A gap of more than 0.6 s between samples, or a jump (more than 30 m/s: he was
    // put somewhere), starts the measure over. The hero's own movement is no part of it.
    bool running(std::uint64_t record, std::uint32_t handle, Kind kind, double x, double z, double heroX,
                 double heroZ, bool moving, double now, double worldScale = 1.) {
        if (kind == Kind::None)
            return false;
        Entry& e = touch(record, handle, now);
        const double dt = now - e.pastAt;
        if (dt >= .02 || dt < 0) { // sooner than that after the last sample there is no speed to tell
            const double scale = worldScale > .05 ? worldScale : .05;
            if (dt > 0 && dt <= .6) {
                const double toX = heroX - x, toZ = heroZ - z, far = std::sqrt(toX * toX + toZ * toZ);
                if (far > .05) {
                    const double along = ((x - e.pastX) * toX + (z - e.pastZ) * toZ) / far;
                    const double speed = along / (dt * scale);
                    if (std::fabs(speed) > 30.) {
                        e.closing = 0;
                    } else {
                        const double blend = dt < .25 ? dt / .25 : 1.;
                        e.closing += static_cast<float>((speed - static_cast<double>(e.closing)) * blend);
                    }
                }
            } else {
                e.closing = 0;
            }
            e.pastX = x;
            e.pastZ = z;
            e.pastAt = now;
        }
        const bool runs = moving && static_cast<double>(e.closing) >= static_cast<double>(tuning_.runSpeed);
        if (runs)
            e.runAt = now;
        return runs;
    }
    // running() saw him running at the hero within the last Tuning::runGrace seconds.
    bool runner(std::uint64_t record, std::uint32_t handle, double now) const {
        const Entry* e = find(record, handle);
        return e && now - e->runAt <= static_cast<double>(tuning_.runGrace);
    }
    // How fast he comes at the hero at the latest sample, metres a second (0: no measure yet).
    double closing(std::uint64_t record, std::uint32_t handle) const {
        const Entry* e = find(record, handle);
        return e ? static_cast<double>(e->closing) : 0.;
    }
    // The mod itself has just staggered or flinched him (a counter's break of his attack): the state he
    // enters is its own, for the grace sinceTouched tells of, and the blow it was part of is a mod blow for
    // hurt().
    void own(std::uint64_t record, std::uint32_t handle, Kind kind, double now) {
        if (kind == Kind::None)
            return;
        Entry& e = touch(record, handle, now);
        e.ownAt = now;
        e.touchAt = now;
    }
    // A sign that he has detected the hero (see the header): he counts as having detected him for
    // Tuning::detectSeconds from now.
    void notice(std::uint64_t record, std::uint32_t handle, Kind kind, double now) {
        if (kind == Kind::None)
            return;
        touch(record, handle, now).noticedAt = now;
    }
    // He has detected the hero: a sign of it was seen within the last Tuning::detectSeconds. An enemy the
    // book has no sign of - or has never met - has not.
    bool noticed(std::uint64_t record, std::uint32_t handle, double now) const {
        const Entry* e = find(record, handle);
        return e && now - e->noticedAt <= static_cast<double>(tuning_.detectSeconds);
    }
    // How long the book has followed him, seconds (0: it has not met him).
    double followedFor(std::uint64_t record, std::uint32_t handle, double now) const {
        const Entry* e = find(record, handle);
        return e ? now - e->metAt : 0.;
    }
    // Whether he is detected (noticed()) has changed since this was last asked of him (game_guard::watch asks
    // at every sample, for the log): Detected - a sign came after none; Lost - the last sign is more than
    // Tuning::detectSeconds old. An enemy the book has just met is unaware until a sign comes. One it has not
    // followed for a second or more (out of the watch's list, say) has no change to tell of: the answer is
    // only brought up to date.
    enum class Change : std::uint8_t { None, Detected, Lost };
    Change detection(std::uint64_t record, std::uint32_t handle, double now) {
        const Entry* known = find(record, handle);
        const bool away = known && now - known->seen > 1.;
        Entry& e = touch(record, handle, now);
        const bool aware = now - e.noticedAt <= static_cast<double>(tuning_.detectSeconds);
        const Change change = away || aware == e.aware ? Change::None
                              : aware                  ? Change::Detected
                                                       : Change::Lost;
        e.aware = aware;
        return change;
    }
    // How long his broken guard has left, seconds (0: not broken).
    double brokenFor(std::uint64_t record, std::uint32_t handle, double now) const {
        const Entry* e = find(record, handle);
        return e && e->brokenUntil > now ? e->brokenUntil - now : 0.;
    }
    // A blow at an enemy whose guard is of this kind.
    Outcome blow(std::uint64_t record, std::uint32_t handle, Kind kind, const Blow& b, double now) {
        if (kind == Kind::None)
            return {};
        Entry& e = touch(record, handle, now);
        e.ownAt = now; // the damage of this blow, if it lands, is not a stagger from elsewhere
        const bool up = now >= e.brokenUntil;
        if (!up || b.open) {
            e.streak = 0;
            return {};
        }
        ++e.blocks;
        if (b.touches)
            e.touchAt = now; // the flinch this block gives him (touch) is the mod's own
        // Blocked blows in a row (none more than counterWindow after the one before) make him answer. The
        // streak waits at the threshold while he cannot (the cooldown, or the hero is not on his feet) and
        // the first blocked blow after that sets the counter off.
        if (now - e.streakAt > static_cast<double>(tuning_.counterWindow))
            e.streak = 0;
        e.streakAt = now;
        if (e.streak < 1000)
            ++e.streak;
        Outcome out{Verdict::Blocked};
        out.streak = e.streak;
        if (tuning_.counterBlows > 0 && static_cast<float>(e.streak) >= tuning_.counterBlows &&
            b.mayCounter && now - e.counterAt >= static_cast<double>(tuning_.counterCooldown)) {
            e.counterAt = now;
            e.streak = 0;
            ++e.counters;
            out.retaliates = true;
        }
        return out;
    }
    // The guard opens for `seconds` (a dodge over him, say). True when it was standing.
    bool open(std::uint64_t record, std::uint32_t handle, Kind kind, double seconds, double now) {
        if (kind == Kind::None || !(seconds > 0))
            return false;
        Entry& e = touch(record, handle, now);
        const bool up = now >= e.brokenUntil;
        e.brokenUntil = (std::max)(e.brokenUntil, now + seconds);
        e.streak = 0;
        return up;
    }
    // His hit points now (read every moment he is in sight). A fall of staggerDamage or more that none of the
    // mod's own blows explains - a gadget, a thrown prop or enemy, the game's own punch - opens the guard for
    // staggerSeconds. True when it was standing and has just opened.
    bool hurt(std::uint64_t record, std::uint32_t handle, Kind kind, float hp, double now) {
        if (kind == Kind::None || !(hp == hp))
            return false;
        Entry& e = touch(record, handle, now);
        const float before = e.hp;
        e.hp = hp;
        // A fall of his hit points, whoever's, is a sign that he has detected the hero.
        if (before >= 0 && hp < before - .01f)
            e.noticedAt = now;
        if (before < 0 || before - hp < tuning_.staggerDamage || !(tuning_.staggerSeconds > 0) ||
            now - e.ownAt < static_cast<double>(tuning_.ownSeconds))
            return false;
        const bool up = now >= e.brokenUntil;
        e.brokenUntil = (std::max)(e.brokenUntil, now + static_cast<double>(tuning_.staggerSeconds));
        e.streak = 0;
        return up;
    }
    // True the first time this enemy is introduced (the census line is written once).
    bool introduce(std::uint64_t record, std::uint32_t handle, double now) {
        Entry& e = touch(record, handle, now);
        const bool first = !e.introduced;
        e.introduced = true;
        return first;
    }
    std::uint32_t blocks(std::uint64_t record, std::uint32_t handle) const {
        const Entry* e = find(record, handle);
        return e ? e->blocks : 0;
    }
    // The counters he has swung, and the blocked blows in a row he has now.
    std::uint32_t counters(std::uint64_t record, std::uint32_t handle) const {
        const Entry* e = find(record, handle);
        return e ? e->counters : 0;
    }
    std::uint32_t streak(std::uint64_t record, std::uint32_t handle) const {
        const Entry* e = find(record, handle);
        return e ? e->streak : 0;
    }
    // Forgets the enemies not seen for `age` seconds (and the oldest beyond a hundred).
    void prune(double now, double age = 180.) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&](const Entry& e) { return now - e.seen > age; }),
                       entries_.end());
        while (entries_.size() > 100) {
            auto oldest = std::min_element(entries_.begin(), entries_.end(),
                                           [](const Entry& a, const Entry& b) { return a.seen < b.seen; });
            entries_.erase(oldest);
        }
    }
    void clear() {
        entries_.clear();
    }
    size_t size() const {
        return entries_.size();
    }

  private:
    const Entry* find(std::uint64_t record, std::uint32_t handle) const {
        for (const auto& e : entries_)
            if (e.record == record && e.handle == handle)
                return &e;
        return nullptr;
    }
    Entry& touch(std::uint64_t record, std::uint32_t handle, double now) {
        for (auto& e : entries_)
            if (e.record == record) {
                if (e.handle != handle) {
                    e = Entry{record, handle};
                    e.metAt = now;
                }
                e.seen = now;
                return e;
            }
        Entry fresh;
        fresh.record = record;
        fresh.handle = handle;
        fresh.seen = now;
        fresh.metAt = now;
        entries_.push_back(fresh);
        return entries_.back();
    }
    Tuning tuning_;
    std::vector<Entry> entries_;
};
} // namespace spidy::guards
