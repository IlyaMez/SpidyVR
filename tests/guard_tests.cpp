// The guards (guard.hpp): which classes give a guard, the rules file, and each enemy's book - blocked blows,
// what opens a guard, the baton guard that breaks under blows, the time an opened guard stays down.
#include "spidy/guard.hpp"
#include "spidy/guard_pose.hpp"
#include "spidy/recoil.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace spidy::guards;
static int failures;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAILED line %d: %s\n", __LINE__, #c);                                               \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (0)

// (guard_pose.hpp): a man's rig in the game's model space (+x left, +y up, +z forward), with the hero's joint
// names, standing with his arms down.
namespace pose_rig {
struct Joint {
    const char* name;
    int parent;
    float x, y, z;
};
inline const std::vector<Joint>& joints() {
    static const std::vector<Joint> j = {{"a_body", -1, 0, 1.0f, 0},
                                         {"pelvis", 0, 0, 1.0f, 0},
                                         {"spine", 1, 0, 1.15f, 0},
                                         {"chest", 2, 0, 1.35f, 0},
                                         {"neck", 3, 0, 1.55f, 0},
                                         {"head", 4, 0, 1.62f, 0},
                                         {"LF_clavicle", 3, .06f, 1.45f, 0},
                                         {"LF_uparm", 6, .18f, 1.45f, 0},
                                         {"LF_loarm", 7, .18f, 1.15f, 0},
                                         {"LF_wrist", 8, .18f, .88f, 0},
                                         {"LF_finger_B_B", 9, .18f, .80f, 0},
                                         {"RT_clavicle", 3, -.06f, 1.45f, 0},
                                         {"RT_uparm", 11, -.18f, 1.45f, 0},
                                         {"RT_loarm", 12, -.18f, 1.15f, 0},
                                         {"RT_wrist", 13, -.18f, .88f, 0},
                                         {"RT_finger_B_B", 14, -.18f, .80f, 0},
                                         {"LF_upleg", 1, .1f, .95f, 0},
                                         {"LF_loleg", 16, .1f, .5f, 0},
                                         {"LF_foot", 17, .1f, .08f, 0},
                                         {"RT_upleg", 1, -.1f, .95f, 0},
                                         {"RT_loleg", 19, -.1f, .5f, 0},
                                         {"RT_foot", 20, -.1f, .08f, 0}};
    return j;
}
enum : int {
    pelvis = 1,
    chest = 3,
    head = 5,
    lUpper = 7,
    lLower = 8,
    lHand = 9,
    rUpper = 12,
    rLower = 13,
    rHand = 14,
    lFoot = 18
};
struct Built {
    std::vector<std::int16_t> parent;
    std::vector<std::uint32_t> hash;
    std::vector<float> rest;
};
// `mirror`: the left arm on the -x side (a rig that does not stand the way the hero's does).
inline Built build(bool mirror = false) {
    Built b;
    for (const auto& j : joints()) {
        b.parent.push_back(static_cast<std::int16_t>(j.parent));
        b.hash.push_back(spidy::guard_pose::nameHash(j.name));
        const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, mirror ? -j.x : j.x, j.y, j.z, 1};
        b.rest.insert(b.rest.end(), m, m + 16);
    }
    return b;
}
inline spidy::Vec3 at(const std::vector<float>& p, int joint) {
    return spidy::guard_pose::at(p.data(), joint);
}
} // namespace pose_rig
int main() {
    // The decorated names the game's type information gives.
    CHECK(plainName(".?AVBotMeleeWeaponManager@@") == "BotMeleeWeaponManager");
    CHECK(plainName(".?AUSomeStruct@@") == "SomeStruct");
    CHECK(plainName(".?AVOuter@Inner@@") == "Outer");
    CHECK(plainName("Plain") == "Plain");
    CHECK(plainName("") == "");

    // Built-in classes.
    Rules rules;
    CHECK(classOf("BotMeleeWeaponManager", rules).kind == Kind::Weapon);
    CHECK(classOf("BatonSwingAnim", rules).kind == Kind::Weapon);
    CHECK(classOf("ArmoredHealth", rules).kind == Kind::Brawler);
    CHECK(classOf("HeavyBrawlerBot", rules).kind == Kind::Brawler);
    CHECK(classOf("FiskBruteBot", rules).kind == Kind::Brawler);
    CHECK(classOf("BotShieldManager", rules).kind == Kind::Shield);
    CHECK(classOf("GauntletBrawlerBot", rules).kind == Kind::Brawler); // the gauntlet enemies
    CHECK(classOf("BotGauntletManager", rules).kind == Kind::Brawler);
    // The classes of the unarmed thugs and the gunners give no guard.
    for (const char* plain :
         {"ThugBot", "BotHealth", "BotBlockType", "BotTankManager", "BehaviorMeleeGame",
          "BotComboGraphComponent", "GameWeaponItem", "RangedCombatJobHost", "BehaviorRangedCombat",
          "BotWeaponManager", "WeaponAnimationManagerBot", "DamageModifierMeleeReaction", "BotHitCounter",
          "BotAttackResponseManager", "WAMAttackCollection", "LoadingTipDamagerType"})
        CHECK(classOf(plain, rules).kind == Kind::None);
    // the states that open a guard: stunned, webbed, knocked about (BotState classes only).
    for (const char* open :
         {"BotStateStunned", "BotStateElectricStun", "BotStateWebStruggle", "BotStateRagdoll",
          "BotStateKnockdown", "BotStateStaggerBig", "BotStateGetUp", "BotStateWebbedToWall"})
        CHECK(opensGuard(open));
    for (const char* shut : {"BotStateIdle", "BotStateCombat", "BotStateMeleeAttack", "BotStateBlock",
                             "ThugBot", "StunGunManager", "BotState", "", "WebStruggle"})
        CHECK(!opensGuard(shut));
    // The thugs' own slam attacks are no stagger; the hit reaction (the state the mod's touch used to put him
    // in) and the game's staged block are no open state either.
    for (const char* attack : {"BotStateShockwaveAttack", "BotStateShockJump", "BotStateHitReactGame",
                               "BotStateSyncedHitReact", "BotStateStagedBlock", "BotStateMeleeGame"})
        CHECK(!opensGuard(attack));
    for (const char* open : {"BotStateKnockUp", "BotStateRagdollGame", "BotStateStunnedLocal"})
        CHECK(opensGuard(open));
    // The Slayer's behavior (SlayerBotBehaviorMelee: the heavy with the ground pound) gives a brute's guard.
    CHECK(classOf("SlayerBotBehaviorMelee", rules).kind == Kind::Brawler);
    CHECK(classOf("SlayerBot", rules).kind == Kind::Brawler);
    // The swordsman (SwordsmanBot and every class with Swordsman in its name) has a baton's guard - the
    // Weapon kind - and is marked: his block has the sword's sound.
    for (const char* sword : {"SwordsmanBot", "DamageModifierSwordsmanDodge", "MrNegativeSwordsman"}) {
        CHECK(classOf(sword, rules).swordsman);
        CHECK(classOf(sword, rules).kind == Kind::Weapon);
    }
    for (const char* plain : {"BotMeleeWeaponManager", "BotWeaponManager", "BatonSwingAnim", "ArmoredHealth",
                              "ThugBot", "SlayerBot", "ShieldWithHealth"})
        CHECK(!classOf(plain, rules).swordsman);
    // A human shield (a thug who holds a hostage) is no shield's guard.
    CHECK(classOf("DamageModifierHostageShield", rules).exempt &&
          !classOf("DamageModifierDroneShield", rules).exempt);
    // Which block sound a guard makes: a baton's for the ordinary melee guards, a sword's for the swordsmen
    // (whatever else they are), a slayer's for the brutes, a shield's for the shields.
    CHECK(soundOf(Kind::Weapon, false) == Sound::Baton);
    CHECK(soundOf(Kind::Weapon, true) == Sound::Sword);
    CHECK(soundOf(Kind::Brawler, false) == Sound::Slayer);
    CHECK(soundOf(Kind::Brawler, true) == Sound::Sword);
    CHECK(soundOf(Kind::Shield, false) == Sound::Shield && soundOf(Kind::Shield, true) == Sound::Shield);
    CHECK(soundOf(Kind::None, false) == Sound::Baton);
    CHECK(std::string(soundName(Sound::Baton)) == "baton" &&
          std::string(soundName(Sound::Sword)) == "sword" &&
          std::string(soundName(Sound::Slayer)) == "slayer" &&
          std::string(soundName(Sound::Shield)) == "shield");
    // The headset plays ImpactSound::BlockBaton (a blocked blow) or WebBlockBaton (a refused web pull) plus
    // the number of his Sound, so the numbers are the order of the four files: baton, sword, slayer, shield
    // (game_xr.cpp asserts it too).
    CHECK(static_cast<int>(Sound::Baton) == 0 && static_cast<int>(Sound::Sword) == 1 &&
          static_cast<int>(Sound::Slayer) == 2 && static_cast<int>(Sound::Shield) == 3);
    // The states that have him webbed or caught in a web - the ones the game names - and nothing else: a
    // webbed enemy never guards.
    for (const char* webbed :
         {"BotStateWebStruggle", "BotStateWebAttached", "BotStateWebYank", "BotStateWebYankAndThrow",
          "BotStateWebYankMarker", "BotStateWebRushCatch", "BotStateWebStrike",
          "BotStatePlaySyncedAnimHostWithWebs", "BotStateWebbedToWall"})
        CHECK(webbedState(webbed));
    for (const char* notWebbed :
         {"BotStateStunned", "BotStateElectricStun", "BotStateRagdoll", "BotStateWallStuck",
          "BotStateMeleeGame", "BotStateHitReactGame", "BotStateBlock", "BotState", "", "WebStruggle",
          "WebAttached", "ThugBot", "StatusEffectTrackerWebbed"})
        CHECK(!webbedState(notWebbed));
    // The stronger kind wins.
    CHECK(stronger(Kind::Weapon, Kind::Shield) == Kind::Shield);
    CHECK(stronger(Kind::Brawler, Kind::Weapon) == Kind::Brawler);
    CHECK(stronger(Kind::None, Kind::None) == Kind::None);

    // The rules file.
    Rules file;
    const int unknown = parseRules("# the guard rules\r\n"
                                   "  weapon  TeslaRod   # a comment\r\n"
                                   "BRUTE Bruiser\n"
                                   "shield RiotPlate\n"
                                   "exempt  TrainingDummy\n"
                                   "hp 250\n"
                                   "\n"
                                   "dance wildly\n"
                                   "hp banana\n"
                                   "stagger -1\n"
                                   "off\n",
                                   file);
    CHECK(unknown == 3);
    CHECK(!file.enabled);
    CHECK(file.weapon.size() == 1 && file.weapon[0] == "TeslaRod");
    CHECK(file.brawler.size() == 1 && file.brawler[0] == "Bruiser");
    CHECK(file.shield.size() == 1 && file.shield[0] == "RiotPlate");
    CHECK(file.exempt.size() == 1 && file.exempt[0] == "TrainingDummy");
    CHECK(file.hp == 250.f);
    CHECK(classOf("TeslaRodManager", file).kind == Kind::Weapon);
    CHECK(classOf("BigBruiserBot", file).kind == Kind::Brawler);
    CHECK(classOf("RiotPlateShield", file).kind == Kind::Shield);
    CHECK(classOf("TrainingDummyShield", file).exempt);
    CHECK(classOf("ArmoredHealth", file).kind == Kind::Brawler); // the built-in ones stay
    // `swordsman NAME` marks a swordsman (a baton's guard with the sword's sound; an empty one is not
    // understood). The older names of the other lines (weapon, brawler) are understood too.
    Rules marked;
    CHECK(parseRules("swordsman Blademan2\nSWORDSMAN Spoiler\nswordsman\n", marked) == 1);
    CHECK(marked.swordsman.size() == 2 && marked.swordsman[0] == "Blademan2" &&
          marked.swordsman[1] == "Spoiler");
    CHECK(classOf("MyBlademan2Bot", marked).swordsman &&
          classOf("MyBlademan2Bot", marked).kind == Kind::Weapon);
    CHECK(classOf("SwordsmanBot", marked).swordsman); // the built-in ones stay
    Rules names;
    CHECK(parseRules("baton Stick\nbrute Hulk\nweapon Club\nbrawler Bruiser\nshield Wall\n", names) == 0);
    CHECK(names.weapon.size() == 2 && names.weapon[0] == "Stick" && names.weapon[1] == "Club");
    CHECK(names.brawler.size() == 2 && names.brawler[0] == "Hulk" && names.brawler[1] == "Bruiser" &&
          names.shield.size() == 1);
    // A line that is none of the rules is not understood, and is counted.
    CHECK(parseRules("venom off\nrecord\nbreaker Thing\n", names) == 3);
    Rules again;
    CHECK(parseRules("on\n", again) == 0 && again.enabled);
    CHECK(parseRules("", again) == 0);
    CHECK(parseRules("off extra\n", again) == 1 && again.enabled); // "off" takes no word
    CHECK(parseRules("hp 0\n", again) == 0 && again.hp == 0.f);
    // The hit points from which an enemy guards as a brute are off until the census says which number tells
    // the heavy ones.
    CHECK(Rules{}.hp == 0.f);
    // The openers' seconds.
    CHECK(Rules{}.tuning.staggerSeconds == 4.f && Rules{}.tuning.dodgeSeconds == 3.f);
    CHECK(parseRules("stagger 6\ndodge 2.5\n", again) == 0 && again.tuning.staggerSeconds == 6.f &&
          again.tuning.dodgeSeconds == 2.5f);
    CHECK(parseRules("stagger 0\ndodge 0\n", again) == 0 && again.tuning.staggerSeconds == 0.f &&
          again.tuning.dodgeSeconds == 0.f);
    CHECK(parseRules("stagger -1\ndodge 999\nstagger soon\n", again) == 3);
    // No blows break a guard, so there is no line for it (a guard.txt with `blows` or `seconds` in it is
    // reported, harmless).
    CHECK(parseRules("blows 5\nseconds 6\n", again) == 2);
    // The reaction of a blocked blow (the game's hit reaction): off unless the file says on.
    CHECK(!Rules{}.touch && !again.touch);
    CHECK(parseRules("touch on\n", again) == 0 && again.touch);
    CHECK(parseRules("Touch OFF\n", again) == 0 && !again.touch);
    CHECK(parseRules("TOUCH On\n", again) == 0 && again.touch);
    CHECK(parseRules("touch\n", again) == 1 && again.touch); // a bad line leaves it as it was
    CHECK(parseRules("touch maybe\n", again) == 1 && again.touch);
    CHECK(parseRules("touch off\n", again) == 0 && !again.touch);
    // how long a webbed enemy stays open after the last moment he was webbed (0: only while his state says
    // so).
    CHECK(Rules{}.tuning.webbedSeconds == 4.f);
    {
        Rules r;
        CHECK(parseRules("webbed 7\n", r) == 0 && r.tuning.webbedSeconds == 7.f);
        CHECK(parseRules("Webbed 0\n", r) == 0 && r.tuning.webbedSeconds == 0.f);
        CHECK(parseRules("WEBBED 2.5\n", r) == 0 && r.tuning.webbedSeconds == 2.5f);
        CHECK(parseRules("webbed -1\nwebbed 999\nwebbed soon\nwebbed\n", r) == 4 &&
              r.tuning.webbedSeconds == 2.5f);
    }
    // the recoil of a blocked blow, in centimetres (0: none).
    CHECK(Rules{}.recoil == 3.f);
    {
        Rules r;
        CHECK(parseRules("recoil 6\n", r) == 0 && r.recoil == 6.f);
        CHECK(parseRules("Recoil 0\n", r) == 0 && r.recoil == 0.f);
        CHECK(parseRules("recoil 1.5\n", r) == 0 && r.recoil == 1.5f);
        CHECK(parseRules("recoil -1\nrecoil 99\nrecoil lots\nrecoil\n", r) == 4 && r.recoil == 1.5f);
    }

    // A brute: every plain blow is blocked, as many as there are; a guard that is opened (a gadget, a dodge
    // ...) is down for its time, and the guard comes back when the time is out.
    {
        Book book;
        const std::uint64_t record = 0x1000;
        const std::uint32_t handle = 0x00500123;
        double now = 100;
        CHECK(book.standing(record, handle, Kind::Brawler, now));
        for (int i = 0; i < 40; ++i) {
            now += .2;
            const auto o = book.blow(record, handle, Kind::Brawler, {.strength = 1.f}, now);
            CHECK(o.verdict == Verdict::Blocked);
        }
        CHECK(book.blocks(record, handle) == 40);
        CHECK(book.standing(record, handle, Kind::Brawler, now));
        now += .2;
        CHECK(book.open(record, handle, Kind::Brawler, 6., now));
        CHECK(!book.standing(record, handle, Kind::Brawler, now + .1));
        CHECK(book.brokenFor(record, handle, now + 1) > 4.9 && book.brokenFor(record, handle, now + 1) < 5.1);
        // While it is down every blow lands.
        for (int i = 0; i < 5; ++i) {
            const auto o = book.blow(record, handle, Kind::Brawler, {.strength = .2f}, now + 1 + i);
            CHECK(o.verdict == Verdict::Lands);
        }
        // A second opener on an open guard extends the time.
        CHECK(!book.open(record, handle, Kind::Brawler, 6., now + 4));
        CHECK(!book.standing(record, handle, Kind::Brawler, now + 9));
        CHECK(book.standing(record, handle, Kind::Brawler, now + 10.1));
        CHECK(book.blow(record, handle, Kind::Brawler, {.strength = 1.f}, now + 10.2).verdict ==
              Verdict::Blocked);
    }
    // A shield behaves as a brute.
    {
        Book book;
        for (int i = 0; i < 30; ++i)
            CHECK(book.blow(7, 1, Kind::Shield, {.strength = 1.f}, i * .3).verdict == Verdict::Blocked);
        CHECK(book.open(7, 1, Kind::Shield, 6., 10));
        CHECK(book.blow(7, 1, Kind::Shield, {.strength = 1.f}, 11).verdict == Verdict::Lands);
    }
    // No number of blows breaks a baton's guard, however hard: sixty of them in a row, all blocked. An opener
    // opens it at once, even before the first blow.
    {
        Book plain;
        for (int i = 0; i < 60; ++i)
            CHECK(plain.blow(6, 6, Kind::Weapon, {.strength = 1.f}, i * .3).verdict == Verdict::Blocked);
        CHECK(plain.blocks(6, 6) == 60 && plain.standing(6, 6, Kind::Weapon, 20));
        Book opened;
        CHECK(opened.open(5, 5, Kind::Weapon, 6., 1));
        CHECK(!opened.standing(5, 5, Kind::Weapon, 2));
    }
    // something other than the hero's fists hurts him (a gadget, a thrown prop or enemy): the guard opens for
    // staggerSeconds. A fall after one of the mod's own blows is that blow's, and a small fall is nothing.
    {
        Book book;
        const std::uint64_t record = 0x2000;
        const std::uint32_t handle = 7;
        CHECK(!book.hurt(record, handle, Kind::Brawler, 110.f, 10)); // the first reading is the baseline
        CHECK(book.standing(record, handle, Kind::Brawler, 10.1));
        CHECK(!book.hurt(record, handle, Kind::Brawler, 108.f, 10.2)); // 2 hit points: below staggerDamage
        CHECK(book.standing(record, handle, Kind::Brawler, 10.3));
        CHECK(book.hurt(record, handle, Kind::Brawler, 90.f, 11)); // a thrown prop
        CHECK(!book.standing(record, handle, Kind::Brawler, 12));
        CHECK(!book.standing(record, handle, Kind::Brawler, 14.9));
        CHECK(book.standing(record, handle, Kind::Brawler, 15.1)); // 4 s
        // While it is open every blow lands, and the damage of the blow that follows is the blow's own.
        CHECK(book.blow(record, handle, Kind::Brawler, {.strength = 1.f}, 12).verdict == Verdict::Lands);
        CHECK(!book.hurt(record, handle, Kind::Brawler, 80.f, 12.1));
        CHECK(book.standing(record, handle, Kind::Brawler, 15.1)); // not extended by it
        // A blocked blow's flinch counts as the mod's too.
        CHECK(book.blow(record, handle, Kind::Brawler, {.strength = 1.f}, 16).verdict == Verdict::Blocked);
        CHECK(!book.hurt(record, handle, Kind::Brawler, 70.f, 16.2));
        CHECK(book.standing(record, handle, Kind::Brawler, 16.3));
        // Later, a fall from elsewhere opens it again; one while it is open extends it and reports no new
        // opening.
        CHECK(book.hurt(record, handle, Kind::Brawler, 55.f, 17));
        CHECK(!book.hurt(record, handle, Kind::Brawler, 40.f, 18));
        CHECK(!book.standing(record, handle, Kind::Brawler, 21.9));
        CHECK(book.standing(record, handle, Kind::Brawler, 22.1));
        // Healing moves the baseline up; the next fall is measured from there.
        CHECK(!book.hurt(record, handle, Kind::Brawler, 100.f, 30));
        CHECK(!book.hurt(record, handle, Kind::Brawler, 99.f, 31));
        CHECK(book.hurt(record, handle, Kind::Brawler, 90.f, 32));
        // Another enemy in the same record starts from nothing.
        CHECK(!book.hurt(record, handle + 1, Kind::Brawler, 50.f, 40));
        CHECK(book.standing(record, handle + 1, Kind::Brawler, 40.1));
        // No guard, nothing to open; a reading that is not a number is ignored.
        CHECK(!book.hurt(9, 9, Kind::None, 10.f, 1) && book.size() == 1);
        CHECK(!book.hurt(record, handle + 1, Kind::Brawler, std::nanf(""), 41));
        // staggerSeconds 0 turns it off.
        Tuning off;
        off.staggerSeconds = 0;
        Book quiet(off);
        quiet.hurt(1, 1, Kind::Shield, 100.f, 1);
        CHECK(!quiet.hurt(1, 1, Kind::Shield, 10.f, 2) && quiet.standing(1, 1, Kind::Shield, 3));
    }
    // how long ago the mod's own blow reached him.
    {
        Book book;
        CHECK(book.sinceOwn(1, 1, 5) > 1e8);
        book.blow(1, 1, Kind::Brawler, {.strength = 1.f}, 10);
        CHECK(book.sinceOwn(1, 1, 10.5) > .49 && book.sinceOwn(1, 1, 10.5) < .51);
        CHECK(book.sinceOwn(1, 2, 10.5) > 1e8); // another enemy in the record
    }
    // the grace for a state of the mod's own making (a blocked blow's flinch, a counter's break) is given by
    // blocked blows and own() alone; a blow that landed leaves it where it was, so punches in a chain into an
    // enemy who is staggered by something else are not blocked after the first.
    {
        Book book;
        CHECK(book.sinceTouched(1, 1, 5) > 1e8);
        book.blow(1, 1, Kind::Brawler, {.open = true, .strength = 1.f}, 10); // landed through an open guard
        CHECK(book.sinceOwn(1, 1, 10.5) < .51 && book.sinceTouched(1, 1, 10.5) > 1e8);
        const auto blocked = book.blow(1, 1, Kind::Brawler, {.strength = 1.f}, 20);
        CHECK(blocked.verdict == Verdict::Blocked);
        CHECK(book.sinceTouched(1, 1, 20.5) > .49 && book.sinceTouched(1, 1, 20.5) < .51);
        book.blow(1, 1, Kind::Brawler, {.open = true}, 21); // landed: the grace still stands from 20
        CHECK(book.sinceTouched(1, 1, 21.5) > 1.49 && book.sinceTouched(1, 1, 21.5) < 1.51);
        CHECK(book.sinceTouched(1, 2, 22) > 1e8); // another enemy in the record
        // An opened guard flinches nobody of the mod's making.
        book.open(5, 5, Kind::Weapon, 6., 25);
        CHECK(book.sinceTouched(5, 5, 26) > 1e8);
        // with the touch off the block gives him no reaction of the mod's, so no grace either: whatever state
        // he is in after it is the game's own. The block itself counts all the same.
        {
            Blow quiet{.strength = 1.f};
            quiet.touches = false;
            const auto out = book.blow(6, 6, Kind::Brawler, quiet, 27);
            CHECK(out.verdict == Verdict::Blocked && out.streak == 1 && book.blocks(6, 6) == 1);
            CHECK(book.sinceTouched(6, 6, 27.5) > 1e8 && book.sinceOwn(6, 6, 27.5) < .51);
            const auto again2 = book.blow(6, 6, Kind::Brawler, {.strength = 1.f}, 28); // touches by default
            CHECK(again2.verdict == Verdict::Blocked && again2.streak == 2);
            CHECK(book.sinceTouched(6, 6, 28.5) > .49 && book.sinceTouched(6, 6, 28.5) < .51);
        }
        // own(): the mod itself has just staggered him (game_punch's break of his attack).
        book.own(2, 2, Kind::Weapon, 30);
        CHECK(book.sinceTouched(2, 2, 30.25) > .24 && book.sinceTouched(2, 2, 30.25) < .26);
        CHECK(book.sinceOwn(2, 2, 30.25) > .24 && book.sinceOwn(2, 2, 30.25) < .26);
        CHECK(book.standing(2, 2, Kind::Weapon, 30.1) && book.streak(2, 2) == 0 && book.blocks(2, 2) == 0);
        const size_t before = book.size();
        book.own(3, 3, Kind::None, 30); // no guard, nothing to note
        CHECK(book.size() == before && book.sinceTouched(3, 3, 31) > 1e8);
        // What he loses in hit points just after it is the mod's, not a stagger from elsewhere.
        book.hurt(4, 4, Kind::Brawler, 100.f, 40);
        book.own(4, 4, Kind::Brawler, 41);
        CHECK(!book.hurt(4, 4, Kind::Brawler, 90.f, 41.3) && book.standing(4, 4, Kind::Brawler, 41.4));
    }
    // The book remembers when an enemy was last seen webbed, so a state between his web states (the guess:
    // the hit reaction) does not hand the next punch a guard.
    {
        Book book;
        CHECK(book.sinceWebbed(1, 1, 5) > 1e8);
        CHECK(book.webbed(1, 1, Kind::Weapon, 10)); // a new web
        CHECK(book.sinceWebbed(1, 1, 10.5) > .49 && book.sinceWebbed(1, 1, 10.5) < .51);
        CHECK(!book.webbed(1, 1, Kind::Weapon, 10.5)); // the same one, seen again (within the gap)
        CHECK(book.sinceWebbed(1, 1, 10.75) > .24 && book.sinceWebbed(1, 1, 10.75) < .26); // and refreshed
        CHECK(book.sinceWebbed(1, 2, 11) > 1e8);         // another enemy in the record
        CHECK(book.webbed(1, 1, Kind::Weapon, 20));      // a new web, long after
        CHECK(!book.webbed(1, 1, Kind::Weapon, 21, 2.)); // ... a wider gap counts it as the same
        CHECK(book.webbed(1, 1, Kind::Weapon, 25, 2.));
        // A fresh enemy in the same record (a new handle) starts with no web.
        book.webbed(2, 2, Kind::Brawler, 30);
        CHECK(book.sinceWebbed(2, 3, 30.1) > 1e8 && book.sinceWebbed(2, 2, 30.1) < .11);
        book.blow(2, 3, Kind::Brawler, {.strength = 1.f}, 31);
        CHECK(book.sinceWebbed(2, 3, 31.1) > 1e8);
        // No guard, nothing to note.
        const size_t before = book.size();
        CHECK(!book.webbed(9, 9, Kind::None, 40));
        CHECK(book.size() == before && book.sinceWebbed(9, 9, 41) > 1e8);
        // Being webbed ends his block streak (a counter that was building does not wait for him).
        Book streaky;
        streaky.blow(4, 4, Kind::Weapon, {.strength = 1.f}, 50);
        streaky.blow(4, 4, Kind::Weapon, {.strength = 1.f}, 50.3);
        CHECK(streaky.streak(4, 4) == 2);
        streaky.webbed(4, 4, Kind::Weapon, 50.5);
        CHECK(streaky.streak(4, 4) == 0);
        // The book's tuning carries the seconds (game_guard reads them from it).
        Tuning tuning;
        tuning.webbedSeconds = 6.f;
        Book tuned(tuning);
        CHECK(tuned.tuning().webbedSeconds == 6.f);
    }
    // A web ball opens nothing (a ball is 11 of the 30 a thug takes before he is webbed): it is the mod's own
    // damage and nothing else (game_guard::ballHit is Book::own): the guard stands, the fall of hit points it
    // causes is no stagger, and only a webbed state opens it.
    {
        Book book;
        const std::uint64_t rec = 0x3000;
        const std::uint32_t h = 0x00800001;
        double now = 10;
        CHECK(!book.hurt(rec, h, Kind::Weapon, 140.f, now)); // his hit points, first seen
        // The ball hits him as he runs at the hero; his hit points fall 8 a moment later.
        book.own(rec, h, Kind::Weapon, now + 1.);
        CHECK(!book.hurt(rec, h, Kind::Weapon, 132.f, now + 1.1));
        CHECK(book.standing(rec, h, Kind::Weapon, now + 1.2));
        // The punch that follows is blocked, and so is the next, a moment after the second ball.
        CHECK(book.blow(rec, h, Kind::Weapon, {.strength = .5f}, now + 1.3).verdict == Verdict::Blocked);
        book.own(rec, h, Kind::Weapon, now + 1.8);
        CHECK(!book.hurt(rec, h, Kind::Weapon, 124.f, now + 1.9));
        CHECK(book.blow(rec, h, Kind::Weapon, {.strength = .5f}, now + 2.).verdict == Verdict::Blocked);
        // A fall of the same size with nothing of the mod's own near it is a stagger from elsewhere (a
        // gadget, a thrown prop).
        CHECK(book.hurt(rec, h, Kind::Weapon, 112.f, now + 6.) &&
              !book.standing(rec, h, Kind::Weapon, now + 8.));
        CHECK(book.standing(rec, h, Kind::Weapon, now + 10.1));
        // Seen in a webbed state, he is webbed for the seconds that follow (the glue holds it, and no longer
        // while he runs).
        CHECK(book.webbed(rec, h, Kind::Weapon, now + 20.) && book.sinceWebbed(rec, h, now + 23.) > 2.9 &&
              book.sinceWebbed(rec, h, now + 23.) < 3.1);
    }
    // An enemy running at the hero guards for as long as he runs, whatever he is (guard.txt `runners on`).
    {
        // The states of a runner: the chase, go-to and go-direction ones and the plain dash, by the state
        // class's own name.
        for (const char* runs :
             {"BotStateChaseRunTo", "BotStateChaseJumpTo", "BotStateChaseJumpToLocal", "BotStateChaseWallRun",
              "BotStateChaseSlideTo", "BotStateChaseSpecialGoTo", "BotStateChaseVaultOver",
              "BotStateChaseHookshotTo", "BotStateGoto", "BotStateGotoGame", "BotStateGotoLocal",
              "BotStateGotoRemote", "BotStateGoDirection", "BotStateGoDirectionGame",
              "BotStateGoDirectionLocalGame", "BotStateGoDirectionRemote", "BotStateDash"}) {
            CHECK(runningState(runs));
            // A running state is no state that opens a guard and none that has him webbed: a runner's guard
            // stands in it.
            CHECK(!opensGuard(runs) && !webbedState(runs));
        }
        // Not the attacks that run at him, not standing, circling or reeling, not a state of another's
        // making, and not a name without the BotState in front of it.
        for (const char* stays : {"BotStateDashAttack",
                                  "BotStateDashAttackLocal",
                                  "BotStateDashAttackRemote",
                                  "BotStateMeleeGame",
                                  "BotStateAOEAttack",
                                  "BotStateChargeAttackGame",
                                  "BotStateLungeGame",
                                  "BotStatePounceLocal",
                                  "BotStateHitReactGame",
                                  "BotStateStunned",
                                  "BotStateStrafeOnCurve",
                                  "BotStateShuffleGame",
                                  "BotStateFace",
                                  "BotStatePlayAnim",
                                  "BotStateJump",
                                  "BotStateVault",
                                  "BotStateWallCrawl",
                                  "BotStateFlung",
                                  "BotStateRagdoll",
                                  "BotStateWebStruggle",
                                  "BotStateTinkererChaseJumpAndAttack",
                                  "BotStateAim",
                                  "BotState",
                                  "Chase",
                                  "ChaseRunTo",
                                  "Goto",
                                  "Dash",
                                  "ThugBot",
                                  ""})
            CHECK(!runningState(stays));
        // The kind: weaker than every guard of his own, the numbers of the others as the report has them, his
        // sound a baton's (a sword's for a swordsman).
        CHECK(std::string(kindName(Kind::Runner)) == "runner");
        CHECK(soundOf(Kind::Runner, false) == Sound::Baton && soundOf(Kind::Runner, true) == Sound::Sword);
        CHECK(stronger(Kind::Runner, Kind::None) == Kind::Runner &&
              stronger(Kind::None, Kind::Runner) == Kind::Runner);
        CHECK(stronger(Kind::Weapon, Kind::Runner) == Kind::Weapon &&
              stronger(Kind::Runner, Kind::Weapon) == Kind::Weapon);
        CHECK(stronger(Kind::Brawler, Kind::Runner) == Kind::Brawler &&
              stronger(Kind::Runner, Kind::Shield) == Kind::Shield);
        CHECK(static_cast<int>(Kind::None) == 0 && static_cast<int>(Kind::Weapon) == 1 &&
              static_cast<int>(Kind::Brawler) == 2 && static_cast<int>(Kind::Shield) == 3);
        // No class gives a runner's guard: it is every enemy's, by his run.
        CHECK(classOf("BotStateChaseRunTo", Rules{}).kind == Kind::None &&
              classOf("ThugBot", Rules{}).kind == Kind::None);
    }
    {
        // guard.txt: `runners on | off` and `runspeed N`. Off until the file says on.
        CHECK(!Rules{}.runners && Rules{}.tuning.runSpeed == 2.f && Rules{}.tuning.runGrace == .35f);
        Rules r;
        CHECK(parseRules("runners on\n", r) == 0 && r.runners);
        CHECK(parseRules("runners off\n", r) == 0 && !r.runners);
        CHECK(parseRules("RUNNERS On\n", r) == 0 && r.runners);
        CHECK(parseRules("runners\nrunners maybe\n", r) == 2 && r.runners); // bad lines leave it as it was
        CHECK(parseRules("Runners OFF\n", r) == 0 && !r.runners);
        CHECK(parseRules("runspeed 3.5\n", r) == 0 && r.tuning.runSpeed == 3.5f);
        CHECK(parseRules("RunSpeed 0.5\nrunspeed 12\n", r) == 0 && r.tuning.runSpeed == 12.f);
        CHECK(parseRules("runspeed 0.2\nrunspeed 13\nrunspeed fast\nrunspeed\n", r) == 4 &&
              r.tuning.runSpeed == 12.f);
        // The other lines go on being read around them.
        Rules both;
        CHECK(parseRules("runners off\nrunspeed 4\nweapon Baton2\n", both) == 0 && !both.runners &&
              both.tuning.runSpeed == 4.f && both.weapon.size() == 1);
    }
    // How fast an enemy comes at the hero, from his own movement (Book::running): the hero at the origin, he
    // runs at him along the x axis, a sample every twentieth of a second.
    {
        Book book;
        const std::uint64_t rec = 0x2000;
        const std::uint32_t h = 0x00700001;
        double t = 50, x = 10;
        std::vector<bool> got;
        for (int i = 0; i < 12; ++i) {
            got.push_back(book.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t));
            t += .05;
            x -= .25; // 5 metres a second at him
        }
        CHECK(!got[0] && !got[1] &&
              !got[2]); // the first sample has no speed, and the speed builds up (a quarter of a second)
        CHECK(got[3] && got[11]); // a fifth of a second of running and he is a runner
        CHECK(book.closing(rec, h) > 4.2 && book.closing(rec, h) < 5.1);
        // He counts for runGrace seconds (0.35) after the last sample that saw him run (at t - 0.05), not for
        // longer.
        CHECK(book.runner(rec, h, t) && book.runner(rec, h, t + .25));
        CHECK(!book.runner(rec, h, t + .5) && !book.runner(rec, h, t + 5));
        // Another enemy of the same record (a new handle) starts with nothing, and so does one never seen.
        CHECK(!book.runner(rec, h + 1, t) && !book.runner(rec + 1, h, t) && book.closing(rec + 1, h) == 0.);
        // A different grace.
        Tuning slow;
        slow.runGrace = 1.f;
        Book patient(slow);
        double u = 10, y = 10;
        for (int i = 0; i < 8; ++i, u += .05, y -= .25)
            patient.running(rec, h, Kind::Runner, y, 0, 0, 0, true, u);
        CHECK(patient.runner(rec, h, u + .8) && !patient.runner(rec, h, u + 1.1));
    }
    {
        // A walk is no run (1.2 metres a second at him), running away is none, and neither is circling him at
        // 5 metres a second - the hero's own movement is no part of it either: he stands and the hero walks
        // at him.
        const std::uint64_t rec = 0x2001;
        const std::uint32_t h = 0x00700002;
        Book walk, away, circle, still;
        double t = 20, x = 10, xa = 10, hx = 0;
        for (int i = 0; i < 40; ++i, t += .05, x -= .06, xa += .25, hx += .3) {
            CHECK(!walk.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t));
            CHECK(!away.running(rec, h, Kind::Runner, xa, 0, 0, 0, true, t));
            const double angle = i * .025; // 10 metres out, 5 metres a second round the hero
            CHECK(!circle.running(rec, h, Kind::Runner, 10 * std::cos(angle), 10 * std::sin(angle), 0, 0,
                                  true, t));
            CHECK(!still.running(rec, h, Kind::Runner, 30, 0, hx, 0, true,
                                 t)); // he stands, the hero comes at 6 m/s
        }
        CHECK(walk.closing(rec, h) > .9 && walk.closing(rec, h) < 1.3);
        CHECK(away.closing(rec, h) < -4.);
        CHECK(std::fabs(circle.closing(rec, h)) < .3);
        CHECK(std::fabs(still.closing(rec, h)) < .01);
        CHECK(!walk.runner(rec, h, t) && !away.runner(rec, h, t) && !circle.runner(rec, h, t) &&
              !still.runner(rec, h, t));
    }
    {
        // His state is the other gate: fast but not in a running state (an attack, standing), he is no runner
        // - and the moment it is a running one he is, with the speed already measured.
        Book book;
        const std::uint64_t rec = 0x2002;
        const std::uint32_t h = 0x00700003;
        double t = 5, x = 12;
        for (int i = 0; i < 12; ++i, t += .05, x -= .25)
            CHECK(!book.running(rec, h, Kind::Runner, x, 0, 0, 0, false, t));
        CHECK(book.closing(rec, h) > 4. && !book.runner(rec, h, t));
        // The next sample, his state a running one: a runner at once - the speed was measured all along.
        CHECK(book.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t) && book.runner(rec, h, t));
        // Asked again within a fiftieth of a second there is no new sample and no new speed: the answer is
        // the state's.
        const double speed = book.closing(rec, h);
        CHECK(book.running(rec, h, Kind::Runner, x - 1., 0, 0, 0, true, t + .01) &&
              book.closing(rec, h) == speed);
        CHECK(!book.running(rec, h, Kind::Runner, x - 1., 0, 0, 0, false, t + .01) &&
              book.closing(rec, h) == speed);
        // And back out of it: the next sample says he is not (an attack began), and the grace runs out.
        t += .05;
        x -= .25;
        CHECK(!book.running(rec, h, Kind::Runner, x, 0, 0, 0, false, t));
        CHECK(!book.runner(rec, h, t + .5));
    }
    {
        // A gap between samples (more than 0.6 s) or a jump (he was put somewhere: more than 30 m/s) starts
        // the measure over.
        Book book;
        const std::uint64_t rec = 0x2003;
        const std::uint32_t h = 0x00700004;
        double t = 1, x = 20;
        for (int i = 0; i < 12; ++i, t += .05, x -= .25)
            book.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t);
        CHECK(book.closing(rec, h) > 4.);
        t += 1.;
        x -= .25;
        CHECK(!book.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t) && book.closing(rec, h) == 0.);
        for (int i = 0; i < 12; ++i, t += .05, x -= .25)
            book.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t);
        CHECK(book.closing(rec, h) > 4.);
        t += .05;
        x -= 6.; // a jump of six metres in a twentieth of a second
        CHECK(!book.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t) && book.closing(rec, h) == 0.);
        // The clock going back is a gap too.
        t -= 100;
        CHECK(!book.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t) && book.closing(rec, h) == 0.);
        // Standing on the hero: no direction, no speed, and no harm.
        CHECK(!book.running(rec, h, Kind::Runner, 0, 0, 0, 0, true, t + .05));
        // An enemy with no kind of guard is not followed at all.
        const size_t before = book.size();
        CHECK(!book.running(77, 77, Kind::None, 5, 0, 0, 0, true, t) && book.size() == before);
    }
    {
        // The game's time scale: in slow motion he covers a fifth of the ground in a second and is as much a
        // runner.
        const std::uint64_t rec = 0x2004;
        const std::uint32_t h = 0x00700005;
        Book slowed, plain, odd;
        double t = 1, x = 10;
        for (int i = 0; i < 12; ++i, t += .05, x -= .05) { // one metre a second of the real time
            slowed.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t, .2);
            plain.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t);
            odd.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t, 0.);
            odd.running(rec, h + 9, Kind::Runner, x, 0, 0, 0, true, t, std::nan(""));
        }
        CHECK(slowed.runner(rec, h, t) && !plain.runner(rec, h, t));
        CHECK(slowed.closing(rec, h) > 3.5 && slowed.closing(rec, h) < 5.1 && plain.closing(rec, h) < 1.1);
        // A scale of nothing or no number is a very small scale, not a division by zero: no speed comes out
        // of it that is not a number.
        CHECK(std::isfinite(odd.closing(rec, h)) && std::isfinite(odd.closing(rec, h + 9)));
        // The speed that makes a runner is the tuning's.
        Tuning quick;
        quick.runSpeed = 4.f;
        Book fast(quick), jog(quick);
        double u = 1, y = 10, w = 10;
        for (int i = 0; i < 40; ++i, u += .05, y -= .25, w -= .15) { // 5 and 3 metres a second
            fast.running(rec, h, Kind::Runner, y, 0, 0, 0, true, u);
            jog.running(rec, h, Kind::Runner, w, 0, 0, 0, true, u);
        }
        CHECK(fast.runner(rec, h, u) && !jog.runner(rec, h, u));
    }
    {
        // A runner's guard in the book is a guard like the others: a blow while it stands is blocked, the
        // openers open it for the full time, and a counter comes after three blocked blows in a row. The
        // caller (the glue) says `open` when he is not running at the hero: then the blow lands.
        Book book;
        const std::uint64_t rec = 0x2100;
        const std::uint32_t h = 0x00700010;
        double now = 70;
        CHECK(book.standing(rec, h, Kind::Runner, now));
        Outcome o = book.blow(rec, h, Kind::Runner, {.strength = .6f}, now);
        CHECK(o.verdict == Verdict::Blocked && o.streak == 1 && !o.retaliates);
        o = book.blow(rec, h, Kind::Runner, {.strength = .6f}, now + .4);
        CHECK(o.verdict == Verdict::Blocked && o.streak == 2 && !o.retaliates);
        o = book.blow(rec, h, Kind::Runner, {.strength = .6f}, now + .8);
        CHECK(o.verdict == Verdict::Blocked && o.streak == 3 && o.retaliates);
        // Not running at the hero: the blow lands, and the streak is over.
        o = book.blow(rec, h, Kind::Runner, {.open = true}, now + 1.);
        CHECK(o.verdict == Verdict::Lands && book.streak(rec, h) == 0 &&
              book.standing(rec, h, Kind::Runner, now + 1.1));
        // An opener opens it for its seconds, whatever he was doing.
        CHECK(book.open(rec, h, Kind::Runner, 6., now + 2.));
        CHECK(!book.standing(rec, h, Kind::Runner, now + 7.9) &&
              book.standing(rec, h, Kind::Runner, now + 8.1));
        // While it is broken a blow that finds him running lands too.
        CHECK(book.blow(rec, h, Kind::Runner, {}, now + 3.).verdict == Verdict::Lands);
        // A dodge over him opens it, as it opens every guard.
        CHECK(book.open(rec, h, Kind::Runner, 3., now + 20.) &&
              !book.standing(rec, h, Kind::Runner, now + 22.9) &&
              book.standing(rec, h, Kind::Runner, now + 23.1));
        // A gadget that hurts him opens it (not the mod's own blows).
        CHECK(!book.hurt(rec, h, Kind::Runner, 100.f, now + 30));
        CHECK(book.hurt(rec, h, Kind::Runner, 80.f, now + 31) &&
              !book.standing(rec, h, Kind::Runner, now + 33));
        CHECK(book.standing(rec, h, Kind::Runner, now + 35.1));
        // What the mod's own blow takes from him is no stagger.
        book.blow(rec, h, Kind::Runner, {.open = true}, now + 40);
        CHECK(!book.hurt(rec, h, Kind::Runner, 60.f, now + 40.3) &&
              book.standing(rec, h, Kind::Runner, now + 40.4));
        // A web: never a guard in his webbed state, and the book remembers the web (the glue holds it for
        // `webbed` seconds).
        CHECK(book.webbed(rec, h, Kind::Runner, now + 50) && book.sinceWebbed(rec, h, now + 51) < 1.1);
        // No number of blows breaks a runner's guard.
        Book hard;
        for (int i = 0; i < 20; ++i)
            CHECK(hard.blow(rec, h, Kind::Runner, {.strength = 1.f}, 100 + i * .2).verdict ==
                  Verdict::Blocked);
        CHECK(hard.standing(rec, h, Kind::Runner, 105));
    }
    {
        // The whole of it, as the glue runs it: he runs at the hero and the blow is blocked; he reaches him
        // and begins his attack (his state is no running one, the next sample says so) and the blow lands.
        Book book;
        const std::uint64_t rec = 0x2200;
        const std::uint32_t h = 0x00700020;
        double t = 300, x = 12;
        for (int i = 0; i < 10; ++i, t += .05, x -= .25)
            book.running(rec, h, Kind::Runner, x, 0, 0, 0, true, t);
        const bool runsAtBlow = book.runner(rec, h, t);
        Blow b;
        b.strength = .5f;
        b.open = !runsAtBlow; // the glue: open when he does not run at the hero
        CHECK(runsAtBlow && book.blow(rec, h, Kind::Runner, b, t).verdict == Verdict::Blocked);
        t += .05;
        x -= .25;
        book.running(rec, h, Kind::Runner, x, 0, 0, 0, false, t); // the attack has begun
        // The grace (a third of a second) still says he ran a moment ago; the glue also asks for his state,
        // which says he does not.
        CHECK(book.runner(rec, h, t + .2) && !book.runner(rec, h, t + .6));
    }
    // a dodge over him opens the guard for as long as the caller says; a second one extends it.
    {
        Book book;
        CHECK(book.open(3, 3, Kind::Weapon, 3., 10));
        CHECK(!book.standing(3, 3, Kind::Weapon, 12.9));
        CHECK(!book.open(3, 3, Kind::Weapon, 3., 11)); // already open: extended, no new opening
        CHECK(!book.standing(3, 3, Kind::Weapon, 13.9));
        CHECK(book.standing(3, 3, Kind::Weapon, 14.1));
        CHECK(!book.open(3, 3, Kind::None, 3., 20) && !book.open(3, 3, Kind::Weapon, 0., 20));
        // An opening that is longer stays.
        book.open(3, 3, Kind::Weapon, 6., 30);
        CHECK(!book.open(3, 3, Kind::Weapon, 3., 31) && !book.standing(3, 3, Kind::Weapon, 35.9));
    }
    // Nothing the fists do breaks a guard: not any number of blows, and not his own counter. An enemy with
    // his guard open takes the blow, and the guard is untouched.
    {
        Book book;
        for (int i = 0; i < 30; ++i)
            CHECK(book.blow(3, 3, Kind::Brawler, {.strength = 1.f}, 10 + i * .3).verdict == Verdict::Blocked);
        CHECK(book.standing(3, 3, Kind::Brawler, 20));
        CHECK(book.blow(3, 3, Kind::Brawler, {.open = true}, 40).verdict == Verdict::Lands);
        CHECK(book.standing(3, 3, Kind::Brawler, 40.1));
        // An opener opens it for the full time.
        CHECK(book.open(3, 3, Kind::Brawler, 6., 50));
        CHECK(!book.standing(3, 3, Kind::Brawler, 55.9) && book.standing(3, 3, Kind::Brawler, 56.1));
        // The cause numbers the headset compares.
        CHECK(static_cast<int>(Cause::None) == 0 && static_cast<int>(Cause::Stagger) == 1 &&
              static_cast<int>(Cause::Dodge) == 2);
    }
    // blocked blows in a row make him answer - the counter: no damage, a stagger on the hero (game_punch).
    {
        Book book;
        Outcome o = book.blow(5, 1, Kind::Brawler, {.strength = .5f}, 10);
        CHECK(o.verdict == Verdict::Blocked && o.streak == 1 && !o.retaliates);
        o = book.blow(5, 1, Kind::Brawler, {.strength = .5f}, 10.4);
        CHECK(o.verdict == Verdict::Blocked && o.streak == 2 && !o.retaliates);
        o = book.blow(5, 1, Kind::Brawler, {.strength = .5f}, 10.8);
        CHECK(o.verdict == Verdict::Blocked && o.streak == 3 && o.retaliates); // the third in a row
        CHECK(book.counters(5, 1) == 1 && book.streak(5, 1) == 0);
        CHECK(book.standing(5, 1, Kind::Brawler, 10.9)); // his guard is up still
        // He does not answer again at once: the cooldown (2.5 s) holds it back, the streak waits at the top
        // ...
        for (double t : {11.0, 11.3, 11.6, 11.9, 12.2})
            CHECK(!book.blow(5, 1, Kind::Brawler, {.strength = .5f}, t).retaliates);
        CHECK(book.streak(5, 1) >= 3);
        // ... and the first blocked blow after it sets the counter off.
        CHECK(book.blow(5, 1, Kind::Brawler, {.strength = .5f}, 13.4).retaliates);
        CHECK(book.counters(5, 1) == 2);
        // Blows a long time apart (more than 3 s) do not add up.
        CHECK(!book.blow(5, 1, Kind::Brawler, {}, 30).retaliates);
        CHECK(!book.blow(5, 1, Kind::Brawler, {}, 34).retaliates);
        CHECK(!book.blow(5, 1, Kind::Brawler, {}, 38).retaliates);
        CHECK(book.streak(5, 1) == 1);
        // A blow that lands (his guard open) or an opening starts the streak over.
        book.blow(5, 1, Kind::Brawler, {}, 60);
        book.blow(5, 1, Kind::Brawler, {}, 60.3);
        CHECK(book.streak(5, 1) == 2);
        CHECK(book.blow(5, 1, Kind::Brawler, {.open = true}, 60.5).verdict == Verdict::Lands);
        CHECK(book.streak(5, 1) == 0);
        book.blow(5, 1, Kind::Brawler, {}, 61);
        book.blow(5, 1, Kind::Brawler, {}, 61.3);
        book.open(5, 1, Kind::Brawler, 6., 61.6);
        CHECK(book.streak(5, 1) == 0);
        // A gadget or a dodge that opens the guard does too.
        book.blow(5, 1, Kind::Brawler, {}, 70);
        book.blow(5, 1, Kind::Brawler, {}, 70.2);
        CHECK(book.streak(5, 1) == 2);
        book.open(5, 1, Kind::Brawler, 3., 70.3);
        CHECK(book.streak(5, 1) == 0);
        // A hero who cannot be staggered (off his feet) is not answered, and the counter waits for the next
        // blow.
        Book air;
        air.blow(6, 1, Kind::Shield, {.mayCounter = false}, 1);
        air.blow(6, 1, Kind::Shield, {.mayCounter = false}, 1.3);
        CHECK(!air.blow(6, 1, Kind::Shield, {.mayCounter = false}, 1.6).retaliates);
        CHECK(air.blow(6, 1, Kind::Shield, {}, 1.9).retaliates);
        // `counter 0` turns it off, `counter 2` makes it quicker; a weapon guard answers too.
        Tuning none;
        none.counterBlows = 0;
        Book quiet(none);
        for (int i = 0; i < 12; ++i)
            CHECK(!quiet.blow(7, 1, Kind::Weapon, {}, 1 + i * .3).retaliates);
        Tuning two;
        two.counterBlows = 2;
        Book quick(two);
        quick.blow(8, 1, Kind::Weapon, {}, 1);
        CHECK(quick.blow(8, 1, Kind::Weapon, {}, 1.4).retaliates);
        // A blow at an enemy with no guard is no business of the book's.
        CHECK(!quick.blow(9, 1, Kind::None, {}, 1).retaliates);
        // The counter is the answer of a guard that stood: it is never set off by a blow that lands.
        Book lands;
        for (int i = 0; i < 6; ++i)
            CHECK(!lands.blow(10, 1, Kind::Brawler, {.open = true}, 1 + i * .3).retaliates);
    }
    // guard.txt: the counter and the pose.
    {
        Rules r;
        CHECK(r.pose && r.tuning.counterBlows == 3.f);
        CHECK(parseRules("counter 5\npose off\n", r) == 0);
        CHECK(!r.pose && r.tuning.counterBlows == 5.f);
        CHECK(parseRules("counter 0\nPOSE On\n", r) == 0);
        CHECK(r.pose && r.tuning.counterBlows == 0.f);
        CHECK(parseRules("counter 0.5\ncounter 99\ncounter x\npose maybe\n", r) == 4);
        CHECK(r.tuning.counterBlows == 0.f);
    }
    // Enemies with no guard are never touched.
    {
        Book book;
        CHECK(book.blow(1, 1, Kind::None, {.strength = 1.f}, 1).verdict == Verdict::Lands);
        CHECK(!book.standing(1, 1, Kind::None, 1));
        CHECK(book.size() == 0);
    }
    // A record's address taken by the next enemy (a new handle) starts over: no inherited broken guard.
    {
        Book book;
        book.open(8, 100, Kind::Brawler, 6., 5);
        CHECK(!book.standing(8, 100, Kind::Brawler, 6));
        CHECK(book.standing(8, 101, Kind::Brawler, 6));
        CHECK(book.blow(8, 101, Kind::Brawler, {.strength = 1.f}, 6).verdict == Verdict::Blocked);
        CHECK(book.size() == 1);
        CHECK(book.standing(8, 101, Kind::Brawler, 6));
    }
    // The census is written once for each enemy.
    {
        Book book;
        CHECK(book.introduce(4, 40, 1));
        CHECK(!book.introduce(4, 40, 2));
        CHECK(book.introduce(4, 41, 3)); // another enemy in the same record
        CHECK(book.introduce(5, 50, 3));
    }
    // Enemies not seen for a long time are forgotten; the book never grows past a hundred.
    {
        Book book;
        book.blow(1, 1, Kind::Brawler, {}, 0);
        book.blow(2, 2, Kind::Brawler, {}, 150);
        book.prune(200);
        CHECK(book.size() == 1);
        for (std::uint64_t i = 10; i < 400; ++i)
            book.blow(i, static_cast<std::uint32_t>(i), Kind::Brawler, {}, 200 + i * .01);
        book.prune(205);
        CHECK(book.size() <= 100);
        book.clear();
        CHECK(book.size() == 0);
    }
    // The tuning changes the numbers.
    {
        Book book;
        Tuning t;
        t.counterBlows = 2.f;
        book.tune(t);
        CHECK(book.tuning().counterBlows == 2.f);
        CHECK(!book.blow(1, 1, Kind::Weapon, {.strength = 1.f}, 1).retaliates);
        CHECK(book.blow(1, 1, Kind::Weapon, {.strength = 1.f}, 1.3)
                  .retaliates); // the second in a row answers now
    }
    // the recoil - a critically damped spring that a kick pulls out and that comes home without overshoot.
    {
        using spidy::Recoil;
        using spidy::Vec3;
        constexpr float dt = 1.f / 90;
        // A kick of 3 cm back (+z): the view goes out to about 3 cm within three or four frames, only along
        // z, never past home on the way back, and is home within half a second.
        Recoil r;
        CHECK(!r.active() && r.offset().z == 0.f);
        r.kick({0, 0, 1}, .03f);
        CHECK(r.active());
        float peak = 0, peakAt = 0, last = -1;
        bool wentBack = false;
        for (int i = 1; i <= 90; ++i) {
            const Vec3 x = r.update(dt);
            CHECK(x.x == 0.f && x.y == 0.f);
            CHECK(x.z >= 0.f);
            if (x.z > peak) {
                peak = x.z;
                peakAt = i * dt;
            }
            if (i > 8 && x.z > last + 1e-6f)
                wentBack = true;
            last = x.z;
            if (i == 18)
                CHECK(x.z < .03f * .15f); // 0.2 s: most of the way home
        }
        CHECK(peak > .029f && peak < .0301f);
        CHECK(peakAt > .02f && peakAt < .06f);
        CHECK(!wentBack); // after the peak it only comes home
        CHECK(!r.active() && r.offset().z == 0.f);
        // Level and unit: the height of `away` is dropped and its length does not matter.
        r.kick({3, 5, 4}, .05f);
        Vec3 far{};
        for (int i = 0; i < 20; ++i) {
            const Vec3 x = r.update(dt);
            if (x.x > far.x)
                far = x;
        }
        CHECK(far.y == 0.f);
        CHECK(std::abs(far.x / far.z - .75f) < 1e-3f); // (3, 4) is 0.6 : 0.8
        CHECK(std::abs(std::sqrt(far.x * far.x + far.z * far.z) - .05f) < .0012f);
        r.reset();
        CHECK(!r.active());
        // Kicks that are nothing: no size, no way, a way straight up, numbers that are not numbers.
        const float nan = std::nanf("");
        r.kick({0, 0, 1}, 0.f);
        r.kick({0, 0, 1}, -.03f);
        r.kick({0, 0, 1}, nan);
        r.kick({0, 0, 0}, .03f);
        r.kick({0, 1, 0}, .03f);
        r.kick({nan, 0, 1}, .03f);
        r.kick({0, 0, 1e-6f}, .03f);
        CHECK(!r.active());
        // Kicks add, and a hail of them stays within `farthest`.
        for (int i = 0; i < 50; ++i)
            r.kick({0, 0, -1}, .1f);
        float most = 0;
        for (int i = 0; i < 90; ++i)
            most = (std::max)(most, std::abs(r.update(dt).z));
        CHECK(most <= Recoil::farthest + 1e-6f && most > .09f);
        r.reset();
        r.kick({0, 0, 1}, .02f);
        r.update(dt);
        r.kick({0, 0, 1}, .02f);
        float two = 0;
        for (int i = 0; i < 40; ++i)
            two = (std::max)(two, r.update(dt).z);
        CHECK(two > .03f); // more than one kick alone gives
        // One long step is a tenth of a second at most, and the closed form is the same however it is cut.
        Recoil a, b;
        a.kick({1, 0, 0}, .03f);
        b.kick({1, 0, 0}, .03f);
        for (int i = 0; i < 10; ++i)
            a.update(.01f);
        const Vec3 whole = b.update(.1f), pieces = a.offset();
        CHECK(std::abs(whole.x - pieces.x) < 1e-6f);
        Recoil c;
        c.kick({1, 0, 0}, .03f);
        c.update(.1f);
        Recoil d;
        d.kick({1, 0, 0}, .03f);
        d.update(60.f); // a stall: the same as a tenth
        CHECK(std::abs(c.offset().x - d.offset().x) < 1e-7f);
        // A step of no time, or of no number, changes nothing.
        const Vec3 before = c.offset();
        c.update(0.f);
        c.update(nan);
        c.update(-1.f);
        CHECK(c.offset().x == before.x);
        // Not allowed (the view stands on a surface): what was pulling stops, and the offset eases home.
        Recoil e;
        e.kick({0, 0, 1}, .03f);
        e.update(dt);
        const float first = e.offset().z;
        float still = first;
        for (int i = 0; i < 30; ++i)
            still = (std::max)(still, e.update(dt, false).z);
        CHECK(still < .02f && still == first);       // it never went further out than the first frame took it
        CHECK(!e.active() || e.offset().z < .0003f); // and it is home within a third of a second
        // ... and the way home is the kick's own: no slower than a tenth of the kick's way out is left at
        // 0.25 s.
        Recoil g;
        g.kick({0, 0, 1}, .03f);
        g.update(dt);
        for (int i = 0; i < 22; ++i)
            g.update(dt, false);
        CHECK(g.offset().z < .0019f);
        e.reset();
        e.kick({0, 0, 1}, .03f);
        CHECK(e.update(dt, false).z == 0.f); // a kick that came where it is not allowed does nothing
    }
    // ---- the guard pose (guard_pose.hpp)
    // -------------------------------------------------------------------
    {
        using namespace spidy;
        namespace gp = spidy::guard_pose;
        // The name hash is the game's (native_body.cpp's): the same CRC steps.
        CHECK(gp::nameHash("") == 0xedb88320u);
        CHECK(gp::nameHash("head") != gp::nameHash("chest") &&
              gp::nameHash("LF_uparm") != gp::nameHash("RT_uparm"));

        // The cue: up in 90 ms, held 0.85 s past the latest blow, down in 0.3 s.
        gp::Cue c;
        CHECK(gp::done(c, 5) && gp::guardWeight(c, 5) == 0.f && !gp::evaluate(c, 5).any());
        gp::raise(c, 10);
        CHECK(gp::guardWeight(c, 10) == 0.f);
        CHECK(std::abs(gp::guardWeight(c, 10 + gp::riseSeconds * .5) - .5f) < 1e-4f);
        CHECK(gp::guardWeight(c, 10.1) == 1.f && gp::guardWeight(c, 10.8) == 1.f);
        CHECK(!gp::done(c, 10.8));
        const float lowering = gp::guardWeight(c, 11.0);
        CHECK(lowering > 0.f && lowering < 1.f && gp::guardWeight(c, 11.2) < lowering);
        CHECK(gp::guardWeight(c, 11.2) == 0.f && gp::done(c, 11.3));
        // A guard raised again while it goes down goes on from where it is, with no jump.
        gp::raise(c, 11.0);
        CHECK(std::abs(gp::guardWeight(c, 11.0) - lowering) < 2e-3f);
        CHECK(gp::guardWeight(c, 11.0 + .06) > lowering && gp::guardWeight(c, 11.2) == 1.f);
        // ... and a blow while it is up extends it.
        gp::raise(c, 11.5);
        CHECK(gp::guardWeight(c, 12.3) == 1.f && gp::guardWeight(c, 12.6) < 1.f);
        // The weight never leaves 0..1 and never jumps by more than the step allows.
        {
            gp::Cue k;
            gp::raise(k, 0);
            gp::raise(k, .3);
            gp::drop(k, .8);
            gp::raise(k, 1.0);
            float last = 0;
            for (double t = 0; t < 4; t += .005) {
                const float w = gp::guardWeight(k, t);
                CHECK(w >= 0.f && w <= 1.f && std::abs(w - last) < .1f);
                last = w;
            }
        }
        // A blocked blow's jolt peaks at its strength 50 ms after the blow and is gone by 0.4 s.
        gp::Cue h;
        gp::hit(h, 20, .8f);
        CHECK(h.hitStrength == .8f);
        CHECK(std::abs(gp::evaluate(h, 20 + gp::joltPeak).jolt - .8f) < 1e-3f);
        CHECK(gp::evaluate(h, 20).jolt == 0.f && gp::evaluate(h, 20 + gp::joltSeconds).jolt < .01f);
        CHECK(gp::evaluate(h, 20.02).jolt < gp::evaluate(h, 20.05).jolt &&
              gp::evaluate(h, 20.2).jolt < gp::evaluate(h, 20.1).jolt);
        CHECK(gp::evaluate(h, 20.1).guard == 1.f);
        gp::hit(h, 30, std::nanf("")); // no number: a middling blow
        CHECK(h.hitStrength == .5f);
        gp::hit(h, 31, 7.f);
        CHECK(h.hitStrength == 1.f);
        // The swing: wind-up, strike, back; the guard holds all through it.
        gp::Cue sw;
        gp::swing(sw, 40);
        CHECK(gp::evaluate(sw, 40).wind == 0.f && gp::evaluate(sw, 40).strike == 0.f);
        const auto mid = gp::evaluate(sw, 40 + gp::windSeconds * .5);
        CHECK(mid.wind > .4f && mid.wind < .6f && mid.strike == 0.f && mid.twist < 0.f);
        const auto top = gp::evaluate(sw, 40 + gp::windSeconds);
        CHECK(std::abs(top.wind - 1.f) < 1e-3f && top.strike < 1e-3f && std::abs(top.twist + .8f) < 1e-2f);
        const auto out = gp::evaluate(sw, 40 + gp::windSeconds + gp::strikeSeconds);
        CHECK(out.wind < 1e-3f && std::abs(out.strike - 1.f) < 1e-3f && std::abs(out.twist - 1.f) < 1e-3f);
        const auto contact = gp::evaluate(sw, 40 + gp::contactSeconds);
        CHECK(contact.strike > .95f && contact.strike < 1.f);
        const auto back =
            gp::evaluate(sw, 40 + gp::windSeconds + gp::strikeSeconds + gp::recoverSeconds * .5);
        CHECK(back.strike > .4f && back.strike < .6f);
        CHECK(!gp::evaluate(sw, 40 + gp::swingSeconds).any() ||
              gp::evaluate(sw, 40 + gp::swingSeconds).guard > .9f);
        CHECK(gp::evaluate(sw, 40 + gp::swingSeconds + .01).wind == 0.f &&
              gp::evaluate(sw, 40 + gp::swingSeconds + .01).strike == 0.f);
        for (double t = 40; t < 41.5; t += .004) {
            const auto e = gp::evaluate(sw, t);
            CHECK(e.wind >= 0.f && e.strike >= 0.f && e.wind + e.strike <= 1.0001f && e.guard >= 0.f &&
                  e.guard <= 1.f);
            if (t > 40 + gp::riseSeconds && t < 40 + gp::swingSeconds)
                CHECK(e.guard == 1.f); // the guard holds through the swing
        }
        // The contact moment is inside the strike.
        CHECK(gp::contactSeconds > gp::windSeconds &&
              gp::contactSeconds < gp::windSeconds + gp::strikeSeconds);
        // His guard opening (a gadget, a dodge) puts it all down and calls the swing off.
        gp::drop(sw, 40.1);
        CHECK(gp::evaluate(sw, 40.2).wind == 0.f && gp::evaluate(sw, 40.2).strike == 0.f);
        CHECK(gp::guardWeight(sw, 40.1 + gp::lowerSeconds) == 0.f);
        CHECK(gp::done(sw, 41));

        // The table: one cue an enemy, the oldest used goes when it is full, nothing stays after its time.
        gp::Table table;
        for (std::uint64_t r = 1; r <= 8; ++r)
            table.post(r, gp::moveBlock, .5f, .1 * static_cast<double>(r));
        CHECK(table.cue(1) && table.cue(8));
        table.post(9, gp::moveRefuse, 1.f, .9); // all eight are live: the one used longest ago goes
        CHECK(!table.cue(1) && table.cue(9) && table.cue(2));
        table.post(2, gp::moveCounter, 1.f, 1.0);
        CHECK(table.cue(2)->swingAt == 1.0);
        std::array<gp::Table::Slot, gp::Table::slots> live;
        CHECK(table.live(1.0, live) == 8);
        table.post(0, gp::moveBlock, 1.f, 1.0); // nobody's
        table.post(99, gp::moveDrop, 0.f, 1.0); // a drop for one never seen is nothing
        table.post(99, gp::moveNone, 0.f, 1.0);
        table.post(99, 77, 0.f, 1.0);
        CHECK(!table.cue(99) && !table.cue(0));
        table.post(3, gp::moveDrop, 0.f, 1.0);
        CHECK(gp::guardWeight(*table.cue(3), 1.2) < gp::guardWeight(*table.cue(4), 1.2));
        CHECK(table.prune(100) == 0 && table.live(100, live) == 0 && !table.cue(2));
        // A finished enemy's slot is the one a new enemy takes, not a live one.
        gp::Table few;
        for (std::uint64_t r = 1; r <= 8; ++r)
            few.post(r, gp::moveRefuse, 1.f, r < 8 ? 0. : 5.);
        few.post(50, gp::moveRefuse, 1.f, 5.5); // 1..7 finished at 0.85 + 0.35 s, 8 is still up
        CHECK(few.cue(50) && few.cue(8));

        // The rig is found by the hero's joint names.
        gp::Rig rig;
        auto built = pose_rig::build();
        CHECK(gp::prepare(rig, built.parent, built.hash, built.rest) == gp::Why::ok && rig.ready);
        CHECK(rig.pelvis == pose_rig::pelvis && rig.chest == pose_rig::chest && rig.head == pose_rig::head);
        CHECK(rig.arms[0].upper == pose_rig::lUpper && rig.arms[0].lower == pose_rig::lLower &&
              rig.arms[0].hand == pose_rig::lHand);
        CHECK(rig.arms[1].upper == pose_rig::rUpper && rig.arms[1].hand == pose_rig::rHand);
        CHECK(rig.arms[0].belowUpper.size() == 4 && rig.arms[0].belowLower.size() == 3);
        CHECK(rig.belowChest.size() == 13 && rig.belowChest.front() == pose_rig::chest);
        CHECK(std::find(rig.belowChest.begin(), rig.belowChest.end(), pose_rig::lFoot) ==
              rig.belowChest.end());
        CHECK(rig.belowHead.size() == 1 && rig.belowHead[0] == pose_rig::head);
        // ... and refused when it is not a rig this can pose.
        {
            gp::Rig r2;
            auto mirrored = pose_rig::build(true);
            CHECK(gp::prepare(r2, mirrored.parent, mirrored.hash, mirrored.rest) == gp::Why::convention &&
                  !r2.ready);
            auto bad = pose_rig::build();
            std::swap(bad.parent[7], bad.parent[8]); // a joint whose parent comes after it
            bad.parent[7] = 8;
            CHECK(gp::prepare(r2, bad.parent, bad.hash, bad.rest) == gp::Why::shape);
            auto noName = pose_rig::build();
            noName.hash[pose_rig::lLower] = 1;
            CHECK(gp::prepare(r2, noName.parent, noName.hash, noName.rest) == gp::Why::names);
            auto detached = pose_rig::build();
            detached.parent[pose_rig::rUpper - 1] =
                0; // the right arm's clavicle hangs off the root, not the chest
            CHECK(gp::prepare(r2, detached.parent, detached.hash, detached.rest) == gp::Why::names);
            auto small = pose_rig::build();
            small.parent.resize(10);
            small.hash.resize(10);
            CHECK(gp::prepare(r2, small.parent, small.hash, small.rest) == gp::Why::shape);
            auto shortRest = pose_rig::build();
            shortRest.rest.resize(40);
            CHECK(gp::prepare(r2, shortRest.parent, shortRest.hash, shortRest.rest) == gp::Why::shape);
            CHECK(std::string(gp::whyName(gp::Why::convention)).size() > 3);
        }

        // The pose. The frame is measured from the joints.
        const std::vector<float> stand = built.rest;
        const auto frame = gp::measure(stand.data(), rig);
        CHECK(frame.ok && frame.left.x > .99f && frame.up.y > .99f && frame.forward.z > .99f);
        CHECK(std::abs(frame.scale - .57f / .62f) < 1e-3f);
        const auto bone = [](const std::vector<float>& p, int a, int b) {
            return length(pose_rig::at(p, a) - pose_rig::at(p, b));
        };
        // Nothing to show: the joints are not touched.
        {
            std::vector<float> p = stand;
            CHECK(!gp::apply(p.data(), rig, gp::Eval{}));
            CHECK(p == stand);
            gp::Rig unready;
            CHECK(!gp::apply(p.data(), unready, gp::Eval{.guard = 1.f}) && p == stand);
            CHECK(!gp::apply(nullptr, rig, gp::Eval{.guard = 1.f}));
        }
        // The guard: both hands come up in front of the face, the bones keep their lengths, what is not the
        // upper body is not touched, the chest joint stays where it is.
        {
            std::vector<float> p = stand;
            CHECK(gp::apply(p.data(), rig, gp::Eval{.guard = 1.f}));
            const auto f = gp::measure(p.data(), rig);
            CHECK(f.ok);
            for (int i = 0; i < 2; ++i) {
                const auto& arm = rig.arms[static_cast<size_t>(i)];
                // (In the frame he stood in before the pose: +x his left, +z forward.)
                const Vec3 wanted = pose_rig::at(p, pose_rig::head) +
                                    Vec3{(i == 0 ? 1.f : -1.f) * .085f * frame.scale, 0, .20f * frame.scale};
                CHECK(length(pose_rig::at(p, arm.hand) - wanted) < 4e-3f);
                CHECK(std::abs(bone(p, arm.upper, arm.lower) - .30f) < 2e-4f);
                CHECK(std::abs(bone(p, arm.lower, arm.hand) - .27f) < 2e-4f);
                // The elbow is below the line from the shoulder to the hand: it is a boxer's guard, not a
                // chicken's wing.
                CHECK(pose_rig::at(p, arm.lower).y < pose_rig::at(p, arm.upper).y);
                // In front of the chest.
                CHECK(pose_rig::at(p, arm.hand).z > .12f);
            }
            CHECK(length(pose_rig::at(p, pose_rig::chest) - pose_rig::at(stand, pose_rig::chest)) < 1e-6f);
            CHECK(length(pose_rig::at(p, pose_rig::pelvis) - pose_rig::at(stand, pose_rig::pelvis)) < 1e-6f);
            for (int j : {0, 1, 2, 16, 17, 18, 19, 20, 21})
                CHECK(std::memcmp(p.data() + j * 16, stand.data() + j * 16, 64) == 0);
            // He leans in a little behind the guard: the head is forward of the chest.
            CHECK(pose_rig::at(p, pose_rig::head).z > .02f);
            // The axes stay square and unit (a turn is no scale or skew).
            for (int j = 0; j < static_cast<int>(stand.size() / 16); ++j) {
                const Vec3 x{p[j * 16], p[j * 16 + 1], p[j * 16 + 2]},
                    y{p[j * 16 + 4], p[j * 16 + 5], p[j * 16 + 6]},
                    z{p[j * 16 + 8], p[j * 16 + 9], p[j * 16 + 10]};
                CHECK(std::abs(length(x) - 1) < 1e-4f && std::abs(length(y) - 1) < 1e-4f &&
                      std::abs(length(z) - 1) < 1e-4f);
                CHECK(std::abs(dot(x, y)) < 1e-4f && std::abs(dot(y, z)) < 1e-4f &&
                      std::abs(dot(x, z)) < 1e-4f);
            }
        }
        // Half a guard is half way.
        {
            std::vector<float> full = stand, half = stand;
            gp::apply(full.data(), rig, gp::Eval{.guard = 1.f});
            gp::apply(half.data(), rig, gp::Eval{.guard = .5f});
            const Vec3 a = pose_rig::at(stand, pose_rig::lHand), b = pose_rig::at(full, pose_rig::lHand),
                       m = pose_rig::at(half, pose_rig::lHand);
            CHECK(length(m - a) > .25f * length(b - a) &&
                  length(m - b) > .1f); // neither where he began nor where he ends
        }
        // A blocked blow's jolt throws the body back; a strike's lean and the twist put the right shoulder
        // forward and the right hand out at the end of the arm.
        {
            std::vector<float> p = stand;
            CHECK(gp::apply(p.data(), rig, gp::Eval{.guard = 1.f, .jolt = 1.f}));
            CHECK(pose_rig::at(p, pose_rig::head).z < 0.f); // the jolt (0.15 rad) outweighs the lean (0.12)
            std::vector<float> q = stand;
            gp::Eval e{.guard = 1.f, .wind = 0.f, .strike = 1.f, .twist = 1.f};
            CHECK(gp::apply(q.data(), rig, e));
            const Vec3 right = pose_rig::at(q, pose_rig::rUpper), left = pose_rig::at(q, pose_rig::lUpper);
            CHECK(right.z - left.z > .1f);
            const Vec3 reach = pose_rig::at(q, pose_rig::rHand) - right;
            CHECK(length(reach) > .5f && length(reach) < .575f);
            // ... straight ahead of where he faces (+z), however far the body turned to throw it.
            CHECK(reach.z > .5f && std::abs(reach.x) < .05f);
            // The left hand keeps its guard.
            const float k = .57f / .62f;
            CHECK(length(pose_rig::at(q, pose_rig::lHand) -
                         (pose_rig::at(q, pose_rig::head) + Vec3{.085f * k, 0, .20f * k})) < 5e-3f);
            // The wind-up draws the right hand back beside the shoulder.
            std::vector<float> w = stand;
            CHECK(gp::apply(w.data(), rig, gp::Eval{.guard = 1.f, .wind = 1.f, .twist = -.8f}));
            CHECK(pose_rig::at(w, pose_rig::rHand).z - pose_rig::at(w, pose_rig::rUpper).z < 0.f);
            // The strike is farther out than the wind-up.
            CHECK(pose_rig::at(q, pose_rig::rHand).z > pose_rig::at(w, pose_rig::rHand).z + .4f);
        }
        // A rig that turns, leans and crouches in the animation is posed in its own frame: the same guard,
        // turned with it.
        {
            std::vector<float> p = stand;
            const Quat turned = Quat::yaw(.7f);
            // Turn the whole body about the vertical through the pelvis, as the animation might.
            for (int j = 0; j < static_cast<int>(p.size() / 16); ++j) {
                float* r = p.data() + j * 16;
                for (int row = 0; row < 3; ++row) {
                    const Vec3 v = turned.rotate({r[row * 4], r[row * 4 + 1], r[row * 4 + 2]});
                    r[row * 4] = v.x;
                    r[row * 4 + 1] = v.y;
                    r[row * 4 + 2] = v.z;
                }
                const Vec3 pos = turned.rotate({r[12], r[13], r[14]});
                r[12] = pos.x;
                r[13] = pos.y;
                r[14] = pos.z;
            }
            std::vector<float> a = stand, b = p;
            gp::apply(a.data(), rig, gp::Eval{.guard = 1.f, .jolt = .4f});
            gp::apply(b.data(), rig, gp::Eval{.guard = 1.f, .jolt = .4f});
            for (int j : {pose_rig::head, pose_rig::lHand, pose_rig::rHand, pose_rig::lLower}) {
                const Vec3 expected = turned.rotate(pose_rig::at(a, j));
                CHECK(length(expected - pose_rig::at(b, j)) < 2e-3f);
            }
        }
        // Bad joints are left as they are and no number comes out of it that is not one.
        {
            std::vector<float> p = stand;
            p[pose_rig::lUpper * 16 + 12] = std::nanf("");
            const std::vector<float> before = p;
            CHECK(!gp::apply(p.data(), rig, gp::Eval{.guard = 1.f}));
            CHECK(std::memcmp(p.data(), before.data(), p.size() * sizeof(float)) == 0);
            std::vector<float> q = stand;
            q[pose_rig::rHand * 16 + 13] = INFINITY; // a hand at infinity
            const std::vector<float> beforeQ = q;
            CHECK(!gp::apply(q.data(), rig, gp::Eval{.guard = 1.f}));
            CHECK(std::memcmp(q.data(), beforeQ.data(), q.size() * sizeof(float)) == 0);
            std::vector<float> flat = stand; // shoulders on top of each other: no frame
            flat[pose_rig::rUpper * 16 + 12] = flat[pose_rig::lUpper * 16 + 12];
            const std::vector<float> beforeFlat = flat;
            CHECK(!gp::apply(flat.data(), rig, gp::Eval{.guard = 1.f}) && flat == beforeFlat);
            CHECK(!gp::apply(stand.data() == nullptr ? nullptr : p.data(), rig,
                             gp::Eval{.guard = std::nanf("")}));
            // An arm of no length is not posed, the rest is.
            std::vector<float> stub = stand;
            for (int i = 12; i < 15; ++i)
                stub[pose_rig::lLower * 16 + i] = stub[pose_rig::lUpper * 16 + i];
            CHECK(gp::apply(stub.data(), rig, gp::Eval{.guard = 1.f}));
            for (const float v : stub)
                CHECK(std::isfinite(v));
        }
        // Posed many times over (a job a frame) the same Eval gives the same joints each time from the same
        // start and nothing drifts: the pose job gives the animation's joints afresh every time.
        {
            std::vector<float> a = stand, b = stand;
            const gp::Eval e = gp::evaluate(h, 31.05);
            gp::apply(a.data(), rig, e);
            gp::apply(b.data(), rig, e);
            CHECK(a == b);
        }
    }
    // An enemy who has not detected the hero does not guard: the signs of it by name.
    {
        // A class on an enemy's actor that is a behavior of a fight - not the machinery every actor's
        // behaviors run on, and not the go-to a patrol has as well.
        for (const char* fights :
             {"BehaviorMeleeGame", "BehaviorMelee", "BehaviorRangedCombat", "BehaviorStandAndShoot",
              "BehaviorBotComboGraph", "BehaviorUseCombo", "BehaviorUseLosBlocker", "BehaviorReactGame",
              "BehaviorAttackResponse", "SlayerBotBehaviorMelee"})
            CHECK(fightBehavior(fights) && isBehavior(fights));
        for (const char* calm :
             {"BehaviorMap", "BehaviorHost", "BehaviorHostCache", "BehaviorEventQueue", "BehaviorGotoGame",
              "BotStateMeleeGame", "BotMeleeWeaponManager", "Behavior", ""})
            CHECK(!fightBehavior(calm));
        // The census lists every Behavior class but the machinery (the go-to included: it tells what a patrol
        // has).
        for (const char* is : {"BehaviorMeleeGame", "BehaviorGotoGame", "BehaviorRangedCombat",
                               "SlayerBotBehaviorMelee", "BehaviorPatrol"})
            CHECK(isBehavior(is));
        for (const char* isnt : {"BehaviorMap", "BehaviorHost", "BehaviorHostCache", "BehaviorEventQueue",
                                 "BotStateStand", "BotMeleeWeaponManager", ""})
            CHECK(!isBehavior(isnt));
        // States an enemy who has not detected the hero can be in: calm. Every other BotState is a sign of a
        // fight - and one the game has that this list does not know counts as one, which keeps his guard up.
        for (const char* calm : {"BotStateStand",
                                 "BotStateInitial",
                                 "BotStateBase",
                                 "BotStateNull",
                                 "BotStatePlayAnim",
                                 "BotStateLookAtTargets",
                                 "BotStatePointAtPOI",
                                 "BotStatePickUpItem",
                                 "BotStatePutDownItem",
                                 "BotStateRecoverHealth",
                                 "BotStateGoto",
                                 "BotStateGotoGame",
                                 "BotStateGotoLocal",
                                 "BotStateGoDirectionLocalGame",
                                 "BotStateFace",
                                 "BotStateFaceTarget",
                                 "BotStatePlayRelativeAnimLocal",
                                 "BotStatePlaySyncedAnimGuest",
                                 "BotStatePlayCinematic",
                                 "BotStateTreadWater"})
            CHECK(calmState(calm) && !fightState(calm));
        for (const char* fights :
             {"BotStateMeleeGame", "BotStateShuffleGame", "BotStateAim", "BotStateChaseRunTo",
              "BotStateHitReactGame", "BotStateWebStruggle", "BotStateStandUp", "BotStateDodge",
              "BotStateStagedBlock", "BotStateSurprise", "BotStateStunned", "BotStateDashAttack",
              "BotStateDash", "BotStateSomethingTheGameHasThatIDoNotKnow"})
            CHECK(fightState(fights) && !calmState(fights));
        // Not a bot state at all, or the bare prefix: no sign either way.
        for (const char* none : {"ThugBot", "BehaviorMeleeGame", "BotState", "", "Stand", "Goto"})
            CHECK(!fightState(none) && !calmState(none));
    }
    {
        // guard.txt: `detect on | off`, `detecthold SECONDS` and `swingkick on | off`. Detecting is on by
        // default.
        CHECK(Rules{}.detect && Rules{}.tuning.detectSeconds == 10.f && Rules{}.swingKick);
        Rules r;
        CHECK(parseRules("detect off\n", r) == 0 && !r.detect);
        CHECK(parseRules("DETECT On\n", r) == 0 && r.detect);
        CHECK(parseRules("detect\ndetect maybe\n", r) == 2 && r.detect); // bad lines leave it as it was
        CHECK(parseRules("detecthold 3\n", r) == 0 && r.tuning.detectSeconds == 3.f);
        CHECK(parseRules("detecthold 0.5\ndetecthold 600\n", r) == 0 && r.tuning.detectSeconds == 600.f);
        CHECK(parseRules("detecthold 0.4\ndetecthold 601\ndetecthold long\ndetecthold\n", r) == 4 &&
              r.tuning.detectSeconds == 600.f);
        CHECK(parseRules("swingkick off\n", r) == 0 && !r.swingKick);
        CHECK(parseRules("SwingKick ON\n", r) == 0 && r.swingKick);
        CHECK(parseRules("swingkick\nswingkick sometimes\n", r) == 2 && r.swingKick);
        // The other lines go on being read around them, and the old ones are as they were.
        Rules both;
        CHECK(parseRules("detect off\nswingkick off\nrunners on\n", both) == 0 && !both.detect &&
              !both.swingKick && both.runners);
        CHECK(Rules{}.runners == false && Rules{}.pose && !Rules{}.touch);
    }
    {
        // The book: an enemy it has never met has not detected the hero, and one it has met has not either
        // until a sign comes.
        Book book;
        const std::uint64_t rec = 0x4000;
        const std::uint32_t h = 0x00900001;
        const double t = 100;
        CHECK(!book.noticed(rec, h, t) && book.followedFor(rec, h, t) == 0.);
        book.hurt(rec, h, Kind::Weapon, 120.f, t); // met: his hit points, first read
        CHECK(!book.noticed(rec, h, t) && std::fabs(book.followedFor(rec, h, t + 2.) - 2.) < 1e-9);
        // A sign keeps him detected for detectSeconds (10) after it, to the second; the next one starts the
        // ten again.
        book.notice(rec, h, Kind::Weapon, t + 3.);
        CHECK(book.noticed(rec, h, t + 3.) && book.noticed(rec, h, t + 12.9) &&
              !book.noticed(rec, h, t + 13.1));
        book.notice(rec, h, Kind::Weapon, t + 12.);
        CHECK(book.noticed(rec, h, t + 21.9) && !book.noticed(rec, h, t + 22.1));
        // A different hold (guard.txt `detecthold`).
        Tuning quick;
        quick.detectSeconds = 2.f;
        Book brief(quick);
        brief.notice(rec, h, Kind::Weapon, t);
        CHECK(brief.noticed(rec, h, t + 1.9) && !brief.noticed(rec, h, t + 2.1));
        // An enemy with no guard is not followed: nothing is kept for him.
        book.notice(rec + 1, h, Kind::None, t);
        CHECK(!book.noticed(rec + 1, h, t) && book.size() == 1);
        // Another enemy of the same record (a new handle) starts with nothing: the record's address is the
        // next enemy's.
        CHECK(!book.noticed(rec, h + 1, t + 13.));
        book.hurt(rec, h + 1, Kind::Weapon, 90.f, t + 13.);
        CHECK(!book.noticed(rec, h + 1, t + 13.) && !book.noticed(rec, h, t + 13.) && book.size() == 1 &&
              book.followedFor(rec, h + 1, t + 14.) < 1.1);
        // The blow that begins a fight is a sign through notice(), and a blow does nothing of the kind by
        // itself: the book's verdict on it does not change with the sign (the stance is the game side's,
        // game_guard::stanceOf).
        Book fresh;
        CHECK(fresh.blow(rec, h, Kind::Weapon, {.strength = .5f}, t).verdict == Verdict::Blocked &&
              !fresh.noticed(rec, h, t));
    }
    {
        // A fall of his hit points is a sign, whoever's it is. The first reading is none, a reading that is
        // no lower is none, nor is a hair of a fall (rounding).
        Book fall;
        const std::uint64_t rec = 0x4002;
        const std::uint32_t h = 0x00900003;
        const double t = 200;
        fall.hurt(rec, h, Kind::Brawler, 160.f, t);
        CHECK(!fall.noticed(rec, h, t));
        fall.hurt(rec, h, Kind::Brawler, 160.f, t + 1.);
        CHECK(!fall.noticed(rec, h, t + 1.));
        fall.hurt(rec, h, Kind::Brawler, 170.f, t + 2.); // up: no sign
        CHECK(!fall.noticed(rec, h, t + 2.));
        fall.hurt(rec, h, Kind::Brawler, 169.995f, t + 3.);
        CHECK(!fall.noticed(rec, h, t + 3.));
        fall.hurt(rec, h, Kind::Brawler, 160.f, t + 4.); // ten points: a sign
        CHECK(fall.noticed(rec, h, t + 4.) && fall.noticed(rec, h, t + 13.9) &&
              !fall.noticed(rec, h, t + 14.1));
        // Whether the mod's own blow explains the fall does not matter to it (it does to the guard's opening,
        // as before).
        Book own;
        own.hurt(rec, h, Kind::Weapon, 100.f, t);
        own.own(rec, h, Kind::Weapon, t + 1.);
        own.hurt(rec, h, Kind::Weapon, 95.f, t + 1.1);
        CHECK(own.noticed(rec, h, t + 1.1) && own.standing(rec, h, Kind::Weapon, t + 1.2));
        // An enemy with no guard keeps no hit points, and so no sign.
        Book none;
        none.hurt(rec, h, Kind::None, 100.f, t);
        none.hurt(rec, h, Kind::None, 50.f, t + 1.);
        CHECK(!none.noticed(rec, h, t + 1.) && none.size() == 0);
    }
    {
        // The changes of whether he is detected, for the log: Detected when a sign comes after none, Lost
        // when the last one is more than detectSeconds old; an enemy just met is unaware, so that is no
        // change.
        Book book;
        const std::uint64_t rec = 0x4001;
        const std::uint32_t h = 0x00900002;
        const double t = 10;
        CHECK(book.detection(rec, h, t) == Book::Change::None);
        CHECK(book.detection(rec, h, t + .05) == Book::Change::None);
        book.notice(rec, h, Kind::Weapon, t + 1.);
        CHECK(book.detection(rec, h, t + 1.05) == Book::Change::Detected);
        CHECK(book.detection(rec, h, t + 1.1) == Book::Change::None);
        // A sign every four seconds keeps him detected, sample after sample.
        double u = t + 2.;
        for (int i = 0; i < 60; ++i, u += .5) {
            if (i % 8 == 0)
                book.notice(rec, h, Kind::Weapon, u);
            CHECK(book.detection(rec, h, u) == Book::Change::None);
        }
        // The last sign was at t + 30: ten seconds later he is lost, once.
        const double last = t + 30.;
        for (double v = u; v <= last + 9.9; v += .25)
            CHECK(book.detection(rec, h, v) == Book::Change::None);
        CHECK(book.detection(rec, h, last + 10.2) == Book::Change::Lost);
        CHECK(book.detection(rec, h, last + 10.7) == Book::Change::None);
        // And a new sign is a new detection.
        book.notice(rec, h, Kind::Weapon, last + 12.);
        CHECK(book.detection(rec, h, last + 12.05) == Book::Change::Detected);
        // Not followed for more than a second (he left the watch's list): no change is told of - the answer
        // is brought up to date.
        Book away;
        away.notice(rec, h, Kind::Weapon, t);
        CHECK(away.detection(rec, h, t + .1) == Book::Change::Detected);
        CHECK(away.detection(rec, h, t + 30.) == Book::Change::None && !away.noticed(rec, h, t + 30.));
        CHECK(away.detection(rec, h, t + 30.1) == Book::Change::None);
        away.notice(rec, h, Kind::Weapon, t + 30.2);
        CHECK(away.detection(rec, h, t + 30.25) == Book::Change::Detected);
        // An enemy with no guard has no sign to tell of; a new handle on the same record starts over.
        Book other;
        other.notice(rec, h, Kind::Weapon, t);
        CHECK(other.detection(rec, h, t + .1) == Book::Change::Detected);
        CHECK(other.detection(rec, h + 1, t + .2) == Book::Change::None &&
              !other.noticed(rec, h + 1, t + .2));
    }
    // A swing kick goes through the guard of an enemy whose block is the baton's.
    {
        const ClassVerdict baton = classOf("BotMeleeWeaponManager", Rules{});
        const ClassVerdict sword = classOf("SwordsmanBot", Rules{});
        const ClassVerdict brute = classOf("BruteBot", Rules{});
        const ClassVerdict slayer = classOf("SlayerBotBehaviorMelee", Rules{});
        const ClassVerdict shield = classOf("ShieldBot", Rules{});
        // An ordinary melee-weapon guard: the kick goes through.
        CHECK(baton.kind == Kind::Weapon && !baton.swordsman && kickPasses(baton.kind, baton.swordsman));
        // A swordsman's (a Weapon guard with the sword's block sound), a brute's and a slayer's (the Brawler
        // kind), a shield's and a runner's stop it as they stop a punch.
        CHECK(sword.swordsman && sword.kind == Kind::Weapon && !kickPasses(sword.kind, sword.swordsman));
        CHECK(brute.kind == Kind::Brawler && !kickPasses(brute.kind, brute.swordsman));
        CHECK(slayer.kind == Kind::Brawler && !kickPasses(slayer.kind, slayer.swordsman));
        CHECK(shield.kind == Kind::Shield && !kickPasses(shield.kind, shield.swordsman));
        CHECK(!kickPasses(Kind::Brawler, true) && !kickPasses(Kind::Shield, true) &&
              !kickPasses(Kind::Runner, false) && !kickPasses(Kind::Runner, true) &&
              !kickPasses(Kind::None, false) && !kickPasses(Kind::None, true));
        // A baton and a brute's class on one enemy: the stronger guard, the Brawler's - he blocks the kick.
        CHECK(!kickPasses(stronger(baton.kind, brute.kind), false));
        // Whose block it is is the sound's, which is what the player hears: baton - passes; sword, slayer,
        // shield - do not.
        CHECK(soundOf(Kind::Weapon, false) == Sound::Baton && soundOf(Kind::Weapon, true) == Sound::Sword &&
              soundOf(Kind::Brawler, false) == Sound::Slayer &&
              soundOf(Kind::Shield, false) == Sound::Shield);
        // The book (game_guard sets Blow::open for a kick that passes): the kick lands, nothing is counted as
        // blocked, and the blocked blows in a row start over. A punch right after is blocked again, as it
        // was.
        Book book;
        const std::uint64_t rec = 0x5000;
        const std::uint32_t h = 0x00a00001;
        double t = 300;
        CHECK(book.blow(rec, h, Kind::Weapon, {.strength = .6f}, t).verdict == Verdict::Blocked);
        CHECK(book.blow(rec, h, Kind::Weapon, {.strength = .6f}, t + .5).verdict == Verdict::Blocked);
        CHECK(book.streak(rec, h) == 2 && book.blocks(rec, h) == 2);
        const Outcome kick = book.blow(rec, h, Kind::Weapon, {.open = true, .strength = 1.f}, t + 1.);
        CHECK(kick.verdict == Verdict::Lands && !kick.retaliates && book.blocks(rec, h) == 2 &&
              book.streak(rec, h) == 0);
        CHECK(book.blow(rec, h, Kind::Weapon, {.strength = .6f}, t + 1.5).verdict == Verdict::Blocked);
        CHECK(book.standing(rec, h, Kind::Weapon, t + 1.6)); // his guard was never broken by it
    }
    if (failures)
        std::printf("%d check(s) failed\n", failures);
    else
        std::printf("guard tests passed\n");
    return failures ? 1 : 0;
}
