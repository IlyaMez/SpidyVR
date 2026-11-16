#pragma once
#include "game_targets.hpp"
#include "guard.hpp"
#include "math.hpp"
#include <cstdint>
#include <vector>

// Enemy types: guards in the game (guard.hpp has the rules and the book). Enemies with a baton or another
// melee weapon, swordsmen, brutes (heavy, armored or brawling ones) and shield bearers block the plain blows
// of the player's fists and web strikes, and no web drags an enemy whose guard stands. game_punch.cpp asks
// blow() for each blow it is about to land, game_grab.cpp asks holds() before it lets a web take an enemy,
// and the headset module reads report() for the feel of a blocked blow.
//
// Punching never breaks a guard, but a few blocked punches in a row make him swing back (blow() says so in
// Outcome::retaliates; game_punch staggers the hero when the swing arrives and says so with counterLanded()),
// and the report names the enemy of every block, refusal, counter and opening, for the headset to pose him
// (guard_pose.hpp, native_body.cpp).
//
// An enemy who has not detected the hero does not guard (watch() reads the signs of it; blow(), own() and
// ballHit() are signs too), and a swing kick - the dive - goes through a baton's guard (Strike::kick). A web
// ball does not open a guard (ballHit()): only an enemy seen in a webbed state is webbed, and one that runs
// at the hero is free of it. See guard.hpp.
//
// The launcher's Enemy types switch is the on/off for all of it (setEnemyTypes). Off by default: nobody
// guards, as before Enemy types.
//
// The settings are guard.txt in the Spidy folder (beside "Launch Spidy VR.cmd"), written with its comments
// the first time and read at every start: `off` turns it all off, and lines can name more classes that guard.
namespace spidy::game_guard {
// SpidyGuardSample's answer, for the headset.
struct Report {
    uint32_t magic = 0x44524755, version = 1, bytes = sizeof(Report);
    // 0 off (or not started), 1 on.
    uint32_t status{};
    // Blows a guard stopped, guards opened or broken, and web pulls refused, since the start. And the
    // counters an enemy began, and those that reached the hero.
    uint32_t blocks{}, breaks{}, refusals{}, counters{}, counterHits{};
    // The latest block: the hand that threw it, the guard (guards::Kind), and the latest opening: its cause
    // (guards::Cause).
    uint32_t lastHand{}, lastKind{}, lastCause{};
    float lastStrength{};
    uint64_t lastTarget{};
    Vec3 lastPoint{};
    // How far a blocked blow knocks the player's view back, metres (guard.txt `recoil`, in centimetres; 0:
    // not at all). The headset kicks its view that way, away from lastPoint (recoil.hpp).
    float recoil{};
    // Whose they were - the latest web pull refused, the latest counter begun (and so the latest to reach the
    // hero), and the latest guard opened or broken, as the actor records the game's enemies are known by.
    uint64_t refusedTarget{}, counterTarget{}, openedTarget{};
    // The enemy's guard is to be seen (guard.txt `pose`, 1 on by default).
    // The sound of the latest block: bits 0..7 the guards::Sound (his guard's - baton, sword, slayer or
    // shield), and lastAttacking (bit 8) when the blow met an enemy in the middle of an attack - he blocked
    // it and follows his own attack through, so the headset leaves his arms alone (no guard pose).
    uint32_t pose = 1, lastSound{};
    // The sound of the latest web pull refused - bits 0..7 the guards::Sound of the enemy's guard, the same
    // four as lastSound's - for the headset to play ImpactSound::WebBlockBaton + that when `refusals` goes
    // up.
    uint32_t refusedSound{};
    // The blocked blows in a row the latest block made on that enemy, itself included (Outcome::streak), and
    // the one that sets off his counter (guard.txt `counter`; 0: he never counters) - for the headset's
    // BLOCKED mark (web_visual.hpp's BlockView) to show how near his counter is.
    uint16_t lastStreak{}, counterBlows{};
};
// Report::lastSound's flag: the latest block met an enemy in the middle of an attack.
constexpr uint32_t lastAttacking = 0x100;
static_assert(sizeof(Report) % 8 == 0);
// One blow of the hero's at an enemy.
struct Strike {
    uint32_t hand{};
    float strength{}; // 0..1
    // He is in the middle of an attack: the game has warned the hero of it. (His guard holds all the same;
    // this only tells the headset to leave his arms alone and the punch module to stagger him, so the attack
    // stops.)
    bool attacking{};
    // The hero is on his feet, which is when the guard's counter can stagger him.
    bool grounded = true;
    Vec3 point{};
    // A swing kick: the dive, the feet-first blow of the hero's own flight at the enemy (game_punch: a web
    // strike whose hand is WebStrike::diveHand). An enemy with a baton's guard does not block it
    // (guards::kickPasses; guard.txt `swingkick`).
    bool kick{};
};
// The guard's log lines for the session log: the headset module (a separate one) reads them with
// SpidyGuardNote, whose request is a Note: in, the reader's count; out, the line.
struct Note {
    uint32_t seen{};
    char text[200]{};
};
// One line for the session log, from any thread.
void log(const char* text);
// The next line the reader (whose count is `seen`) has not read: false when there is none new.
bool note(uint32_t& seen, char* out, size_t size);
// Reads guard.txt, tunes the classes and the book, and starts. 0 when ready (also when guard.txt turns it
// off: nobody then guards).
uint32_t start(uintptr_t gameBase);
void stop();
bool running();
// The launcher's Enemy types (game_swing's settings, version 32). Off, nobody guards - what guard.txt's `off`
// does, and then every call below is as before the first guard: blows land, web pulls take, nothing is posed.
// Either one off turns the guards off. Off is the default, until the settings say on. It only remembers the
// choice, so it may come before start() or after stop(), from any thread, as often as the settings are sent.
// The log says when it changes.
void setEnemyTypes(bool enabled);
// Every sample, the bots of the target watch and where the hero's feet are. It reads the hit points of the
// ones with a guard - a fall that none of the mod's own blows explains (a gadget, a thrown prop or enemy)
// opens the guard - and, while a dodge is on (dodgeBegan), opens the guard of an enemy the dodge carries the
// hero over or past. It also follows every enemy's run at the hero (how fast he comes along the line to him,
// in the world's time: worldScale is the game's time scale, 1 or less in slow motion), and reads the hit
// points of every enemy, a runner's guard being the one every enemy may have. Cheap: it works at most twenty
// times a second. From the punch module's thread.
void watch(const std::vector<game_targets::Candidate>& bots, Vec3 heroFeet, float worldScale = 1.f);
// The hero's dodge began (the headset asked for one): where he is. The next moment or so is the dodge.
void dodgeBegan(Vec3 heroFeet);
// guard.txt says `touch on` (it is off by default): a blocked blow makes the enemy react with the game's own
// hit reaction, a flinch or a stagger (game_punch's touchBlocked).
bool touchWanted();
// The guard an enemy's classes (or his hit points) give him, whether it is up or not. None for the bosses,
// the civilians and his own side - and for an enemy with no guard of his own, though any enemy may guard
// while he runs at the hero if guard.txt says `runners on` (holds() says).
guards::Kind kindOf(const game_targets::Candidate& c);
// His guard is up now: he has one, it is not broken, and he stands free (not down, stunned, webbed - or
// webbed within the last `webbed` seconds of guard.txt - held or flung). An enemy in the middle of an attack
// holds his guard. So does any enemy, with a guard of his own or none, while he runs at the hero when
// guard.txt says `runners on` (watch() has seen him come at the hero fast enough, within the last moments,
// and his state is still a running one): the guard of a runner. Safe from any thread.
bool holds(const game_targets::Candidate& c);
// A blow of the hero's: what the guard does with it. Blocked blows are counted for the headset; a guard
// opened is written to the session log with the enemy's classes the first time.
guards::Outcome blow(const game_targets::Candidate& c, const Strike& strike);
// A web pull refused on a guarding enemy (counted for the headset, at most about twice a second, with the
// sound of his guard). Only the long web comes here, the pull that would take him (game_grab's pick); a web
// ball is another thing, and webs him (ballHit(), below).
void refused(const game_targets::Candidate& c);
// The mod itself has just staggered or flinched this enemy (game_punch's break of an attack): the state he
// enters is its own and does not open his guard to the blow that follows. Nothing for an enemy with no guard.
void own(const game_targets::Candidate& c);
// (game_shooter) one of the mod's web balls has just hit this enemy. A web ball webs an enemy only at the
// third hit or so, and it decays: the hit is the mod's own damage (its fall of hit points is no stagger from
// elsewhere, own()) and nothing more; whether he is webbed is for his state to say - every moment watch()
// sees him in a webbed state begins the `webbed` seconds, and they do not hold while he runs (he is free).
// The log says of the hit. Nothing for an enemy with no guard. From any thread.
void ballHit(const game_targets::Candidate& c);
// (game_grab) a web pull has just taken this enemy up. For one with a guard of his own the log says why his
// guard was not up (so that a guard that lets a pull through can be found by the log), and in which state he
// was. Nothing for the others. From the web grab's thread.
void taken(const game_targets::Candidate& c);
// The enemy's counter reached the hero (counted for the headset, which feels it).
void counterLanded(const game_targets::Candidate& c);
// The numbers for the headset.
Report report();
} // namespace spidy::game_guard
