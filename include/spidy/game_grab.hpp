#pragma once
#include "game_targets.hpp"
#include "web_grab.hpp"
#include "web_tie.hpp"
#include <cstdint>

// The web grab in the game. The core (web_grab) runs in the swing's world
// query callback (game_swing.cpp), on its own clock, with the same input and
// the game's own rays. Targets come from game_targets, and each kind moves
// the way the game itself moves it:
//
// - Props (ThrowableHelper actors) are freed as the game's own throw frees
//   them, and the web's law moves their Havok bodies from their actual
//   motion, contacts included (native_bodies). Let go, they fly, bounce,
//   slide and come to rest by the game's physics, in real time.
// - Thugs fly the game's own launched reaction, BotStateFlung, flailing.
//   The web's first pull deals one the kinetic blow the game answers with
//   that flight (a direct request for the state is overridden by a fighting
//   thug's AI: until October 8 webbed thugs just slid along on their movers),
//   and from the next step on the web steers the flight it brings, its pull
//   written as the flight's velocity every step. Let go of, the game flies it
//   on and lands it (BotStateGroundFlop, then BotStateStunned). A thug the
//   game would not fling, or a civilian, is steered through his own
//   MoverStandard instead (native movement Drive), and let go of, falls
//   under Spidy's rays until he is down.
// - What flies hurts what it strikes, as the game's thrown bodies do: a thug
//   whose flight is stopped by the world, or who lands hard, takes a kinetic
//   blow by how much speed he lost; a thug or a thrown prop striking another
//   thug knocks him down or flings him (web_grab's StrikeConfig).
//
// Pedestrians are kinematic crowd agents with no physics or flung state;
// they are not offered.
namespace spidy::game_grab {
struct Hand {
    uint32_t phase{}, kind{}; // GrabPhase and game_targets::Kind of the hand's target, 0 for none
    uint64_t target{};        // the target's actor record
    Vec3 end{};               // the target's centre, where the web ends
    float length{};
    uint32_t taut{};
    // How hard the web pulls, as a share of its strength (Grab::tension).
    float tension{};
    // The web this hand just let go of (thrown, released or lost) still
    // hangs from `target`, at `end`, for trailSeconds: it is drawn falling
    // away with it rather than left where the target was. phase is 0.
    uint32_t trailing{}, reserved{};
};
// How long a web let go of is reported trailing its target.
constexpr float trailSeconds = 1.5f;
struct Data {
    uint32_t magic = 0x53475244, version = 3, bytes = sizeof(Data), status{};
    int64_t sequence{};
    uint64_t steps{}, commands{}, grabs{}, yanks{}, catches{}, throws{}, releases{}, lost{}, flights{},
        landed{};
    uint32_t candidates{}, error{}, kinds{}, driveFailures{};
    Hand hands[2];
    Vec3 lastThrow{};  // launch velocity of the latest throw
    float timeScale{}; // physics time per second of the world's (native_bodies)
    // native_bodies: physics steps seen, props freed and rebuilt, body
    // velocities set, instance poses Spidy set, bots flung, flights steered,
    // requests the game refused, and leases that lapsed.
    uint64_t bodySteps{}, frees{}, rebuilds{}, writes{}, follows{}, flings{}, steers{}, refused{}, expired{};
    // The latest commanded prop's velocity through the step in flight, and
    // its actual velocity as that step began (real m/s); the real length of
    // the latest grab step and the physics length of the game's step.
    Vec3 commanded{}, observed{};
    float tickDt{}, stepDt{};
    // Thugs the web knocked into the game's flight (kinetic blows sent),
    // steps it steered such a flight, blows to flying thugs for what they
    // struck or landed on, and thugs struck by a flying thug or a thrown prop.
    uint64_t launches{}, flown{}, impacts{}, struck{};
};
static_assert(sizeof(Hand) == 48 && sizeof(Data) == 368);
using Call = unsigned long(__stdcall*)(void*);
// drive and driven: the movement module's SpidyMotionDrive and
// SpidyMotionDrivenSample. kinds: bits 1 << game_targets::Kind to offer.
// Returns native_bodies' start code: 0 when it started, or nothing is offered.
uint32_t start(uintptr_t base, Call drive, Call driven, uint32_t kinds);
uint32_t stop();
// Whether a start offered anything to catch.
bool offering();
// The VR settings switch catching off and on during play,
// in the swing's callback lock. Off, the webs let go of what they hold at the
// next sample and every press swings; the watch keeps running. On by default.
void allow(bool);
// One input sample, in the swing's callback before the swing sees it.
// Returns the input for the swing, without the grips a grab owns. `kept`: a
// bit per hand whose press the swing keeps (WebGrab::claim).
Input claim(float inputSeconds, const Input&, const WorldQueries&, const Body& player, unsigned kept = 0);
// The swing's view of an input between grab ticks.
Input forSwing(const Input&);
// What a grip press aimed along `aim` would catch now, if anything: the pick
// a press makes, without taking the target up. Nothing while a web holds
// something for `hand`, or nothing is offered.
std::optional<GrabTarget> preview(unsigned hand, Pose aim, const WorldQueries&);
// Whether a web holds something for this hand.
bool holds(unsigned hand);
// Whether the game has stepped its physics since the grab last stepped, and
// for how long. The grab steps with the game's physics, not the player's
// mover, which does not step while the player perches or stands: one command
// per physics step, each from the state that step left. Ticking twice per
// step restarted twice from the same state and gave targets half the pull.
bool due(float& dt);
// One grab step, of the length due() gave. player: the player's actor
// record, who deals the web's blows.
void step(float dt, const WorldQueries&, uint64_t player);
// Lets every web go; a bot on a flight the web steered is handed to the game.
void cancel();
// A fist landed on this bot: one the web was bringing to a player in the air
// (an air yank) is the blow's from here.
void struck(uint64_t record);
Data data();
// The web tie (web_tie.hpp, docs/WEB-TIE.md): webbed-up enemies stuck by a
// web to a surface until the fight is over. Each is held at his place
// through his own mover (native movement Drive), as a thug on his feet is
// steered: upright in his cocoon, no blow, no damage; one already in the
// game's flight when tied has that flight steered there instead. A tie lets
// go when no other enemy has been in the fight within fightReach of the
// player for TieConfig::fightOverSeconds, when its target is gone or down
// (knocked out), when a hand's web takes him again, or when the grab stops.
// A pause or a menu keeps the ties: their targets are held again once play
// steps on.
constexpr float fightReach = 60;
struct TieLine {
    Vec3 anchor{}, end{}; // where the web sticks to the surface, the target's centre
    float distance{};     // from his centre to his place
    // 1 in the game's flight, 2 on his mover, 4 he came to his place, 8 he
    // arrived (the tie webs him), 16 down (not up: a flop, a stick to come)
    uint32_t flags{};
};
// SpidyTieSample. armed: the gesture is ready now (the hands together on the
// right webs). made: ties made; ended, taken, gone, down: ties the end of a
// fight let go of, a hand's web took over, whose target was gone, whose
// target stayed down tieDownSeconds after he arrived without being stuck. knocks: blows
// that knocked a tied enemy into the game's flight. stuck: ties that ended
// with the game sticking him to a surface (BotStateWallStuck); webs: the
// tie's webbing hits as he arrived.
// hangs: thugs hung under web lines, each its strand (from the line to his
// head; `distance` the line's id, as a float's bits; `flags` his record's
// bits 4..35, a stable id for the strand's rope).
// Version 2 (October 31): the counters of how ties ended and the lines'
// distances and flags. Version 3 (November 2): stuck and webs. Version 4
// (November 3): hangs. Version 5: sixteen of them (six: the seventh hung had
// no strand).
struct TieData {
    uint32_t magic = 0x53544945, version = 5, bytes = sizeof(TieData), status{};
    int64_t sequence{};
    uint64_t made{}, ended{}, taken{}, gone{}, down{}, knocks{}, stuck{}, webs{};
    uint32_t count{}, armed{};
    TieLine lines[8]{};
    uint32_t hangCount{}, reserved{};
    TieLine hangs[16]{};
};
static_assert(sizeof(TieLine) == 32 && sizeof(TieData) == 872);
// Whether the bot this record names is an enemy the game has webbed up
// (game_targets::cocooned), or Spidy's web balls lately did (game_shooter::webbedUp),
// or was seen so within cocoonMemory and is still up: a yank flings him out
// of his cocoon's state into the game's flight, and he is still webbed
// (November 2, 13:08: 20 yanks, no tie of a thug in the air - "you should
// be able to pull one and tie him to a wall in the air").
constexpr float cocoonMemory = 15;
bool cocooned(uint64_t record);
// The bot this hand's web holds now, or held within heldGrace: a yank ends
// its grab as the thug comes to the hand, and the tie's gesture comes after.
// 0 for none.
constexpr float heldGrace = 2;
uint64_t heldLately(unsigned hand);
// Ties the enemy this hand's web holds to the surface at `anchor` (its normal
// `normal`, out of it): the hand's web is handed over to the tie. False when
// it holds no bot.
// A tie to a web line (web_line.hpp) when `line` is its id: `lineSurface` the
// body at one of its ends (0: none found), `lineAlong` its direction. He
// hangs under the line in his cocoon, lineHang below it (his centre), on a
// strand of web from it (TieData::hangs), the game's stick asked there on that
// body (the line has none of its own) as on a wall facing the player.
constexpr float lineHang = 1.8f;
bool tie(unsigned hand, Vec3 anchor, Vec3 normal, uint32_t lineSurface = 0, Vec3 lineAlong = {},
         uint32_t line = 0);
// The web lines there are now: a thug hung from one that is gone falls.
void keepLines(const uint32_t* ids, uint32_t count);
// The gesture is ready now (TieGesture::armed), for the telemetry.
void tieArmed(bool);
// WEB TIE (vr_settings::Values::webTie): ties are made, and an enemy on a web
// comes only on the yank (GrabConfig::yankOnlyCharacters). Off (as at a
// start): no tie, an enemy comes as before, and the ties there are let go,
// as at a fight's end.
void allowTies(bool on);
TieData tieData();
// The kinds this build can move.
constexpr uint32_t movableKinds = 1u << static_cast<unsigned>(game_targets::Kind::throwable) |
                                  1u << static_cast<unsigned>(game_targets::Kind::bot);
} // namespace spidy::game_grab
