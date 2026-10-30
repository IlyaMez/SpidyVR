// Bounded first-person OpenXR integration using the game's two native views.
#include "spidy/native_appearance.hpp"
#include "spidy/native_body.hpp"
#include "spidy/wall_crawl_body.hpp"
#include "spidy/wall_run_body.hpp"
// The input bridge remains a separately verified, separately stoppable module.
#include "spidy/bhaptics_output.hpp"
#include "spidy/body_calibration.hpp"
#include "spidy/body_haptics.hpp"
#include "spidy/crawl_hands.hpp"
#include "spidy/crawl_stick.hpp"
#include "spidy/eye_resolution.hpp"
#include "spidy/monitor_rate.hpp"
#include "spidy/eye_snapshot.hpp"
#include "spidy/game_bridge_protocol.hpp"
#include "spidy/game_dodge.hpp"
#include "spidy/game_grab.hpp"
#include "spidy/game_menu.hpp"
#include "spidy/game_offers.hpp"
#include "spidy/game_pad.hpp"
#include "spidy/game_prompts.hpp"
#include "spidy/game_player.hpp"
#include "spidy/game_punch.hpp"
#include "spidy/game_sense.hpp"
#include "spidy/gadget_wheel.hpp"
#include "spidy/phone.hpp"
#include "spidy/phone_music.hpp"
#include "spidy/game_shooter.hpp"
#include "spidy/game_sound.hpp"
#include "spidy/game_steam.hpp"
#include "spidy/game_swing.hpp"
#include "spidy/game_time.hpp"
#include "spidy/game_tracking.hpp"
#include "spidy/game_warp.hpp"
#include "spidy/hand_pose.hpp"
#include "spidy/impact_sound.hpp"
#include "spidy/native_eye_frame.hpp"
#include "spidy/native_eye_gpu.hpp"
#include "spidy/native_eye_history.hpp"
#include "spidy/native_hud.hpp"
#include "spidy/native_webs.hpp"
#include "spidy/near_clip.hpp"
#include "spidy/offer_pointing.hpp"
#include "spidy/presentation_gate.hpp"
#include "spidy/recording_fov.hpp"
#include "spidy/seated.hpp"
#include "spidy/slow_motion.hpp"
#include "spidy/throw_aim.hpp"
#include "spidy/vr_settings.hpp"
#include "spidy/web_pull.hpp"
#include "spidy/xr_layers.hpp"
#include "spidy/xr_session.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <mmsystem.h>
#include <shlobj.h>
using namespace spidy;
struct XrConfig {
    uint32_t magic = 0x53585243, version = 24, bytes = sizeof(XrConfig), pid{};
    // record and mover are no longer used: VR starts with the game, before
    // there is a player, and finds each new player itself (game_player).
    uint64_t base{}, queue{}, bridgeModule{}, rayModule{}, motionModule{}, record{}, mover{};
    uint32_t durationMs = 20000, eyeSize = defaultEyeSize;
    float swingSpeed = 32;
    // bit 0: CPU eye capture; bit 1: Spidy's overlay webs instead of the game's;
    // bit 2: keep the stock camera as the game's active (monitor) view in VR;
    // bit 3: no web grab (webs never catch props or thugs);
    // bit 4: no eye occlusion (each eye draws everything in its view, hidden or not);
    // bit 5: no body (the hero stays hidden in VR, the overlay draws gloves);
    // bit 6: no punching (fists pass through thugs);
    // bit 7: aim markers start hidden (the SPIDY VR tab shows them);
    // bit 8: no webs in open air (a web that meets nothing within reach misses);
    // bit 9: no web shooter (a free hand's trigger shoots nothing);
    // bit 10: no T-pose calibration at the first gameplay of a session without
    // one (the player skipped it before; the SPIDY VR tab still offers it);
    // bit 11: flips, experimental (A and the left stick in the air flip the player; FlipMotion);
    // bit 12: the trigger webs and the grip reels (WEB BUTTON: TRIGGER);
    // bit 13: the view stays upright on walls (STAND ON WALLS: OFF);
    // bit 14: walls are the game's (its wall crawl takes a player who flies
    // into one, as before the swing's own walls);
    // bit 15: no anti-aliasing for the eyes (each eye is its scene as rendered,
    // at the eye's size, as before October 10's eighth build: upscale is not used);
    // bit 16: the game renders its own view on the head in immersive VR too,
    // for the monitor (as before October 10's eighth build; without it the monitor shows the
    // left eye);
    // bit 17: each eye makes its own set of the key light's shadow cascades,
    // as before October 11 (without it both use one set, fitted to a lens
    // covering both: SpidyEyeShadows);
    // bit 18: no targets for screen-space reflections in the eyes;
    // bit 19: the eyes keep the distances their own wide lenses give
    // (--eye-lens-distances; until October 15 it was webs as before October
    // 12: PHYSICAL WEBS off);
    // bit 20: the game's own button prompts are left alone (neither read
    // for the button they ask for nor named with the VR control, and the
    // interact button is the game's Y as before; the pad layer stays);
    // bit 21: arcade webs (WEB STYLE: ARCADE; vr_settings::Values::arcadeWebs)
    uint32_t options{};
    // OpenXR runtime manifest the launcher chose; empty: Virtual Desktop's if
    // installed, else Windows' active runtime.
    wchar_t runtime[260]{};
    // The rest of the VR settings a session starts from (options bits 3, 5-9
    // and 11-13, swingSpeed, weight, hud and flipSpeed give the others):
    // degrees per snap turn (0: none), controller vibration in percent, the
    // game screen's size (0-2), degrees a second of smooth turning (0: the
    // stick snap turns).
    uint32_t snapTurn = 30, haptics = 100, screenSize = 1, smoothTurn{};
    // Eye resolution in percent of the runtime's recommendation, per side
    // (eye_resolution.hpp; with eyeSize set, 100), and the player's weight
    // while webs fly them, in percent of real gravity (40-300).
    uint32_t renderScale = 100, weight = 100;
    // The player's T-pose calibration (body_calibration.hpp): the eye height
    // and arm length their body is sized to, millimetres. Both 0: none yet.
    uint32_t eyeHeightMm{}, armLengthMm{};
    // The game's HUD in the headset: 0 off, 1 small, 2 medium, 3 large
    // (native_hud.hpp); how fast a flip turns the player at full tilt of the
    // left stick, degrees a second (90-480; FlipMotion::speed).
    uint32_t hud = 2, flipSpeed = 180;
    // Percent of the eye's size, per side, the eyes' scene is rendered at
    // (50-100; eye_resolution.hpp): the engine's temporal anti-aliasing brings
    // it to the eye's size. And how often the game's window gets the left eye
    // while the game renders no view of its own: presents a second (1-240), 0
    // every frame (monitor_rate.hpp).
    uint32_t upscale = defaultEyeUpscale, monitorRate = defaultMonitorRate;
    // Version 23: how much depth scenes have on the game screen: 0 none (the
    // game's flat frame), 1 low, 2 medium, 3 high (vr_settings::screenDepths).
    // Version 24, in version 23's spare word: how strongly a bHaptics vest,
    // sleeves and visor play (BODY HAPTICS, percent; 0 off: body_haptics.hpp).
    uint32_t screenDepth = 2, bodyHaptics = 100;
};
static_assert(sizeof(XrConfig) == 664);
// Why the last frame had no gameplay (XrData::gate bits).
enum GateReason : uint32_t {
    gateNoPlayer = 1,     // no save loaded, or a level change in progress
    gateBridge = 2,       // the input bridge is not running
    gateNoCommit = 4,     // no camera commit for 100 ms: paused, loading, a cutscene
    gateOtherCamera = 8,  // the camera is not the player's follow or combat camera
    gateTracking = 16,    // the headset's pose or timing was not usable
};
struct XrData {
    uint32_t magic = 0x53585244, version = 43, bytes = sizeof(XrData), status{};
    int64_t sequence{};
    uint64_t frames{}, tracked{}, submitted{}, dropped{}, leftHands{}, rightHands{}, serial{}, generation{};
    uint32_t nativeKeys{}, error{};
    float head[16]{}, hands[2][16]{};
    char message[256]{};
    uint64_t uniqueSubmitted{}, reusedSubmitted{};
    uint32_t flatScreen{}, toggles{}, eyeWidth{}, eyeHeight{};
    // What the last frame showed (Presentation); B presses that reached the
    // game as its B (the dodge; 0 from builds before it, where this was
    // reserved); and frames that showed the game's presented frame on the
    // screen because gameplay was unavailable.
    uint32_t presentation{}, dodges{};
    uint64_t screenSubmitted{};
    // GateReason bits of the last frame, and the vtable (image offset) of the
    // camera mover that last committed: which camera a scene without VR used.
    uint32_t gate{}, cameraMover{};
    // Players found so far (each loaded save, respawn or character switch),
    // and the current one's actor record.
    uint64_t players{}, playerRecord{};
    // Xbox buttons the VR controllers hold for the game, and the game's reads
    // of that virtual controller.
    uint32_t padButtons{}, padInstalled{};
    uint64_t padReads{};
    // The input bridge's camera commits, and those on the player by an
    // accepted camera. Both rising while the gate says other_camera: two
    // cameras commit each frame and the other one commits last.
    uint64_t cameraCommits{}, playerCommits{};
    // Y presses that reached the game as its Y (interact, web strike; B
    // presses until October 11); whether the aim markers show now; markers
    // drawn so far, one per hand per headset frame that shows one.
    uint32_t interacts{}, aimMarkers{};
    uint64_t markers{};
    // The VR settings now: the SPIDY VR tab in the game's Settings changes
    // them during play. settings bits: 1 web grab,
    // 2 punch, 4 body, 8 webs in open air, 16 web shooter, 32 flips, 64 the
    // trigger webs (WEB BUTTON: TRIGGER; the grip reels), 128 STAND ON WALLS,
    // 256 PHYSICAL WEBS (until October 15), 512 arcade webs (WEB STYLE:
    // ARCADE).
    // Then the changes
    // made in that tab so far, the times the game built its Settings with it,
    // whether its hooks are in, and why it is missing (game_menu: 93xx-94xx
    // not hooked, 95xx not built).
    uint32_t settings{}, snapTurn{}, haptics{}, screenSize{};
    float swingSpeed{};
    uint32_t settingChanges{};
    uint64_t menuTabs{};
    uint32_t menuInstalled{}, menuStatus{};
    // Degrees a second of smooth turning now (0: the stick snap turns).
    uint32_t smoothTurn{};
    // Walls and ceilings the game held the player on (GameTrackingRig): the
    // stretches and frames of play on one, the player's up (its actor's)
    // now, the head's stand-off now (m), and on the last frame on one the
    // head's height over it from the feet, before and after the stand-off.
    uint32_t surfaceEntries{};
    uint64_t surfaceFrames{};
    float heroUp[3]{};
    float standOff{}, surfaceHeight{}, surfaceClearance{};
    // The weight now, percent of real gravity (the swing's gravity while webs
    // fly the player), and the HUD row now (0 off, 1-3 small to large).
    uint32_t weight{}, hud{};
    // The T-pose calibration (body_calibration.hpp): its phase now and what its
    // panel asks for (Phase, Hint), how far the hold is (0-1); the eye height
    // and arm length the body is sized to (mm; 0: the headset's running height
    // and the hero's own arms); calibrations finished and skipped this session;
    // calibrationFlags bit 0: a session without one asks for none at its first
    // gameplay (skipped); each arm's reach at the last one (m, 0 the left).
    uint32_t calibrationPhase{}, calibrationHint{};
    float calibrationProgress{};
    uint32_t eyeHeightMm{}, armLengthMm{}, calibrations{}, calibrationSkips{}, calibrationFlags{};
    float calibrationReach[2]{};
    // The flip speed now, degrees a second at full tilt (FLIP SPEED), and how
    // far the view leans from the world's up now, degrees: 0 level, 90 while
    // the player stands on a wall (GameMotionFrame::viewTilt).
    uint32_t flipSpeed{};
    float viewTilt{};
    // Version 21: the walks a held Y began (game_pad::InteractStep). Version
    // 22: the GAME SCREEN 3D row now (0 off, 1-3 low to high), and the frames
    // that showed the game screen in stereo (a scene's own two eye images;
    // screenSubmitted counts them too).
    uint32_t interactSteps{}, screenDepth{};
    uint64_t stereoSubmitted{};
    // Version 23: pushes of the right stick that reached the game as its
    // D-pad down (heal) and as its LT + RT (the zip to a point).
    uint32_t heals{}, zips{};
    // Version 24: the game's own camera against where the head looks
    // (game_pad::CameraLook): the angle between them now, degrees (the head to
    // the camera's right: more than 0; 0 while the camera is not steered), and
    // the times the right stick began to turn the camera.
    float lookError{};
    uint32_t lookTurns{};
    // Version 25: the scans a trigger pulled at the temple began (the game's
    // R3: game_pad::TemplePress), and the Charge Jumps a held A began (its
    // RT + A: game_pad::ChargeJump).
    uint32_t scans{}, chargeJumps{};
    // Version 26: the layout the runtime uses for each hand's controller now
    // (ControllerKind, controller_layout.hpp; left, right: 0 none yet,
    // 1 Touch, 2 Index, 3 Steam Frame, 4 one of the runtime's own), the
    // layouts the runtime took Spidy's controls for (bit 1 << ControllerKind)
    // and a spare word. A Steam Frame whose hands say Touch plays SteamVR's
    // translation of that layout: its B, X and Y are all B. Version 38 in
    // that spare word: the zips sent as a hold of the stick was let go in the
    // game's aim mode (game_pad::ZipPress; `zips` counts them too).
    uint32_t controllers[2]{}, controllerLayouts{}, zipReleases{};
    // Version 27: the game's own prompts, read through game_menu's hooks
    // (game_prompts.hpp). Its calls into Flash and the prompt and QTE
    // interface's among them (`promptCalls` at 0 with `promptInvokes` rising
    // says the names no longer match the game), prompts raised and taken
    // away, texts that named a button and texts rewritten for a VR player;
    // what the game is asking for now and the last thing it asked for
    // (prompt_buttons::pack), the asks VR play could not have sent on its own
    // (what the interact button now carries) and texts that came with the
    // keyboard's icons, which name no pad button at all.
    uint64_t promptInvokes{}, promptCalls{}, promptsRaised{}, promptsCleared{};
    uint64_t promptTexts{}, promptRewrites{};
    uint32_t promptDemand{}, promptLastDemand{}, promptBeyondPlay{}, promptKeyboardTexts{};
    // Presses that went to a button the game named, from the VR control of
    // that name or a thrown fist (game_pad::NamedPress, since October 20;
    // before it the interact button sending a prompt's demand, which never
    // ran with the reading off), the times the pad layer was latched on and
    // whether it is on now (game_pad::PadLayer), and a spare word.
    // Then the presses of Y that went out while the player stood on a wall,
    // which `playing` withheld until October 13 (the closed vent).
    uint32_t contextPresses{}, padEntries{}, padLayerNow{}, wallInteracts{};
    // Version 28: the VR controllers' own buttons as the runtime gives them
    // (ControllerButton bits: 1 A, 2 B, 4 X, 8 Y, 16 menu, 32 the Steam
    // Frame's View, 64-512 its D-pad) - held on the last frame, and every bit
    // that went down at all in VR play. A report then says what the player
    // pressed as well as what reached the game, which is what two October 13
    // sessions could not tell apart: the game's Y never reached it in either
    // (0 of 4230 and 0 of 3432 samples) while its B, from the other hand,
    // reached it 30 times, and nothing said whether Y had been pressed.
    uint32_t vrButtons{}, vrButtonsPlayed{};
    // Version 29: presses of the interact that handed the player back to the
    // game's own wall crawl, because the swing held him on a wall of its own
    // and the game was offering nothing to press (game_pad::WallHandover).
    // Then those of them the game answered by offering an interact again,
    // which is the hand-over having worked: the two apart say whether it is
    // worth its moment.
    uint32_t wallHandovers{}, wallHandoversOffered{};
    // Version 37 in version 30's slots (the room around the player, which
    // decided nothing since October 15): the eyes held out of what the world
    // is built of (HeadClearance) - frames they were moved more than a
    // centimetre, and the most they ever were, millimetres.
    uint32_t headPushFrames{}, headPushMostMm{};
    // Version 31: the game's own wall crawl, now that the view turns onto it
    // as it does onto a wall of the swing's (SurfaceHold::crawl). Frames the
    // crawl held the player, and of those the frames the view had turned.
    //
    // Then what the game did with the stick there (CrawlStick): the mean turn
    // from the direction sent to the direction taken, in the surface's own
    // axes, and how alike those turns were - near 1 a frame fixed against the
    // surface, which the stick wants rotating by that turn; near 0 no fixed
    // frame at all, and the mover wants driving instead, as the swing drives
    // it on a wall of its own. Then the mean speed taken. The crawl is the
    // only state the game calls supported on a wall, so it is the only one
    // whose interacts work there: what it does with the stick is the last
    // thing in the way of a vent that behaves like the ground.
    uint32_t crawlFrames{}, crawlStood{}, crawlStickSamples{};
    float crawlStickTurn{}, crawlStickAgreement{}, crawlStickSpeed{};
    // Frames the head was brought down to keep it under the surface over the
    // player, and the least room there ever was over his feet (metres; zero
    // where no ray ever found one). Inside the vent the head stood at the
    // player's own eye height over its floor and so out through its roof.
    uint32_t headDroppedFrames{};
    float headroomLeast{};
    // Version 32: handing the player from the swing's own wall to the game's,
    // indoors (WallToGame). Tries, those the game answered by taking him -
    // which is a vent that works - those it did not and the swing took the
    // wall straight back, and the frames he was being driven into the wall.
    // The three apart say whether the game catches a player let go of at a
    // wall, which nothing could rig headlessly.
    uint32_t wallToGameTries{}, wallToGameHanded{}, wallToGameRecovered{}, wallToGamePushFrames{};
    // Version 33: the game's own crawl under the player at rest on the
    // swing's wall (game_warp): crawls asked for, those the game's state
    // machine took, refused, faulted; how the hand-overs ended - the crawl
    // took him, or the swing had him back (the game did not begin or finish,
    // or the body dropped). Then frames of the game's own wall run (its RT
    // with the stick up the wall, `climbing`) and frames the view held its
    // surface through a gap in the swing's reports (`holdGrace`).
    uint32_t crawlAsked{}, crawlApplied{}, crawlRefused{}, crawlFaulted{};
    uint32_t crawlTaken{}, crawlDeclined{}, gameRunFrames{}, holdGraceFrames{};
    // Version 34: frames of play the rig could not place (the gate's
    // `tracking`), and every reason seen for them (GameTrackingRig::Inactive
    // bits); one shorter than blipSeconds keeps the view standing where it
    // stood. Then frames the game crawled the player on a floor (a vent, a
    // duct: game_swing::Surface::floor), of those the frames the head was
    // held at the crawl's own height (crawlRoom), and frames the body knelt
    // under a held head. Then the game camera's look and up at the latest
    // frame: the frame the game's crawl takes its stick in.
    uint32_t trackingGapFrames{}, trackingGapReasons{}, floorCrawlFrames{}, crawlCapFrames{};
    // Version 35 in the slot after: the game_offers hooks (game_offers::Data
    // status - 0 off, 1 hooked, 3 failed - and its error above the low byte).
    uint32_t kneelFrames{}, offersState{};
    float cameraForward[3]{}, cameraUp[3]{};
    // Version 35: the game's interactions on Spidy's wall (game_offers,
    // offer_pointing): frames a hand pointed at one the game would let the
    // player use, and at one he had to come closer to; presses of the
    // interact that asked for a use, those the game refused at once, those
    // that came to the game's interact and of those the ones the game had
    // picked the offer for first, and those given up (its crawl did not take
    // him); the most offers the game gathered near him at once.
    uint32_t offerPointFrames{}, offerReachFrames{}, offerAsks{}, offerRefusals{};
    uint32_t offerPresses{}, offerPicked{}, offerFailures{}, offersSeen{};
    // Version 36: game_offers' own counts at the latest frame - the player's
    // offer updates run with the crawl's mask, the offers checked against the
    // crawl's pose, the masked updates whose gather checked any offer and the
    // most it checked at once, the spot searches for an aimed offer out of
    // reach and those that found one, and the offers near the player now.
    uint32_t offersMasked{}, offersPosed{}, offersGathered{}, offersMostChecked{};
    // And the presses made again because the game still held the offer.
    uint32_t offersSearches{}, offersSpots{}, offersNear{}, offerRetries{};
    // Version 39: Spider-Sense in the headset (spider_sense.hpp). Warnings
    // that showed (an attacker's mark from its start), headset frames that
    // drew a mark over an attacker and frames that drew one in the sightline
    // for an attacker out of view, tingles sent to the controllers, marks a
    // counter broke, and the game's dodge (B) pressed while a warning stood.
    uint32_t senseShown{}, senseFrames{}, senseSightFrames{}, senseTingles{}, senseBroken{}, senseDodges{};
    // Version 40: suit powers asked for with both hands at the chest
    // (game_pad::SuitPowerPress), dodges made by moving (MoveDodge), dives
    // (WebStrike::dive) and the blows whose sound the headset played itself,
    // the game's own not being there (game_sound.hpp).
    uint32_t suitPowers{}, moveDodges{}, dives{}, ownSounds{};
    // Version 41: seated play (seated.hpp): SEATED on (1) and a seated eye
    // height measured (2); HEIGHT, centimetres (0 AUTO); the seated eye height
    // last measured and how far the tracking space is raised now, millimetres;
    // and how many seated eye heights were measured.
    uint32_t seated{}, heightCm{}, seatedEyeMm{}, seatedLiftMm{}, seatedMeasures{}, reserved41{};
    // Version 42: the phone drawn from a back pocket (PocketDraw::Stats):
    // grip presses that may have been for it, draws begun, phones out, draws
    // too slow and let go of on the way, and presses that began none, by
    // PocketDraw::Miss. Then the latest miss: its Miss + 1 (0 none yet), the hand
    // from the neck in the body's level axes (millimetres: x right, y up, z
    // ahead), how long the headset had lost it (milliseconds) and how far the
    // head looked from the body's way (tenths of a degree, left positive).
    uint32_t pocketPresses{}, pocketBegun{}, pocketDraws{}, pocketSlow{}, pocketLetGo{}, pocketMissed[6]{};
    uint32_t pocketLastMiss{};
    int32_t pocketLastHand[3]{};
    uint32_t pocketLastLostMs{};
    int32_t pocketLastLook{}, reserved42{};
    // Version 43: EYE AIM. eyeGaze: the headset's eye tracking + 1 (0 not
    // known yet; 1 the runtime offers none, 2 it does but not for this
    // headset, 3 an eye tracker: XrRuntime::EyeTracking) and the EYE AIM row
    // in bits 8-9 (1 web balls, 2 zips). Then frames of play with EYE AIM on
    // and of those the frames the eyes were tracked; web balls the eyes aimed;
    // zips the eyes aimed that went out with the game's camera on their point,
    // and those that went at EyeZip::waitSeconds without.
    uint32_t eyeGaze{}, eyeAimFrames{}, gazeFrames{}, eyeShots{}, eyeZips{}, eyeZipsLate{};
};
static_assert(sizeof(XrData) == 1312);
static_assert(PocketDraw::misses == 6);
// The game's own prompt and QTE calls, for one play session to say what their
// arguments hold (game_prompts.hpp). The eight names matched the game on
// October 13 - 107 of 630 calls into Flash in a 224 s session - but no prompt
// text reached the localized lookup (`texts` 0), so the text is not where a
// session can read what the game is asking for, and these arguments are.
struct PromptData {
    uint32_t magic = 0x53505244, version = 4, bytes = sizeof(PromptData), status{};
    int64_t sequence{};
    uint64_t invokes{}, calls{}, raised{}, cleared{}, texts{}, rewrites{};
    uint32_t demand{}, lastDemand{}, beyondPlay{}, keyboardTexts{};
    // The newest of those calls, one a line with its arguments, oldest first;
    // how many lines are filled; and how often each of the eight names came,
    // in the order of game_prompts.cpp's table (ShowPlaceablePrompt,
    // HidePlaceablePrompt, SetPlaceablePromptProgress, UpdateInWorldPrompt,
    // POIInteractAvailable, AddQTE, UpdateQTE, ClearQTE, ShowText, HideText).
    uint32_t lines{}, reserved{}, perName[game_prompts::callNames]{};
    char line[game_prompts::callLines][game_prompts::callChars]{};
    // Then the census: every distinct name the game called, in the order
    // first seen, how often each came, and one call of each with its
    // arguments. This is what names the call a prompt's text and its button
    // arrive in, without anyone having to guess which it is.
    uint32_t names{}, spare{}, censusCalls[game_prompts::censusNames]{};
    char example[game_prompts::censusNames][game_prompts::censusChars]{};
};
static_assert(sizeof(PromptData) == 12112);
// Slow motion (slow_motion.hpp, game_time.hpp): what the player did with it
// and what the game's time did. status: 1 before VR starts, then 0 with the
// game's time hooked, else why it is not (game_time::install) and slow
// motion does nothing.
struct SlowMotionData {
    uint32_t magic = 0x574f4c53, version = 2, bytes = sizeof(SlowMotionData), status = 1;
    int64_t sequence{};
    // Presses that started it, ended it, or came with too little focus; the
    // times focus ran out and play stopped it (a menu, the game screen, the
    // flat screen); whether it is on now.
    uint32_t starts{}, stops{}, refused{}, emptied{}, interrupted{}, active{};
    // Real seconds in it so far; focus (0-1) and how far into it (0-1) now;
    // the time scale asked of the game.
    float seconds{}, focus = 1, blend{}, wanted = 1;
    // The game's side in the last update of its time system: its own time
    // scale, its world's (the smaller of that and Spidy's), Havok's step (s)
    // and whether the game scales physics itself; its updates so far, and
    // those Spidy slowed.
    float gameScale = 1, worldScale = 1, physicsStep{};
    uint32_t physicsScaled{};
    uint64_t updates{}, slowedUpdates{};
    // Version 2, moments (slow_motion.hpp's SlowMoments): those begun so far
    // by kind (a heavy blow, a called blow, a thug pulled in the air, a
    // perfect dodge), whether one is on now, and the time scale it asks for.
    uint32_t moments[4]{};
    uint32_t momentActive{};
    float momentScale = 1;
};
static_assert(sizeof(SlowMotionData) == 120);
// Body haptics on bHaptics devices (body_haptics.hpp, bhaptics_output.hpp):
// what the bHaptics Player and its devices did. status: bhaptics::Status (0
// off: BODY HAPTICS has not been on; 4 no_player: no bHaptics Player installed,
// as for most players). level: BODY HAPTICS now, percent. Devices on now and
// at any time (bit 1 << body_haptics::Device: 1 the vest, 2 and 4 the left and
// right sleeve, 8 the visor) and the times the Player was connected. Events
// posted, frames sent to a device and those the SDK did not send. Blows the
// player took (a fall in his health) and of them those placed by the attacker
// the game had warned of, and landings. Version 2: ticks of VR play with the
// Player connected (25 a second), and of them each device's with something
// playing and the sum of its strongest motor over those (percent) - how much
// of play a device was on, and how strongly. Then the events by Kind (posted,
// whether their group played them or not), and the groups the tab's switches
// leave on (bit 1 << body_haptics::Group: 1 webs, 2 wind, 4 combat, 8 movement).
struct HapticsData {
    uint32_t magic = 0x53485044, version = 2, bytes = sizeof(HapticsData), status{};
    int64_t sequence{};
    uint32_t level{}, devices{}, devicesSeen{}, connects{};
    uint64_t events{}, frames{}, refused{};
    uint32_t hits{}, aimedHits{}, landings{}, liveTicks{};
    uint32_t onTicks[body_haptics::deviceCount]{}, peakSum[body_haptics::deviceCount]{};
    uint32_t kinds[body_haptics::kindCount]{};
    uint32_t groups{};
};
static_assert(sizeof(HapticsData) == 116 + 4 * body_haptics::kindCount && body_haptics::kindCount == 35);
extern "C" {
__declspec(dllexport) XrData SpidyXrData;
__declspec(dllexport) PromptData SpidyPromptData;
__declspec(dllexport) SlowMotionData SpidySlowMotionData;
__declspec(dllexport) HapticsData SpidyHapticsData;
__declspec(dllexport) XrTimingData SpidyXrTimingData;
__declspec(dllexport) EyeSnapshot SpidyXrSnapshot;
DWORD WINAPI SpidyStart(void*);
DWORD WINAPI SpidyEyeTemporal(void*);
DWORD WINAPI SpidyEyeShadows(void*);
DWORD WINAPI SpidyMonitorRate(void*);
DWORD WINAPI SpidyStop(void*);
DWORD WINAPI SpidySetEyes(void*);
float SpidyFlatAspect();
}
namespace {
using BridgeCall = DWORD(WINAPI*)(void*);
XrConfig config;
BridgeCall submitInput{}, sampleBridge{}, stopBridge{}, retargetBridge{};
BridgeCall startRays{}, submitRays{}, stopRays{};
// The world rays' results, read where they are written: the bridge exports the
// struct itself, and the session is in the same process as it.
const native_rays::Data* rayResults{};
// The game's own blow sounds (game_sound.hpp), read in place; null from an
// older ray module, whose blows sound as the headset makes them.
const game_sound::Data* soundResults{};
// The game's own dodges for a player a web carries (game_dodge.hpp), read in
// place; null from an older ray module, which has none.
const game_dodge::Data* dodgeResults{};
// The tab's BLOW SOUNDS to the ray module (SpidySoundMode; optional).
DWORD(WINAPI* soundMode)(void*){};
BridgeCall startSwing{}, submitSwing{}, sampleSwing{}, stopSwing{}, retargetSwing{}, sampleGrab{}, sampleAim{};
// Each web's edges (game_swing::Bends): optional, without it a web round an
// edge is drawn to that edge.
BridgeCall sampleBends{};
BridgeCall sampleClimb{};
BridgeCall startPunch{}, samplePunch{}, stopPunch{}, swingSettings{}, sampleStrike{}, sampleSlingshot{};
// Spider-Sense (game_sense, in the same module as the punches): optional, an
// older module draws no warnings.
BridgeCall sampleSense{};
// The game's throw aimed by the free hand (game_throw, same module): optional,
// an older module throws the camera's way.
BridgeCall aimGameThrow{};
BridgeCall startShooter{}, sampleShooter{}, stopShooter{}, wheelShooter{};
// The game's own crawl under the player (game_warp, in the same module):
// optional, for the session report.
BridgeCall sampleWarp{};
// The game's interactions on Spidy's wall (game_offers, in the same module):
// what it gathered near the player, and the use of the one he points at.
BridgeCall sampleOffers{}, useOffer{}, aimOffer{};
std::mutex lifecycle, telemetry;
HANDLE worker{};
std::atomic<bool> stopRequested{};
std::atomic<uint64_t> keepAliveDeadline{};
void message(uint32_t status, uint32_t error, const char* text) {
    std::lock_guard lock(telemetry);
    InterlockedIncrement64(&SpidyXrData.sequence);
    SpidyXrData.status = status;
    SpidyXrData.error = error;
    strncpy_s(SpidyXrData.message, text, _TRUNCATE);
    InterlockedIncrement64(&SpidyXrData.sequence);
}
bool read(uintptr_t p, void* out, size_t bytes) {
    __try {
        if (p < 0x10000)
            return false;
        std::memcpy(out, reinterpret_cast<void*>(p), bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void check(DWORD code, const char* operation) {
    if (code)
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(code));
}
// Publishes a completed eye copy, or withdraws the previous one before its
// pixels are released.
void publishSnapshot(EyeSnapshot shot, const D3D12Renderer::Captured* pixels) {
    std::lock_guard lock(telemetry);
    auto& d = SpidyXrSnapshot;
    InterlockedIncrement64(&d.sequence);
    const auto sequence = d.sequence;
    const auto count = d.count + (pixels != nullptr);
    d = shot;
    d.sequence = sequence;
    d.count = count;
    if (pixels) {
        d.pixels = reinterpret_cast<uint64_t>(pixels->pixels);
        d.width = pixels->width;
        d.height = pixels->height;
        d.rowPitch = pixels->rowPitch;
    }
    InterlockedIncrement64(&d.sequence);
}
struct RuntimeSelection {
    std::wstring previous;
    bool present{};
    explicit RuntimeSelection(const wchar_t* requested) {
        const DWORD size = GetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr, 0);
        present = size != 0;
        if (size) {
            previous.resize(size);
            GetEnvironmentVariableW(L"XR_RUNTIME_JSON", previous.data(), size);
            previous.resize(size - 1);
        }
        constexpr auto virtualDesktop =
            L"C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\virtualdesktop-openxr.json";
        const wchar_t* manifest = *requested ? requested : virtualDesktop;
        if (GetFileAttributesW(manifest) == INVALID_FILE_ATTRIBUTES) {
            if (*requested)
                throw std::runtime_error("The chosen OpenXR runtime is not installed");
            return; // no Virtual Desktop: the loader uses Windows' active runtime
        }
        if (!SetEnvironmentVariableW(L"XR_RUNTIME_JSON", manifest))
            throw std::runtime_error("Could not select the OpenXR runtime for this process");
    }
    ~RuntimeSelection() {
        SetEnvironmentVariableW(L"XR_RUNTIME_JSON", present ? previous.c_str() : nullptr);
    }
};
// OpenXR add-ons Spidy cannot run with (xr_layers.hpp), switched off in this
// process while the loader reads them: as it creates the instance. Each by
// the variable its own manifest names; one the player has set stays as it is.
struct LayerSwitches {
    std::vector<std::string> variables;
    // The layers switched off, for the player.
    std::string names;
    explicit LayerSwitches(std::span<const xr_layers::Layer> layers = xr_layers::unsupported) {
        for (const HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
            HKEY key{};
            if (RegOpenKeyExW(root, L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit", 0, KEY_READ, &key) !=
                ERROR_SUCCESS)
                continue;
            // Each value: a manifest's path, and 0 while its layer is on.
            for (DWORD index = 0; index < 256; ++index) {
                wchar_t path[1024];
                DWORD chars = static_cast<DWORD>(std::size(path)), type{}, off{}, bytes = sizeof(off);
                const auto code = RegEnumValueW(key, index, path, &chars, nullptr, &type,
                                                reinterpret_cast<BYTE*>(&off), &bytes);
                if (code == ERROR_NO_MORE_ITEMS)
                    break;
                if (code != ERROR_SUCCESS || type != REG_DWORD || off)
                    continue;
                const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                nullptr, OPEN_EXISTING, 0, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                    continue;
                std::string text(65536, '\0');
                DWORD size{};
                const bool loaded =
                    ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &size, nullptr);
                CloseHandle(file);
                if (!loaded)
                    continue;
                text.resize(size);
                const auto layer = xr_layers::switchOff(text, layers);
                if (layer.variable.empty() || GetEnvironmentVariableA(layer.variable.c_str(), nullptr, 0) ||
                    !SetEnvironmentVariableA(layer.variable.c_str(), "1"))
                    continue;
                variables.push_back(layer.variable);
                names += (names.empty() ? "" : ", ") + std::string(layer.name);
            }
            RegCloseKey(key);
        }
    }
    ~LayerSwitches() {
        for (const auto& variable : variables)
            SetEnvironmentVariableA(variable.c_str(), nullptr);
    }
};
// Where the settings only the SPIDY VR tab offers are kept
// (vr_settings::tabOnlyText): beside the launcher's launcher.ini, so every
// Spidy folder and every build finds them (this module runs from a copy
// staged per build).
std::wstring tabOnlyPath() {
    PWSTR folder{};
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &folder)) && folder)
        path = std::wstring(folder) + L"\\Spidy\\spidy_vr.ini";
    CoTaskMemFree(folder);
    return path;
}
std::string readTabOnly() {
    const auto path = tabOnlyPath();
    if (path.empty())
        return {};
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};
    std::string text(4096, '\0');
    DWORD size{};
    if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &size, nullptr))
        size = 0;
    CloseHandle(file);
    text.resize(size);
    return text;
}
// Written whole and then put in place, so a crash mid-write keeps the last.
bool writeTabOnly(const std::string& text) {
    const auto path = tabOnlyPath();
    if (path.empty())
        return false;
    CreateDirectoryW(path.substr(0, path.find_last_of(L'\\')).c_str(), nullptr);
    const auto written = path + L".new";
    const HANDLE file = CreateFileW(written.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD size{};
    const bool whole = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &size, nullptr) &&
                       size == text.size();
    CloseHandle(file);
    return whole &&
           MoveFileExW(written.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
// REALISTIC FLIPS and the rows under it, as the rig takes them.
RealisticFlipSettings realisticFlips(const vr_settings::Values& v) {
    return {v.realisticFlips,         v.automaticReleaseFlip,  v.automaticStrength,
            v.automaticSpeed,         v.automaticMinimumSpeed, v.automaticResponse,
            v.automaticHandStrength,  v.automaticFalloff,      v.automaticHandThreshold,
            v.automaticAirBrake,      v.automaticCombat};
}
DWORD WINAPI run(void*) {
    bool nativeStarted = false, gpuStarted = false, raysStarted = false, swingStarted = false, websStarted = false,
         bodyStarted = false;
    uint64_t serial = 1, nativeDeadline{};
    // lastPresentedMs: an image was on show. lastNewImageMs: the game delivered a newer one.
    uint64_t firstImageDeadline{}, lastPresentedMs{}, lastNewImageMs{};
    bool presented{}, wasGameplay{};
    // The last frame's predicted display time; held images age against it.
    int64_t displayTime{};
    Presentation shownKind{};
    uint32_t error{};
    std::string failure;
    // VR starts with the game, before there is a player; each loaded save,
    // respawn or character switch brings a new one.
    game_player::Watch players;
    bool padInstalled{};
    try {
        auto queue = reinterpret_cast<ID3D12CommandQueue*>(config.queue);
        ComPtr<ID3D12Device> device;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))))
            throw std::runtime_error("Game graphics device unavailable");
        XrRuntime runtime;
        {
            RuntimeSelection selected(config.runtime);
            LayerSwitches layers;
            if (!layers.names.empty()) {
                const auto text = layers.names + " is switched off for the game: Spidy does not run with it. "
                                                 "It stays on for your other games";
                message(1, 0, text.c_str());
            }
            runtime.initialize(device.Get(), queue, config.eyeSize, config.renderScale);
        }
        D3D12Renderer overlay;
        overlay.initialize(device.Get(), queue);
        // The session report's copies of the left eye live in the overlay's
        // readback buffer; withdraw the last one before the overlay goes.
        struct WithdrawSnapshot {
            ~WithdrawSnapshot() {
                publishSnapshot({}, nullptr);
            }
        } withdrawSnapshot;
        EyeSnapshotSchedule snapshots;
        EyeSnapshot pendingSnapshot{};
        uint64_t snapshotTicket{}, gapGeneration{};
        message(2, 0, "Waiting for the headset");
        players.start(config.base);
        game_player::Player player{};
        uint64_t playerChanges{}, playersFound{};
        // Menus are played with a virtual Xbox controller (game_pad).
        padInstalled = !game_pad::install();
        uint64_t padRetryMs = GetTickCount64() + 1000;
        // The VR settings are a tab of the game's own Settings (game_menu).
        const uint32_t menuCode = game_menu::install(config.base);
        if (menuCode) {
            const auto text = "No SPIDY VR tab in the game's Settings (" + std::to_string(menuCode) +
                              "); the launcher's settings apply";
            message(3, 0, text.c_str());
        }
        // The game's request for a Steam ticket, on its way back to the main
        // menu, closed every VR session that quit to it (game_steam). The
        // guard goes in once the game holds its Steam interface.
        bool steamGuarded{}, steamWarned{};
        uint64_t steamRetryMs{};
        // Slow motion slows the game's own time (game_time).
        const uint32_t timeCode = game_time::install(config.base);
        if (timeCode) {
            const auto text =
                "No slow motion: the game's time could not be hooked (" + std::to_string(timeCode) + ")";
            message(3, 0, text.c_str());
        }
        {
            std::lock_guard lock(telemetry);
            InterlockedIncrement64(&SpidySlowMotionData.sequence);
            SpidySlowMotionData.status = timeCode;
            InterlockedIncrement64(&SpidySlowMotionData.sequence);
        }
        // The last frame showed the game screen; A held from there (Resume,
        // Continue) is not a jump once gameplay is back.
        bool wasScreen{}, jumpFromScreen{};
        // A pressed in the air is not the game's web zip.
        AirJumpFilter airJump;
        // Where B, X and Y went down: on the game screen they are the game's
        // own, in VR B dodges, Y interacts and X opens the gadget wheel
        // (game_pad::PressOrigins).
        game_pad::PressOrigins origins;
        // Y held walks the player up to what they look at: the game takes
        // its Y only within about a metre of the thing.
        game_pad::InteractStep interactStep;
        uint32_t interactStepsBefore{};
        // The game's own camera, turned to where the player looks: the game
        // takes its Y for what that camera has in view.
        game_pad::CameraLook cameraLook;
        // The game's LB + RB (its web pull): both web buttons on a thing no
        // web holds on, then a yank (WebPull); or X where the game shows the
        // two (NamedPress; the right thumbstick's click until October 24).
        // aimed: what each hand's web button would do, markers or not.
        WebPull webPull;
        game_swing::AimData aimed{};
        uint64_t pullsBefore{};
        // The right thumbstick pushed down is the game's D-pad down (heal),
        // pushed up its LT + RT (the zip to a point, a vent).
        game_pad::StickPush stickPush;
        game_pad::ZipPress zipPress;
        game_pad::FlipZipPriority flipZipPriority;
        // EYE AIM: the zip waits for the game's camera to come onto what the
        // eyes look at (EyeZip); frames of play with EYE AIM on and, of those,
        // with the eyes tracked; web balls the eyes aimed; and whether the
        // player was told the headset gives no gaze.
        game_pad::EyeZip eyeZip;
        uint32_t eyeAimFrames{}, gazeFrames{}, eyeShots{};
        bool toldNoGaze{};
        bool interacting{}, dodging{}, healing{}, zipping{};
        // The player asked the game for a move of its own while the swing carried him
        // in the air (game_swing::AirYield), for the next swing command.
        bool askingGame{};
        uint32_t interacts{}, dodges{}, heals{}, zips{};
        // The whole pad, latched by clicking both thumbsticks, for the
        // controls VR play has no room for; and the buttons the game names in
        // a prompt, sent by the VR controls of those names (game_pad.hpp,
        // game_prompts.hpp): what they send this frame, and whether the swing
        // was last told that fists are the game's X meanwhile.
        game_pad::PadLayer padLayer;
        game_pad::NamedPress namedPress;
        game_pad::State namedPad{};
        bool namedFists{}, fistsBlows{};
        uint32_t namedPressesBefore{}, slamsBefore{};
        // The gadget wheel (X held; gadget_wheel.hpp): the player's gadgets
        // as the shooter last listed them, and whether the free hand's trigger
        // fires the equipped one (off: web balls, the game's own Web Shooter's
        // while it is the one equipped: game_shooter.hpp).
        GadgetWheel gadgetWheel;
        game_shooter::Data listed{};
        bool gadgetFire{};
        // The gadgets in the wheel's places, the web shooter's at the top, and
        // the place equipped (the web shooter's while the free hand's trigger
        // fires web balls): the wheel's, and the phone's GEAR in its order.
        const auto wheelPlaces = [&](GadgetWheelView& view) {
            view.places[0] = Gadget::webShooter;
            view.equipped = 0;
            view.bare = true;
            for (uint32_t g = 0; g < std::min(listed.gadgetCount, 8u); ++g) {
                const auto& o = listed.gadgets[g];
                if (!o.kind || o.place >= gadgetPlaces)
                    continue;
                view.places[o.place] = static_cast<Gadget>(o.kind);
                view.ammo[o.place] = o.ammo;
                view.most[o.place] = o.most;
                view.bare = view.bare && o.place == 0;
                if (gadgetFire && o.weapon == listed.equipped)
                    view.equipped = o.place;
            }
        };
        // The phone (phone.hpp): open from a back-pocket draw until B, live
        // while VR shows play; its music plays on with it away, on a thread
        // of its own (phone_music.hpp). phoneShown: what its screen shows,
        // the newest, which every image draws. phoneBlows: the swing was last
        // told the phone has the hands (Settings::noBlows).
        Phone phone;
        PocketDraw pocketDraw;
        PhoneMusic phoneMusic;
        PhoneView phoneShown;
        bool phoneBlows{};
        // The phone's MENU: the game's Back, which opens its menu (the map,
        // suits and skills), sent this long. A tap of X sent it until October
        // 23, when the menu went onto the phone.
        constexpr float menuTapSeconds = .1f;
        float menuTap{};
        // The phone's CAMERA: the game's D-pad up, which takes its camera out
        // (and puts it away again), sent this long.
        constexpr float cameraTapSeconds = .2f;
        float cameraTap{};
        // The game played on its screen (VIEW: GAME SCREEN): Back and the
        // D-pad, which the VR controllers lack, besides the Xbox controller's.
        game_pad::ScreenControls screenControls;
        // Both thumbsticks held in: the game's Start, where the runtime keeps
        // the menu button (SteamVR's dashboard on a Quest's left controller).
        game_pad::PausePress pausePress;
        uint32_t padEntriesBefore{}, wallInteracts{}, pausesBefore{};
        // The headset has given a tracked head pose at least once.
        bool headSeen{};
        // The swing settings exactly as they were last sent, so the interact's
        // hand-over can put the walls back the way the player set them, and
        // never turn them on for a player who asked for them off.
        game_swing::Settings sentSwingSettings;
        bool swingSettingsSent{}, wallsHandedBack{}, toldHandoverRefused{};
        game_pad::WallHandover wallHandover;
        // What the game's own crawl does with the stick Spidy sends it, which
        // is the last unmeasured step between a wall and a vent that behaves
        // like the ground.
        CrawlStick crawlStick;
        uint32_t crawlFrames{}, crawlStood{};
        // The game's own crawl at rest and its wall run (XrData version 33).
        uint32_t gameRunFrames{}, holdGraceFrames{};
        // Version 34: frames of play the rig could not place and why; the
        // game's crawl on a floor, and the head held at its height there
        // (under a duct's roof: FloorCrawlRoof); the body kneeling under a
        // held head.
        uint32_t trackingGapFrames{}, trackingGapReasons{}, floorCrawlFrames{}, crawlCapFrames{};
        uint32_t kneelFrames{};
        FloorCrawlRoof floorRoof;
        CrawlCameraGate crawlCameraGate;
        game_warp::Data warp{};
        // The game's interactions on Spidy's wall (version 35): what the game
        // gathered near the player, what each hand points at, and the
        // interact's use of it.
        game_offers::Data offers{};
        std::array<std::optional<offer_pointing::Pointed>, 2> pointing{};
        std::array<uint32_t, 2> pointedBefore{};
        offer_pointing::Use offerUse;
        uint32_t offerPointFrames{}, offerReachFrames{}, offersSeen{};
        uint32_t offerAsksBefore{}, offerFailuresBefore{}, offerAimed{};
        // The surface over the player's feet, as the last finished ray batch
        // found it: the head stops short of it, so it never goes through the
        // roof of a vent. Zero where nothing is over him.
        float headroomSeen{}, headroomLeast{};
        uint32_t headDroppedFrames{};
        bool pulling_{};
        // The game's throw aimed by the free hand: seconds the aim still goes
        // out after X is let go (the throw may still be reading it).
        float throwAimTail{};
        bool padLayerWas{};
        // The prompt calls are copied out only when one more has come: the
        // lines are 3 kB and nothing changes between calls.
        uint64_t promptCallsBefore = ~0ull;
        uint32_t promptRefresh{}, vrButtonsPlayed{};
        float slingshotPulseSeconds{};
        // A held on the ground charges a leap of the swing's own (the game's
        // Charge Jump is a skill a save may lack), and a trigger pulled with
        // its hand at the temple is the game's R3, its scan. leapOut: the
        // leap let go of, for the swing's next commands.
        game_pad::ChargeJump chargeJump;
        game_pad::TemplePress templePress;
        uint32_t chargesBefore{}, leapsBefore{}, scansBefore{};
        // Both hands at the chest with both triggers pulled: the game's suit
        // power (L3 + R3).
        game_pad::SuitPowerPress suitPress;
        uint32_t suitPowersBefore{};
        float leapOut{};
        // The game's camera follows the head's pitch too: it offers a thing
        // at the player's feet or overhead only with that camera on it.
        game_pad::CameraPitch cameraPitch;
        // Thugs the web brought to the player's hand so far (an air yank's
        // arrival among them): in the air, time slows as one arrives.
        uint64_t catchesBefore{};
        // A dodge the game itself slows time for is its perfect dodge: for a
        // moment after B went down with the game's time at full speed. The
        // side its whoosh starts on (body_haptics' azimuth: + the right).
        float dodgeWatch{}, dodgeFrom{};
        // A dodge by moving (spider_sense.hpp's MoveDodge): the head out of a
        // warned attack's way is the game's B, the way it went.
        MoveDodge moveDodge;
        // A dive (WebStrike::dive): the legs kick out at the one it flies at,
        // from its start until a moment after its blow. And the dives and the
        // blows whose sound the headset played itself, for the report.
        Vec3 diveAt{};
        int64_t kickUntil{};
        uint32_t dives{}, ownSounds{};
        // The VR settings: the launch options, then the SPIDY VR tab in the
        // game's Settings during play.
        vr_settings::Values values;
        values.aimMarkers = !(config.options & 128);
        values.webGrab = !(config.options & 8);
        values.body = !(config.options & 32);
        values.punch = !(config.options & 64);
        values.airWebs = !(config.options & 256);
        values.webShooter = !(config.options & 512);
        values.flips = config.options & 2048;
        values.triggerWebs = config.options & 4096;
        values.standOnWalls = !(config.options & 8192);
        values.arcadeWebs = config.options & 0x200000;
        values.swingSpeed = config.swingSpeed;
        values.snapTurn = static_cast<int>(config.snapTurn);
        values.smoothTurn = static_cast<int>(config.smoothTurn);
        values.haptics = static_cast<int>(config.haptics);
        values.screenSize = static_cast<int>(config.screenSize);
        values.weight = static_cast<int>(config.weight);
        values.hud = static_cast<int>(config.hud);
        values.flipSpeed = static_cast<int>(config.flipSpeed);
        values.screenDepth = static_cast<int>(config.screenDepth);
        values.bodyHaptics = static_cast<int>(config.bodyHaptics);
        // And those only the tab offers, as it left them (tabOnlyPath); all sanitized.
        values = vr_settings::withTabOnly(values, readTabOnly());
        std::string tabOnlySaved = vr_settings::tabOnlyText(values);
        game_menu::publish(values);
        {
            // BODY HAPTICS as the session starts: written before the first frame,
            // so a report never reads the zero of a struct not yet written.
            std::lock_guard lock(telemetry);
            InterlockedIncrement64(&SpidyHapticsData.sequence);
            SpidyHapticsData.level = static_cast<uint32_t>(values.bodyHaptics);
            InterlockedIncrement64(&SpidyHapticsData.sequence);
        }
        uint32_t settingChanges{};
        // aims are each hand's latest preview.
        uint64_t markersDrawn{};
        game_swing::AimData aims{};
        // Each hand's aim marker, steadied from eye image to eye image.
        std::array<AimMarkerMotion, 2> aimMotion;
        GameTrackingRig rig;
        GameMotionFrame motion;
        NativeEyeHistory history;
        // Where the HUD panel faces in the room: it follows the head lazily
        // (native_hud::Follow), and starts in front of it again with play,
        // the immersive view and a recentred room.
        native_hud::Follow hudFollow;
        GameScreen gameScreen;
        // Whether the game is playing a scene on the screen (its time runs),
        // and whether that scene's stereo eyes have the HUD's panel.
        SceneClock sceneClock;
        ScenePanel scenePanel;
        bool flatScreen{};
        Pose screenPose{};
        const auto dimensions = runtime.eyeDimensions();
        // Aim markers are drawn so many pixels wide: above the headset's own
        // resolution they keep the size they have at it.
        const auto recommended = runtime.recommendedDimensions();
        const float markerPixels =
            recommended[0] ? std::max(1.f, static_cast<float>(dimensions[0]) / static_cast<float>(recommended[0])) : 1.f;
        const auto moduleDuration = config.durationMs ? config.durationMs + 3000 : 0;
        {
            std::lock_guard lock(telemetry);
            InterlockedIncrement64(&SpidyXrData.sequence);
            SpidyXrData.eyeWidth = dimensions[0];
            SpidyXrData.eyeHeight = dimensions[1];
            InterlockedIncrement64(&SpidyXrData.sequence);
        }
        game_swing::Data swingState;
        game_swing::Bends swingBends;
        game_swing::ClimbData climbState;
        game_swing::ClimbPulses climbPulses;
        unsigned collisionRayPhase{};
        WallCrawlBody wallCrawlBody;
        WallRunBody wallRunBody;
        bool ventCrawlBefore{};
        // What each hand's web holds, when it caught a thug instead of a wall.
        game_grab::Data grabState;
        uint32_t grabPhases[2]{};
        // A grab's event pulse plays this long before the web's tension
        // takes the hand's haptics again (predicted display time, ns).
        int64_t pulseUntil[2]{};
        WebTimeline webTimes;
        bool attachedBefore[2]{};
        // Which of the game's two ropes draws which hand's web (one web in
        // both hands has one rope), and when a free hand last came onto the
        // other hand's web (a tick says a press there takes hold of it).
        WebRopes webRopes;
        bool onStrandBefore[2]{};
        int64_t strandTickAt[2]{};
        uint64_t zipBefore{};
        // Walls the swing took the player onto, and jumps off them, by the
        // previous frame.
        uint32_t wallsBefore{}, wallJumpsBefore{};
        // Punches each hand had landed by the previous frame.
        bool punchStarted{};
        uint64_t punchesBefore[2]{};
        // Fighting across the room (web_strike.hpp): the blows landed and the
        // web strikes opened by the previous frame; the game's time around a
        // blow; the bursts still showing where the latest blows landed, each
        // since its display time; and the sounds, made once.
        uint64_t blowsBefore{}, strikesBefore{};
        HitStop hitStop;
        // The called blow's arrow over the thug it is on (web_strike.hpp's
        // Cues), since the display time it showed.
        CueView cue{};
        uint64_t cueTarget{};
        uint32_t cueWay{};
        bool cueLocked{};
        int64_t cueSince{};
        // The player's knockouts by the previous frame, and the lettering of
        // the latest, each since its display time.
        uint64_t knockoutsBefore{};
        struct Knockout {
            KnockoutView view;
            int64_t since{};
        };
        std::array<Knockout, 2> knockoutMarks{};
        size_t nextKnockout{};
        struct Burst {
            ImpactView view;
            int64_t since{};
        };
        std::array<Burst, 6> bursts{};
        size_t nextBurst{};
        // Spider-Sense's marks (spider_sense.hpp), one for each attacker the
        // game warned the player of: since the display time it showed, the
        // top of his head, what it asks for, its time left, when a counter
        // broke it, and what the sightline showed for him on the image before.
        // Then the counters made by the previous frame, and what the session
        // report counts.
        struct SenseMark {
            uint64_t attacker{};
            int64_t since{}, brokenAt{};
            Vec3 head{};
            SenseAnswer answer = SenseAnswer::Counter;
            float left = 1;
            bool timed{}, live{};
            SenseSight sight{};
        };
        std::array<SenseMark, 6> senseMarks{};
        uint64_t countersBefore{}, webDodgesBefore = UINT64_MAX;
        uint32_t senseShown{}, senseFrames{}, senseSightFrames{}, senseTingles{};
        uint32_t senseBroken{}, senseDodges{};
        // (They outlive the session: one may still be playing as it ends.)
        static const std::vector<uint8_t> sounds[5] = {
            impactWave(ImpactSound::Light), impactWave(ImpactSound::Firm), impactWave(ImpactSound::Heavy),
            impactWave(ImpactSound::Finisher), impactWave(ImpactSound::Rush)};
        // One at a time, the newest cutting the one before short: a strike's
        // rush ends in its blow.
        auto play = [&](ImpactSound sound) {
            const auto& wave = sounds[static_cast<size_t>(sound)];
            PlaySoundW(reinterpret_cast<LPCWSTR>(wave.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        };
        // Web-shooter shots each hand had fired by the previous frame.
        bool shooterStarted{};
        uint64_t shotsBefore[2]{};
        uint64_t submittedFrames{};
        uint64_t previousPresentedGeneration{};
        // The player's standing eye height above the floor: it rises at once
        // to a higher head and sinks a centimetre a second, so crouching
        // does not shrink the body.
        float standingEye{};
        // Seated play (SEATED, seated.hpp): how far the tracking space is
        // raised from where the player sits, and the seated eye heights
        // measured by the last frame.
        seated::Lift seatLift;
        uint32_t seatMeasuresBefore{};
        // Each hand's fingers (HandPoser): the web-shooting hand as its web
        // button goes down and while the shot is on its way, else a fist,
        // closed by the grip, by a web held or by a hand moving fast relative
        // to the head (a punch); and where each grip was relative to the
        // head. What the swing's frame says of each hand for it: a web ball
        // left it, it holds the other hand's web (taken hold of, not shot),
        // its own web holds (attachedBefore); and an enemy is marked or
        // within a fist's reach.
        HandPoser fingers[2];
        Vec3 gripsBefore[2]{};
        bool gripsSeen[2]{};
        bool ballShot[2]{}, webTaken[2]{}, fightNear{};
        // When enemies were last within a strike's reach or a fist's
        // (performance counter; 0: never): FLIPS IN COMBAT.
        int64_t fightAt{};
        // Stretches and frames of play the game held the player on a wall
        // or a ceiling (the head stood off it).
        uint32_t surfaceEntries{};
        uint64_t surfaceFrames{};
        bool wasOnSurface{};
        // The surface the view last stood the player on, and for how long
        // since no hold has been reported: kept a moment while the game turns
        // the hero onto the wall or off it (`hold` below).
        SurfaceHold lastHold{};
        float holdGapSeconds{};
        // The player's size for the body: their T-pose calibration (the
        // launch's, or one made in this session), else the headset's running
        // height and the hero's own arms. A session with the body and without
        // a calibration asks for one at its first gameplay unless the player
        // skipped it before; the SPIDY VR tab asks again (CALIBRATE BODY: ON
        // RESUME).
        float calibratedEye = static_cast<float>(config.eyeHeightMm) / 1000;
        float calibratedArm = static_cast<float>(config.armLengthMm) / 1000;
        bool calibrationPrompt = !(config.options & 1024);
        // Reading the game's prompts and naming the VR control in them.
        const bool promptHelp = !(config.options & 0x100000);
        bool calibrationWanted = calibrationPrompt && !config.eyeHeightMm && values.body;
        body_calibration::Calibration calibration;
        body_calibration::Measurements lastCalibration{};
        // The panel's place in the tracking space, ahead of where the player
        // faced as it appeared; since when VR has shown play; when the result
        // appeared.
        Pose calibrationPanel{};
        uint64_t immersiveSince{}, calibrationShownAt{};
        // From the calibration's start until each is let go after it, the
        // triggers, grips, A and B do nothing in the game (B skips it).
        bool calibrationInput{}, skipHeld{};
        uint32_t calibrations{}, calibrationSkips{};
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        // Body haptics (body_haptics.hpp): a bHaptics vest, sleeves and visor,
        // played on a thread of their own (bhaptics_output.hpp), started the
        // first time BODY HAPTICS is on. What happens is posted where it
        // happens, beside the controllers' pulses (feel); what goes on, once a
        // frame. A fall in the hero's health is a blow, from where the attack
        // the game last warned of came (blowFrom, since the display time
        // blowSeen); landings come from the swing's samples.
        bhaptics::Output haptics;
        // feel: an event in its own place (a dodge's azimuth names only its
        // side); feelFrom: one that comes from a world direction, placed on the
        // vest by where the torso faces (Facing: it follows the head, the
        // room's turns at once), which the frame loop updates every frame.
        body_haptics::Facing facing;
        body_haptics::Body torso;
        const auto feel = [&](body_haptics::Kind kind, int hand = -1, float strength = 1, float azimuth = 0) {
            haptics.post({kind, static_cast<int8_t>(hand), strength, azimuth, .4f, false});
        };
        const auto feelFrom = [&](body_haptics::Kind kind, int hand, float strength, Vec3 from) {
            const bool placed = finite(from) && length(from) > 1e-3f;
            haptics.post({kind, static_cast<int8_t>(hand), strength, placed ? torso.azimuth(from) : 0.f,
                          placed ? torso.height(from) : .4f, placed});
        };
        // The groups the tab's WEB, WIND, COMBAT and MOVEMENT HAPTICS switches
        // leave on (body_haptics::State::groups).
        const auto hapticGroups = [&] {
            using body_haptics::Group;
            const auto bit = [](bool on, Group g) { return on ? 1u << static_cast<unsigned>(g) : 0u; };
            return bit(values.hapticWebs, Group::webs) | bit(values.hapticWind, Group::wind) |
                   bit(values.hapticCombat, Group::combat) | bit(values.hapticMovement, Group::movement);
        };
        body_haptics::LandingWatch landingWatch;
        body_haptics::HealthWatch heroHealth;
        Vec3 blowFrom{};
        int64_t blowSeen{};
        uint32_t hits{}, aimedHits{}, landings{};
        bhaptics::Status hapticsTold{};
        uint32_t devicesTold{};
        uint64_t hapticsSince{};
        // Slow motion: the left thumbstick's click (StickPress keeps it apart
        // from clicking both), its focus and the game's eased time
        // (SlowMotion). Each step sets the game's time, gives the controllers
        // a pulse at a change and reports it; without headset frames for a
        // while the loop below steps it to its end.
        SlowMotion slowMotion;
        // And the moments it slows by itself (a finisher, a called blow, a
        // thug pulled in the air, a perfect dodge).
        SlowMoments moments;
        StickPress slowPress;
        SlowMotionData slowCounts;
        uint64_t slowSteppedMs = GetTickCount64();
        auto stepSlowMotion = [&](float seconds, bool pressed, bool allowed) {
            using Event = SlowMotion::Event;
            const bool before = slowMotion.active();
            const auto event = slowMotion.update(seconds, pressed, allowed);
            // A landed blow stops the game's time for a moment, in slow motion too.
            hitStop.update(seconds);
            if (!allowed)
                hitStop.reset();
            moments.update(seconds, allowed);
            game_time::slow(std::min({slowMotion.timeScale(), hitStop.timeScale(), moments.timeScale()}));
            slowSteppedMs = GetTickCount64();
            if (before && std::isfinite(seconds))
                slowCounts.seconds += std::clamp(seconds, 0.f, .1f);
            switch (event) {
            case Event::started:
                ++slowCounts.starts;
                runtime.haptic(0, .55f);
                runtime.haptic(1, .55f);
                feel(body_haptics::Kind::slowStart);
                break;
            case Event::stopped:
                ++slowCounts.stops;
                runtime.haptic(0, .3f);
                runtime.haptic(1, .3f);
                feel(body_haptics::Kind::slowStop);
                break;
            case Event::emptied:
                ++slowCounts.emptied;
                runtime.haptic(0, .8f);
                feel(body_haptics::Kind::slowStop);
                break;
            case Event::refused:
                ++slowCounts.refused;
                runtime.haptic(0, .6f);
                break;
            case Event::interrupted:
                ++slowCounts.interrupted;
                break;
            case Event::none:
                break;
            }
            const auto time = game_time::telemetry();
            std::lock_guard lock(telemetry);
            auto& d = SpidySlowMotionData;
            InterlockedIncrement64(&d.sequence);
            d.starts = slowCounts.starts;
            d.stops = slowCounts.stops;
            d.refused = slowCounts.refused;
            d.emptied = slowCounts.emptied;
            d.interrupted = slowCounts.interrupted;
            d.active = slowMotion.active();
            d.seconds = slowCounts.seconds;
            d.focus = slowMotion.focus();
            d.blend = slowMotion.blend();
            d.wanted = slowMotion.timeScale();
            d.gameScale = time.own;
            d.worldScale = time.world;
            d.physicsStep = time.physicsStep;
            d.physicsScaled = time.physicsScaled;
            d.updates = time.updates;
            d.slowedUpdates = time.slowed;
            for (unsigned i = 0; i < std::size(d.moments); ++i)
                d.moments[i] = moments.count(static_cast<Moment>(i));
            d.momentActive = moments.active();
            d.momentScale = moments.timeScale();
            InterlockedIncrement64(&d.sequence);
        };
        // Puts the settings into effect: at once, or when the module they
        // belong to starts (each start reads `values`).
        // What the swing is told during play: the settings, a hand-over's
        // walls, and the hands that land no blow: the phone's while it is out,
        // and both while the game names its X, whose press a thrown fist is
        // then (NamedPress) - a blow of Spidy's on top would hit as well.
        auto sendSwingSettings = [&] {
            // BLOW SOUNDS: the game's own impacts only on GAME.
            if (soundMode)
                soundMode(reinterpret_cast<void*>(static_cast<uintptr_t>(values.blowSounds)));
            if (!swingStarted || !swingSettings)
                return;
            game_swing::Settings s;
            s.grab = values.webGrab && sampleGrab;
            s.maxSpeed = values.swingSpeed;
            s.airWebs = values.airWebs;
            s.gravity = vr_settings::gravity(values.weight);
            s.walls = !(config.options & 16384);
            s.arcade = values.arcadeWebs;
            s.handClimb = values.handClimb;
            s.webSlingshot = values.webSlingshot;
            s.betterClimbing = values.betterClimbing;
            s.twoHandZip=values.twoHandZip;s.ropeSteering=values.ropeSteering;
            sentSwingSettings = s;
            swingSettingsSent = true;
            // A hand-over owns the walls while it lasts; the player's own
            // setting is kept above and goes back when it ends.
            if (wallsHandedBack)
                s.walls = 0;
            phoneBlows = phone.live();
            fistsBlows = namedFists;
            s.noBlows = phoneBlows || fistsBlows ? 3u : 0u;
            if (const auto code = swingSettings(&s)) {
                const auto text = "VR settings: the swing did not take them (" + std::to_string(code) + ")";
                message(3, 0, text.c_str());
            }
        };
        auto applySettings = [&] {
            runtime.hapticStrength(static_cast<float>(values.haptics) / 100);
            // The eyes are read only while something aims with them.
            runtime.wantGaze(values.eyeAim != 0);
            // The bHaptics library loads, on the haptics' own thread, the first
            // time BODY HAPTICS is on; at 0 the devices go quiet.
            if (values.bodyHaptics > 0 && !haptics.started()) {
                bhaptics::Config felt;
                felt.library = bhaptics::libraryBeside();
                felt.appId = bhaptics::builtAppId();
                felt.apiKey = bhaptics::builtApiKey();
                haptics.start(felt);
                hapticsSince = GetTickCount64();
            }
            haptics.level(values.bodyHaptics);
            rig.snapTurn(static_cast<float>(values.snapTurn) * 3.14159265f / 180);
            rig.smoothTurn(static_cast<float>(values.smoothTurn) * 3.14159265f / 180);
            rig.flips(values.flips);
            rig.controlledFlips(values.controlledFlips);
            rig.flipSpeed(static_cast<float>(values.flipSpeed) * 3.14159265f / 180);
            rig.triggerWebs(values.triggerWebs);
            rig.standOnWalls(values.standOnWalls);
            rig.handClimb(values.handClimb);
            rig.wallRunCameraTilt(values.wallRunCameraTilt);
            rig.wallRunCameraDistance(vr_settings::fixedRunCameraDistance * .01f);
            rig.momentumWallRun(vr_settings::fixedMomentumCameraDistance * .01f,
                vr_settings::fixedMomentumCameraTilt * .0174532925f,
                std::tan(vr_settings::fixedMomentumBodyTilt * .0174532925f),
                vr_settings::fixedMomentumForwardLean * .0174532925f);
            rig.wallDistance(static_cast<float>(vr_settings::fixedWallDistance) / 100.f);
            rig.automaticFlips(realisticFlips(values));
            native_hud::setSize(values.hud);
            near_clip::set(values.nearClipMm);
            recording_fov::set(values.recordingFov);
            sendSwingSettings();
            if (swingStarted && startPunch && samplePunch) {
                if (values.punch && !punchStarted) {
                    game_punch::Config punch;
                    punch.pid = config.pid;
                    punch.base = config.base;
                    punchStarted = !startPunch(&punch);
                } else if (!values.punch && punchStarted && stopPunch) {
                    stopPunch(nullptr);
                    punchStarted = false;
                }
            }
            if (swingStarted && startShooter && sampleShooter && stopShooter) {
                if (values.webShooter && !shooterStarted) {
                    game_shooter::Config shooter;
                    shooter.pid = config.pid;
                    shooter.base = config.base;
                    shooterStarted = !startShooter(&shooter);
                } else if (!values.webShooter && shooterStarted) {
                    stopShooter(nullptr);
                    shooterStarted = false;
                }
            }
            // Turned off, the body blends back to the game's pose (Command
            // flags); it is started only once.
            if (nativeStarted && values.body && !bodyStarted)
                bodyStarted = !native_body::start(config.base);
        };
        applySettings();
        auto controls = [&](uint32_t keys) {
            bridge::Control c;
            c.modes = bridge::Mode::input;
            c.serial = ++serial;
            c.leaseMs = 250;
            c.keys = keys;
            check(submitInput(&c), "Controller input");
        };
        // The game replaced its player or has none: every module that acts on
        // the player follows. A player gone again before this reaches the
        // modules is reported by the watch once more.
        auto attach = [&](const game_player::Player& next) {
            const auto follow = [&](const game_player::Player& p) {
                bridge::Config target;
                target.pid = config.pid;
                target.imageBase = config.base;
                target.hero = p.hero;
                target.actorRecord = p.record;
                if (retargetBridge(&target))
                    return false;
                if (!swingStarted)
                    return true;
                game_swing::Config swing;
                swing.pid = config.pid;
                swing.base = config.base;
                swing.record = p.record;
                swing.mover = p.mover;
                return !retargetSwing(&swing);
            };
            player = follow(next) ? next : game_player::Player{};
            if (!player.hero)
                follow({});
            native_appearance::setPlayerRecord(player.record);
            native_body::retarget(player.record);
            if (websStarted)
                native_webs::retarget(player.record);
            playersFound += player.hero != 0;
            // A respawn or another hero: his health is his own.
            heroHealth.reset();
            // The tracking space aligns with the new body's camera again.
            rig.reset();
            history.clear();
        };
        while (!stopRequested && (!nativeDeadline || GetTickCount64() < nativeDeadline)) {
            if (!config.durationMs && GetTickCount64() >= keepAliveDeadline)
                break;
            double copyMs{}, overlayMs{};
            Presentation drawn{};
            // This frame shows the game's presented frame on the screen, or
            // (stereoScene) the scene's own two eye images once there are some.
            bool showScreen{}, stereoScene{};
            if (!steamGuarded && GetTickCount64() >= steamRetryMs) {
                const uint32_t code = game_steam::install(config.base);
                steamGuarded = !code;
                steamRetryMs = GetTickCount64() + 2000;
                if (code && code != 9801 && !steamWarned) {
                    steamWarned = true;
                    const auto text = "Quitting to the game's main menu may close the game: its Steam ticket "
                                      "call could not be guarded (" + std::to_string(code) + ")";
                    message(3, 0, text.c_str());
                }
            }
            if (!padInstalled && GetTickCount64() >= padRetryMs) {
                padInstalled = !game_pad::install();
                padRetryMs = GetTickCount64() + 1000;
            }
            // VR play has eye images; the game played on its screen has the
            // screen's, which these do not count.
            const bool eyesWanted = motion.active && !flatScreen;
            if (eyesWanted && firstImageDeadline && !lastNewImageMs && GetTickCount64() >= firstImageDeadline)
                throw std::runtime_error("No native eye images reached OpenXR within 3 seconds");
            if (eyesWanted && lastNewImageMs && GetTickCount64() - lastNewImageMs >= 3000)
                throw std::runtime_error("Native eye presentation stalled for 3 seconds");
            const bool active = runtime.frameStereo(
                [&](const XrFrame& trackedFrame) {
                    XrFrame frame = trackedFrame;
                    // The phone takes its controls first (Phone::take, PhoneHold):
                    // while it is live the stick picks a button, A presses it, B
                    // puts it away and the triggers step the volume, and none of
                    // its controls reaches the swing or the game.
                    {
                        const auto use = phone.take(frame, phoneShown.gearCount);
                        const auto& e = use.controls;
                        if (e.x || e.y)
                            runtime.haptic(use.stick, .08f);
                        if (e.volume) {
                            phoneMusic.setVolume(phoneMusic.status().volume + e.volume * phoneVolumeStep);
                            runtime.haptic(e.volume < 0 ? 0 : 1, .1f);
                        }
                        switch (use.action) {
                        case PhoneAction::previous:
                            phoneMusic.previous();
                            break;
                        case PhoneAction::play:
                            phoneMusic.toggle();
                            break;
                        case PhoneAction::next:
                            phoneMusic.next();
                            break;
                        case PhoneAction::autoplay:
                            phoneMusic.setAutoplay(!phoneMusic.status().autoplay);
                            break;
                        case PhoneAction::camera:
                            cameraTap = cameraTapSeconds;
                            break;
                        case PhoneAction::menu:
                            menuTap = menuTapSeconds;
                            break;
                        case PhoneAction::none:
                        case PhoneAction::close:
                            break;
                        }
                        if (e.pressed || (e.closed && use.action != PhoneAction::close))
                            runtime.haptic(1, .2f);
                        if (use.action == PhoneAction::close) {
                            runtime.haptic(1, .15f);
                            message(3, 0, "Phone away - each control is the game's again once let go of");
                        } else if (use.action == PhoneAction::camera) {
                            message(3, 0, "The game's camera - left trigger aims, right trigger takes the photo, "
                                          "right stick up puts it away");
                        } else if (use.action == PhoneAction::menu) {
                            message(3, 0, "The game's menu (map, suits, skills) - B closes it");
                        }
                    }
                    uint64_t changes{};
                    if (const auto found = players.current(changes); changes != playerChanges) {
                        playerChanges = changes;
                        attach(found);
                    }
                    bridge::Data game{};
                    check(sampleBridge(&game), "Gameplay sample");
                    LARGE_INTEGER now{};
                    QueryPerformanceCounter(&now);
                    // A fight goes on 3 s past the last enemy in reach, so
                    // the realistic flips that FLIPS IN COMBAT keeps out of
                    // it do not come back between blows.
                    const bool fightLately = fightAt && now.QuadPart - fightAt < frequency.QuadPart * 3;
                    Mat4 body{}, camera{};
                    std::memcpy(body.data(), game.player, 64);
                    std::memcpy(camera.data(), game.before, 64);
                    // Gameplay: the player's own follow or combat camera
                    // committed within 100 ms (game_bridge.cpp).
                    const bool bridgeRunning = game.state == 1;
                    const bool committed = game.qpc && now.QuadPart >= static_cast<int64_t>(game.qpc) &&
                                           static_cast<double>(now.QuadPart - game.qpc) / frequency.QuadPart < .1;
                    const bool followsPlayer = native_view::validPose(body) && native_view::validPose(camera);
                    const bool gameplay = player.hero && bridgeRunning && committed && followsPlayer;
                    uint64_t moverTable{};
                    read(game.mover, &moverTable, sizeof(moverTable));
                    // The headset is worn, focused and tracked: an image can be shown.
                    const bool viewing = frame.focused && frame.valid;
                    displayTime = frame.predictedDisplayTime;
                    // The game as it is, on its screen, instead of VR: the
                    // SPIDY VR tab's VIEW (a click of both thumbsticks until
                    // October 12). Its gameplay is the game screen's, in 3D
                    // as scenes are there, the controllers an Xbox
                    // controller's, and nothing of Spidy's on the hero.
                    if (values.flatScreen != flatScreen && viewing) {
                        flatScreen = values.flatScreen;
                        rig.reset();
                        history.clear();
                        lastPresentedMs = lastNewImageMs = 0;
                        presented = false;
                        firstImageDeadline = GetTickCount64() + 3000;
                        // Chosen in the pause menu, the game's screen is the
                        // menu's own, where it stands.
                        if (!wasScreen)
                            screenPose = screenAhead(frame.head);
                        std::lock_guard lock(telemetry);
                        InterlockedIncrement64(&SpidyXrData.sequence);
                        ++SpidyXrData.toggles;
                        SpidyXrData.flatScreen = flatScreen;
                        InterlockedIncrement64(&SpidyXrData.sequence);
                    }
                    if (!frame.jump)
                        jumpFromScreen = false;
                    else if (wasScreen)
                        jumpFromScreen = true;
                    XrFrame controller = frame;
                    // The T-pose calibration has A as it has the other buttons.
                    controller.jump = frame.jump && !jumpFromScreen && !calibrationInput;
                    // Seated play: how far everything the game's world takes from
                    // the tracking space is raised - the rig's head, eyes and hands,
                    // the body's height, a T-pose calibration - measured from where
                    // the player sits in VR play, again after a recenter.
                    const float seatStanding = seated::standingEye(values.heightCm, calibratedEye, seated::defaultEye);
                    const float seatRaise =
                        seatLift.update(frame.seconds, values.seated,
                                        viewing && gameplay && !flatScreen && validTrackedPose(frame.head),
                                        frame.head.position.y, frame.recentered, seatStanding);
                    if (seatLift.measures() != seatMeasuresBefore) {
                        seatMeasuresBefore = seatLift.measures();
                        char line[128];
                        sprintf_s(line, "Seated: your eyes are %.2f m up; raised %.2f m to %.2f m (HEIGHT %s)",
                                  seatLift.seatedEye(),
                                  std::clamp(seatStanding - seatLift.seatedEye(), 0.f, seated::Lift::maxLift),
                                  seatStanding, values.heightCm ? "set" : "AUTO");
                        message(3, 0, line);
                    }
                    // Each hand's grip pose, and whether the headset has it; and
                    // whether the hand gestures play VR: gameplay in the headset,
                    // the controllers no calibration's.
                    const Pose grips[2]{frame.hands[0].grip, frame.hands[1].grip};
                    const bool gripsTracked[2]{frame.hands[0].valid && validTrackedPose(frame.hands[0].grip),
                                               frame.hands[1].valid && validTrackedPose(frame.hands[1].grip)};
                    const bool gesturesPlay = viewing && gameplay && !flatScreen && !calibrationInput &&
                                              validTrackedPose(frame.head);
                    // A trigger pulled with its hand at the temple is the game's
                    // scan, and no web ball or reel meanwhile.
                    {
                        const float triggers[2]{frame.hands[0].trigger, frame.hands[1].trigger};
                        templePress.update(frame.seconds, gesturesPlay, frame.head, grips, gripsTracked,
                                           triggers);
                        for (unsigned i = 0; i < 2; ++i)
                            if (templePress.hands() & (1u << i))
                                controller.hands[i].trigger = 0;
                        if (templePress.scans() != scansBefore) {
                            scansBefore = templePress.scans();
                            runtime.haptic(0, .3f);
                            runtime.haptic(1, .3f);
                            feel(body_haptics::Kind::scan);
                        }
                    }
                    // Both hands at the chest with both triggers pulled is the
                    // game's suit power (its L3 + R3, which VR play had no way
                    // to), and those triggers shoot nothing meanwhile.
                    {
                        const float triggers[2]{frame.hands[0].trigger, frame.hands[1].trigger};
                        suitPress.update(frame.seconds, gesturesPlay, frame.head, grips, gripsTracked, triggers);
                        for (unsigned i = 0; i < 2; ++i)
                            if (suitPress.hands() & (1u << i))
                                controller.hands[i].trigger = 0;
                        if (suitPress.powers() != suitPowersBefore) {
                            suitPowersBefore = suitPress.powers();
                            runtime.haptic(0, .9f, .12f);
                            runtime.haptic(1, .9f, .12f);
                            feel(body_haptics::Kind::leap);
                            game_prompts::note("both hands at the chest, both triggers -> the game's L3 + R3 (its suit "
                                               "power)");
                        }
                    }
                    // A grip squeezed at a back pocket draws the phone, and shoots
                    // no web until it is let go. Not with the phone out. In the pad
                    // layer too, where the grips are the game's bumpers: that grip
                    // is no bumper (October 19: a draw sent the game LB or RB until
                    // the phone came out), and the phone drawn ends the layer
                    // (PadLayer::leave), so the phone is always the way back to
                    // play's controls.
                    int phoneDrawnWith = -1;
                    {
                        const float squeezes[2]{frame.hands[0].squeeze, frame.hands[1].squeeze};
                        const bool drawPlay = gesturesPlay && !phone.open();
                        if (pocketDraw.update(frame.seconds, drawPlay, frame.head, grips, gripsTracked,
                                              squeezes))
                            phoneDrawnWith = pocketDraw.drewWith();
                        if (phoneDrawnWith >= 0 && padLayer.latched()) {
                            padLayer.leave();
                            // "Phone out" says it; "Pad layer off" would hide it.
                            padLayerWas = false;
                        }
                        for (unsigned i = 0; i < 2; ++i)
                            if (pocketDraw.hands() & (1u << i))
                                controller.hands[i].squeeze = 0;
                    }
                    // The buttons the game names in a prompt, pressed with the
                    // controls of those names (game_pad::NamedPress). A control
                    // it claims is the game's until it is let go, and does
                    // nothing of Spidy's meanwhile: no web ball or reel from that
                    // hand's web-ball button, no jump or flip from A, no turn
                    // from the right stick pushed for the game's D-pad.
                    {
                        const auto asked = game_prompts::demand();
                        game_pad::NamedPress::Input named;
                        named.buttons = frame.buttons;
                        // The temple's scan has a trigger, the pocket's draw a grip.
                        const uint32_t gestures =
                            values.triggerWebs ? pocketDraw.hands() : templePress.hands() | suitPress.hands();
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& hand = frame.hands[i];
                            named.balls[i] = values.triggerWebs ? hand.squeeze : hand.trigger;
                            named.ballsTaken[i] = (gestures & (1u << i)) != 0;
                            named.clicks[i] = hand.stickClick;
                            named.hands[i] = hand.grip.position - frame.head.position;
                            named.tracked[i] = gripsTracked[i] && validTrackedPose(frame.head);
                            named.busy[i] = swingState.webs[i].attached != 0;
                        }
                        named.stickX = frame.hands[1].stickX;
                        named.stickY = frame.hands[1].stickY;
                        named.airborne = game_swing::airborne(swingState, now.QuadPart, frequency.QuadPart);
                        named.slams = fightLately && (!values.realisticFlips || !values.automaticCombat);
                        const bool namedPlay =
                            gesturesPlay && !padLayer.latched() && !phone.open() && !gadgetWheel.open();
                        namedPad = namedPress.update(frame.seconds, namedPlay, named, asked);
                        const uint32_t claimed = namedPress.claimed();
                        if (claimed & game_pad::NamedPress::controlA) {
                            controller.jump = false;
                            controller.buttons &= ~static_cast<uint32_t>(buttonA);
                        }
                        for (unsigned i = 0; i < 2; ++i) {
                            auto& hand = controller.hands[i];
                            if (claimed & game_pad::NamedPress::ball(i))
                                (values.triggerWebs ? hand.squeeze : hand.trigger) = 0;
                        }
                        if (claimed & (game_pad::NamedPress::stickLeft | game_pad::NamedPress::stickRight))
                            controller.hands[1].stickX = 0;
                        if (values.flips && !values.controlledFlips && !values.realisticFlips &&
                            (claimed & game_pad::NamedPress::stickUp))
                            controller.hands[1].stickY = 0;
                        // While the game names its X a thrown fist is that X, and
                        // the swing lands no blow of its own (sendSwingSettings).
                        namedFists = namedPlay && (asked.buttons & game_pad::x) != 0;
                    }
                    // The player's up (its actor's second row): a wall's normal
                    // while the game holds it on one. Spidy's own flight is on none.
                    const Vec3 heroUp{body[4], body[5], body[6]};
                    // The wall that holds the player, as the latest swing sample
                    // has it (no older than a tenth of a second): the swing's own
                    // wall, its floor on the wall beside the body's centre, or
                    // the game's own crawl, whose actor has the feet on the
                    // surface itself.
                    //
                    // Both of them, since October 14. The crawl was held back
                    // while how it takes the stick went unmeasured, and that
                    // left the one state whose interacts work on a wall - the
                    // only one the game calls supported there - looking nothing
                    // like the wall walking the player asked to keep, so a vent
                    // cost him the view. The rig's own crawl path was built and
                    // tested the whole time; only this passed it nothing.
                    const Vec3 feet{body[12], body[13], body[14]};
                    SurfaceHold hold;
                    const bool swingFresh =
                        swingState.status && swingState.qpc &&
                        static_cast<int64_t>(swingState.qpc) > now.QuadPart - frequency.QuadPart / 10;
                    if (swingFresh && swingState.wall == static_cast<uint32_t>(game_swing::Surface::wall)) {
                        // The wall's face under the feet, in the wall's own frame
                        // (its distance is the body's centre's, a metre over the
                        // feet): the same floor the game's crawl stands the hero
                        // on, so going from the one hold to the other moves no one.
                        hold.normal = swingState.wallNormal;
                        hold.anchor = feet - hold.normal * (swingState.wallDistance - hold.normal.y);
                        hold.speed = length(swingState.velocity);
                    } else if (swingFresh && swingState.wall == static_cast<uint32_t>(game_swing::Surface::game)) {
                        // The crawl's feet are on the surface, so they are the
                        // floor under the player, and its normal comes from a
                        // ray where the swing has one. The actor's own up is
                        // that surface while it rocks on it (up to 14 degrees
                        // off over a few frames, reports/wall-crawl.json) and
                        // serves where the ray found nothing. In the game's own
                        // wall run the feet are off the wall by the distance
                        // the swing measured: the floor is the wall under them.
                        hold.normal = finite(swingState.wallNormal) && length(swingState.wallNormal) > .5f
                                          ? normalized(swingState.wallNormal)
                                          : normalized(heroUp);
                        hold.anchor = feet - hold.normal * (std::isfinite(swingState.wallDistance)
                                                                ? std::clamp(swingState.wallDistance, 0.f, 1.5f)
                                                                : 0.f);
                        hold.speed = length(swingState.velocity);
                        hold.crawl = true;
                        ++crawlFrames;
                    } else if (swingFresh && swingState.wall == static_cast<uint32_t>(game_swing::Surface::floor)) {
                        // The game's crawl on a floor: a vent, a duct. The
                        // floor is under the feet and level, whatever the
                        // crawling actor rocks to: the view stands as it
                        // stands, the body plants its feet on it (the game's
                        // crawl laid the legs out in front of the player in
                        // the first mission's duct, October 14 19:57), and
                        // the head stays under its roof (below).
                        hold.normal = {0, 1, 0};
                        hold.anchor = feet;
                        hold.speed = length(swingState.velocity);
                        hold.crawl = true;
                        ++floorCrawlFrames;
                    }
                    hold.vent = hold.crawl &&
                        (climbState.status && climbState.crawl == 1 && climbState.sampleTimeNs &&
                         frame.predictedDisplayTime > 0 &&
                         static_cast<uint64_t>(frame.predictedDisplayTime) >= climbState.sampleTimeNs &&
                         static_cast<uint64_t>(frame.predictedDisplayTime) - climbState.sampleTimeNs <=
                             100'000'000);
                    // The game's crawl on a floor keeps the head under the
                    // roof (below) whether or not the vent view takes it.
                    const bool gameFloorCrawl = hold.crawl && hold.normal.y > .99f;
                    // The gate keeps transient floor crawls out of the vent
                    // view: with vents off for now (ventHandCrawl) there is none.
                    const bool ventView = ventHandCrawl && gameplay && values.handClimb;
                    hold = crawlCameraGate.update(frame.seconds, ventView,
                        (origins.play() & buttonB) != 0, swingFresh && swingState.grounded, hold);
                    const bool crawlsFloor = hold.crawl && hold.normal.y > .99f;
                    const float crawlCap = floorRoof.update(gameFloorCrawl, frame.seconds, headroomSeen);
                    // Between the swing's wall and the game's crawl (game_warp:
                    // the game turning the hero onto the wall at rest, or off it
                    // as he walks on) the swing reports neither for a moment.
                    // The view keeps the surface it stood him on while the body
                    // stays at its floor, slower along it than a walk: dropped,
                    // the view turned back toward level and onto the wall again
                    // (48 degrees in a tenth of a second, October 14 16:48). A
                    // jump or a run off the wall leaves it at once.
                    {
                        constexpr float holdGrace = .4f, holdGraceSpeed = .8f;
                        const float seconds =
                            std::isfinite(frame.seconds) ? std::clamp(frame.seconds, 0.f, .1f) : 0.f;
                        const Vec3 n = lastHold.normal;
                        const Vec3 v = finite(swingState.velocity) ? swingState.velocity : Vec3{};
                        const bool held = length(hold.normal) > .5f;
                        holdGapSeconds = held ? 0.f : holdGapSeconds + seconds;
                        if (held) {
                            lastHold = hold;
                        } else if ((motion.standing || motion.ventCrawl) && length(n) > .5f &&
                                   holdGapSeconds < holdGrace &&
                                   std::abs(dot(feet - lastHold.anchor, n)) < 1.5f &&
                                   length(v - n * dot(v, n)) < (motion.ventCrawl ? 3.f : holdGraceSpeed)) {
                            // On the plane it stood on, with the feet: kept
                            // where it was while the game took a player over
                            // a shaft's lip at 1.3 m/s, the view stayed on
                            // the wall a third of a second after he had left
                            // it and then jumped to him (October 14, 86 s).
                            // Only for a player at rest: one on his way off
                            // the surface is on his way off it.
                            hold = lastHold;
                            hold.anchor = feet - n * dot(feet - lastHold.anchor, n);
                            ++holdGraceFrames;
                        } else {
                            lastHold = {};
                        }
                    }
                    // A or the left stick in the air flips the player (FlipMotion),
                    // in the air as the latest swing sample has it.
                    // In the game's crawl in a duct the head goes no higher
                    // than a crouch, whatever a ray finds over it: part of the
                    // first mission's duct has a roof no ray hits, and the
                    // head stood out through it at the player's own height.
                    // A duct is the crawl under a duct's roof (FloorCrawlRoof):
                    // the game crawls a hero ducking under a beam the same way.
                    float over = headroomSeen;
                    if (crawlCap > 0) {
                        over = over > 0 ? std::min(over, crawlCap) : crawlCap;
                        ++crawlCapFrames;
                    }
                    rig.headroom(over);
                    if (headroomSeen > 0 && (headroomLeast == 0 || headroomSeen < headroomLeast))
                        headroomLeast = headroomSeen;
                    // The eyes out of what the world is built of (HeadClearance): from a point in
                    // the body, where no wall is - half a metre off the wall that holds him, or
                    // a metre up from his feet along his up (the world's while Spidy's swing
                    // has him, which the game keeps upright in its air state) - and the
                    // surfaces the head's rays met in the batch the bridge last finished,
                    // facing them and of what the world is built of (a car, a crate or a
                    // passer-by moves, and is no wall).
                    {
                        const Vec3 held = normalized(hold.normal);
                        const Vec3 bodyUp = swingState.owned ? Vec3{0, 1, 0} : normalized(heroUp);
                        const bool holds = finite(held) && length(held) > .5f && finite(hold.anchor);
                        // A thick corner pillar can cover the old half-metre
                        // point. Start climbing camera rays inside the actual
                        // native capsule so its front cannot be ignored.
                        rig.bodyCentre(
                            headCollisionOrigin(feet, bodyUp, hold, values.handClimb && swingState.owned));
                        if (holds)
                            rig.surfaceFound({hold.anchor, held}, true);
                        if (rayResults) {
                            // The bridge publishes on another thread. Hand
                            // contacts consume each complete, fresh batch once.
                            const auto sequence = rayResults->sequence;
                            if (!(sequence & 1)) {
                                MemoryBarrier();
                                const native_rays::Data sample = *rayResults;
                                MemoryBarrier();
                                if (rayResults->sequence == sequence)
                                    rig.handFeedback(sample, now.QuadPart, frequency.QuadPart);
                            }
                        }
                        if (rayResults)
                            for (unsigned i = 0; i < native_rays::capacity && i < rayResults->count; ++i) {
                                const auto& hit = rayResults->hits[i];
                                const Vec3 n = normalized(hit.normal);
                                if (hit.ray.tag >= 2 && hit.ray.tag <= 6 && native_rays::fixedSurface(hit) &&
                                    std::isfinite(hit.fraction) && hit.fraction < 1 && finite(n) &&
                                    dot(n, hit.ray.direction) < -.2f)
                                    rig.surfaceFound({hit.position, n});
                            }
                    }
                    // REALISTIC FLIPS turn with the swing's flight: its
                    // velocity, and while it has the player each web's pull
                    // toward where it holds, by its tension.
                    RealisticFlight flight;
                    if (values.realisticFlips) {
                        flight.fighting = fightLately;
                        const int64_t fresh = now.QuadPart - frequency.QuadPart / 10;
                        flight.valid = swingState.qpc && static_cast<int64_t>(swingState.qpc) > fresh &&
                                       !swingState.error &&
                                       (swingState.status == 1 || swingState.status == 2);
                        flight.velocity = swingState.velocity;
                        float ropes{};
                        unsigned attached{};
                        if (flight.valid && swingState.owned)
                            for (const auto& web : swingState.webs) {
                                const Vec3 toward = web.anchor - feet;
                                if (!web.attached || !finite(web.anchor) || !std::isfinite(web.tension) ||
                                    length(toward) < .1f)
                                    continue;
                                flight.attached = true;
                                ropes += length(toward);
                                ++attached;
                                flight.pull += normalized(toward) * std::clamp(web.tension, .1f, 100.f);
                            }
                        if (attached)
                            flight.ropeLength = ropes / static_cast<float>(attached);
                    }
                    rig.flight(flight);
                    rig.wallHands(climbState.status ? climbState.hands : 0u);
                    rig.outwardCrawl(values.body && !flatScreen ? native_body::crawlOutwardWeight() : 0.f);
                    const bool runPalm = game_swing::outdoorClimbGrip(climbState,
                        frame.predictedDisplayTime > 0 ? frame.predictedDisplayTime : 0);
                    rig.wallRunBody(values.body && values.handClimb && !flatScreen && swingFresh,
                        runPalm ? climbState.hands : 0, swingState.velocity);
                    const bool flipZipPlay = gesturesPlay && !padLayer.latched() && !phone.open() &&
                        !gadgetWheel.open() && values.flips && !values.controlledFlips &&
                        !values.realisticFlips &&
                        game_swing::airborne(swingState, now.QuadPart, frequency.QuadPart) &&
                        length(hold.normal) < .5f;
                    const auto& zipHand = controller.hands[1];
                    const bool zipPriority = flipZipPriority.update(flipZipPlay, zipHand.valid,
                        zipHand.stickX, zipHand.stickY);
                    if (zipPriority || (flipZipPlay && zipPress.active())) {
                        controller.hands[1].stickX = controller.hands[1].stickY = 0;
                    }
                    motion = rig.update(seated::raised(controller, seatRaise), feet, {camera[8], camera[9], camera[10]}, gameplay,
                                        swingState.owned ? Vec3{0, 1, 0} : heroUp,
                                        game_swing::airborne(swingState, now.QuadPart, frequency.QuadPart), hold,
                                        {camera[4], camera[5], camera[6]});
                    if (values.eyeAim && motion.active && !flatScreen) {
                        ++eyeAimFrames;
                        gazeFrames += motion.gazeValid ? 1u : 0u;
                        // An eye tracker that gave no gaze in three seconds of
                        // play: over Quest Link and Virtual Desktop the eyes reach
                        // PC games only where the player switched that on.
                        if (!toldNoGaze && !gazeFrames && eyeAimFrames >= 270 &&
                            runtime.eyeTracking() == XrRuntime::EyeTracking::tracker) {
                            toldNoGaze = true;
                            message(3, 0,
                                    "EYE AIM: the headset has an eye tracker but sends no gaze. Quest Pro: turn on "
                                    "eye tracking for PC apps (Quest Link: Settings > Beta; Virtual Desktop: its "
                                    "eye tracking forwarding). Until then hands and head aim as before.");
                        }
                    }
                    if (motion.headDrop > .02f)
                        ++headDroppedFrames;
                    if (motion.active && motion.onSurface) {
                        surfaceEntries += !wasOnSurface;
                        ++surfaceFrames;
                    }
                    wasOnSurface = motion.active && motion.onSurface;
                    // Without VR play (menus, hint cards, cutscenes, animated
                    // cameras, loading, and the game played on its screen,
                    // VIEW: GAME SCREEN) the game's own camera goes on the
                    // screen.
                    const bool vrPlay = motion.active && !flatScreen;
                    bool screen{};
                    if (viewing)
                        screen = gameScreen.update(vrPlay, GetTickCount64());
                    else
                        gameScreen.reset();
                    wasScreen = screen;
                    // A scene the game plays there (a cutscene, a finisher, a
                    // death) gets its own two eyes: the game renders its
                    // camera once for each of the viewer's. Not a pause menu
                    // or a hint card (the game's frames take no game time),
                    // nor without a player (the title screen, a load), nor a
                    // scene whose eyes get no HUD panel for its subtitles.
                    const auto clock = game_time::telemetry();
                    const bool timeRuns =
                        sceneClock.running(clock.installed && clock.gameFrame > 0, GetTickCount64());
                    const bool scene =
                        scenePanel.wanted(screen && values.screenDepth > 0 && player.hero && bridgeRunning && timeRuns,
                                          native_hud::onScreen(sceneHoldMs), GetTickCount64());
                    // On the screen the controllers are the game's Xbox
                    // controller; in VR it gets Start (menu), Back (X) and
                    // the native walking, jumping, dodge and interact
                    // (submitted below).
                    // The pad layer is latched in the input block below, where the
                    // swing's state is known, so this reads the last frame's: the
                    // chord takes chordSeconds to latch, so a frame is nothing.
                    // Nothing reaches the game until the headset has tracked
                    // once: before that the player presses blind, and the title
                    // screen's QUIT GAME is two moves from the top.
                    headSeen = headSeen || validTrackedPose(frame.head);
                    const auto mapping =
                        game_pad::mappingFor(viewing, headSeen, screen, padLayer.latched());
                    const bool inPlay =
                        mapping == game_pad::Mapping::gameplay || mapping == game_pad::Mapping::pad;
                    origins.update(frame.buttons, inPlay);
                    // What the controllers themselves sent, beside what reached the
                    // game: a button missing from both was never pressed. The
                    // phone's presses are the controllers' too.
                    if (inPlay)
                        vrButtonsPlayed |= trackedFrame.buttons;
                    // The prompts are read wherever the game's UI builds one: what
                    // they name is what the controls of those names send in VR
                    // play (NamedPress). Off from October 13 to 20, because with
                    // the reading on CONTINUE at the main menu killed the game
                    // every time - the process simply gone, no crash dump. The
                    // death was the reading's own: the timeline's line for a
                    // prompt call went through sprintf_s into a buffer a long
                    // text overflowed, and sprintf_s ends the process for that
                    // (game_prompts.cpp, keepLine). The wording stays the game's
                    // but for its LB + RB in VR play (setRewriting): X sends them
                    // there (NamedPress), so the game's prompt says X (October 24).
                    // On the pad layer the grips are its bumpers again.
                    game_prompts::setObserving(promptHelp);
                    game_prompts::setReading(promptHelp);
                    game_prompts::setRewriting(promptHelp && mapping == game_pad::Mapping::gameplay);
                    // A change in the SPIDY VR tab takes effect at once.
                    if (game_menu::take(values)) {
                        ++settingChanges;
                        applySettings();
                        if (auto text = vr_settings::tabOnlyText(values); text != tabOnlySaved) {
                            if (!writeTabOnly(text))
                                message(3, 0, "VR settings: the SPIDY VR tab's own settings were not saved");
                            tabOnlySaved = std::move(text);
                        }
                    }
                    game_menu::publish(values);
                    const uint32_t gate =
                        (player.hero ? 0 : gateNoPlayer) | (bridgeRunning ? 0 : gateBridge) |
                        (committed ? 0 : gateNoCommit) | (followsPlayer ? 0 : gateOtherCamera) |
                        (gameplay && !motion.active ? gateTracking : 0);
                    if (gameplay && !motion.active) {
                        ++trackingGapFrames;
                        trackingGapReasons |= motion.inactive;
                    }
                    if ((gameScreen.entered() && !flatScreen) || ((flatScreen || screen) && frame.recentered))
                        screenPose = screenAhead(frame.head, vr_settings::screenDistance);
                    // An image keeps the tracking-space poses it was rendered
                    // for, valid until the tracking space itself changes.
                    if (!viewing || frame.recentered)
                        history.clear();
                    if (!viewing || frame.recentered || !motion.active || flatScreen)
                        hudFollow.reset();
                    if (!viewing) {
                        lastPresentedMs = lastNewImageMs = firstImageDeadline = 0;
                        presented = false;
                    } else if (vrPlay && !wasGameplay) {
                        // Gameplay counts its own images: the screen before it
                        // may have had none for a while (loading).
                        lastNewImageMs = 0;
                        presented = false;
                        firstImageDeadline = nativeStarted ? GetTickCount64() + 3000 : 0;
                    }
                    wasGameplay = vrPlay;
                    const bool controlsVisible = recentPresentation(lastNewImageMs, GetTickCount64());
                    // The T-pose calibration: at the first gameplay of a session
                    // without one, or when the SPIDY VR tab asks for it, once VR
                    // has shown play for a moment and no web flies the player. It
                    // goes on only while VR shows play (a pause or the flat
                    // screen holds it), and B skips it.
                    using CalibrationPhase = body_calibration::Phase;
                    const bool immersive = viewing && motion.active && !flatScreen && controlsVisible;
                    const uint64_t nowMs = GetTickCount64();
                    if (!immersive)
                        immersiveSince = 0;
                    else if (!immersiveSince)
                        immersiveSince = nowMs;
                    const bool skipPressed = (frame.buttons & buttonB) && !skipHeld;
                    skipHeld = (frame.buttons & buttonB) != 0;
                    if (calibration.phase() == CalibrationPhase::idle && (calibrationWanted || values.calibrate) &&
                        immersive && nowMs - immersiveSince >= 1500 && !swingState.owned) {
                        calibration.start();
                        calibrationPanel = body_calibration::panelPose(frame.head);
                        calibrationInput = true;
                        values.calibrate = false; // the tab shows NO again
                        runtime.haptic(0, .3f);
                        runtime.haptic(1, .3f);
                        message(3, 0, "Body calibration: stand in a T-pose and hold both triggers (B skips)");
                    } else if (calibration.phase() == CalibrationPhase::done) {
                        if (nowMs - calibrationShownAt >= 3500)
                            calibration.stop();
                    } else if (calibration.phase() != CalibrationPhase::idle && immersive && skipPressed) {
                        calibration.stop();
                        ++calibrationSkips;
                        // Nor at the next sessions' first gameplay (the launcher keeps it).
                        calibrationWanted = calibrationPrompt = false;
                        runtime.haptic(1, .25f);
                        message(3, 0, "Body calibration skipped - Settings > SPIDY VR > CALIBRATE BODY does it later");
                    } else if (calibration.phase() != CalibrationPhase::idle && immersive) {
                        body_calibration::Sample sample;
                        // Seated, the player as the game's world has them: raised
                        // (seated.hpp), so the eyes measure as standing ones.
                        const XrFrame placed = seated::raised(frame, seatRaise);
                        sample.headTracked = validTrackedPose(placed.head);
                        sample.head = placed.head;
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& hand = placed.hands[i];
                            sample.handTracked[i] = hand.valid && validTrackedPose(hand.grip);
                            sample.grips[i] = hand.grip;
                            sample.triggers[i] = hand.trigger;
                        }
                        sample.seconds = frame.seconds;
                        // The hero's rig once the body has turned it, else Spider-Man's measured one.
                        body_calibration::Proportions sizes;
                        native_body::proportions(sizes);
                        const auto before = calibration.phase();
                        if (calibration.update(sample, sizes)) {
                            lastCalibration = calibration.result();
                            calibratedEye = std::clamp(lastCalibration.eyeHeight,
                                                       body_calibration::minEyeHeightMm / 1000.f,
                                                       body_calibration::maxEyeHeightMm / 1000.f);
                            calibratedArm = std::clamp(lastCalibration.armLength,
                                                       body_calibration::minArmLengthMm / 1000.f,
                                                       body_calibration::maxArmLengthMm / 1000.f);
                            ++calibrations;
                            calibrationWanted = false;
                            calibrationShownAt = nowMs;
                            runtime.haptic(0, .7f);
                            runtime.haptic(1, .7f);
                            feel(body_haptics::Kind::calibrated);
                            char text[160];
                            sprintf_s(text, "Body calibrated: height %.2f m (eyes %.2f m), arm length %.2f m (left %.2f, "
                                      "right %.2f)",
                                      body_calibration::height(calibratedEye), calibratedEye, calibratedArm,
                                      lastCalibration.reach[0], lastCalibration.reach[1]);
                            message(3, 0, text);
                        } else if (before == CalibrationPhase::waiting &&
                                   calibration.phase() == CalibrationPhase::holding) {
                            // The pose counts: a light tick in both hands.
                            runtime.haptic(0, .15f);
                            runtime.haptic(1, .15f);
                        }
                    }
                    // The phone, which a back-pocket draw opens in the hand. It is
                    // live while VR shows play and no calibration has the
                    // controllers, which it leaves to a calibration (October 19:
                    // one begun under it could be neither finished nor skipped).
                    {
                        const bool phonePlay = immersive && calibration.phase() == CalibrationPhase::idle;
                        if (phone.update(frame.seconds, phonePlay, frame.head, grips, gripsTracked,
                                         phoneDrawnWith, pocketDraw.hand())) {
                            phoneMusic.load();
                            runtime.haptic(0, .2f);
                            runtime.haptic(1, .2f);
                            feel(body_haptics::Kind::phone, pocketDraw.drewWith());
                            message(3, 0, "Phone out - the stick, A, B and the triggers are the phone's "
                                          "until B puts it away");
                        }
                    }
                    // While it is out its hands land no blow (Settings::noBlows),
                    // nor while the game names its X (NamedPress, above).
                    if (phone.live() != phoneBlows || namedFists != fistsBlows)
                        sendSwingSettings();
                    if (calibrationInput && calibration.phase() == CalibrationPhase::idle) {
                        bool held = (frame.buttons & buttonB) || frame.jump;
                        for (const auto& hand : frame.hands)
                            held = held || hand.trigger > .2f || hand.squeeze > .2f;
                        calibrationInput = held;
                    }
                    if (calibrationInput) {
                        // The calibration has the controllers: no web, web ball or jump.
                        motion.swing.jump = false;
                        motion.nativeKeys &= ~swingJumpKey;
                        for (auto& hand : motion.swing.hands)
                            hand.trigger = hand.grip = 0;
                    }
                    if (padLayer.latched()) {
                        // The pad layer has them too: the grips are the game's bumpers
                        // there and the triggers its triggers, so neither shoots a web.
                        // A is the game's own A straight from the controller, so the
                        // swing's leap goes with them.
                        motion.swing.jump = false;
                        motion.nativeKeys &= ~swingJumpKey;
                        for (auto& hand : motion.swing.hands)
                            hand.trigger = hand.grip = 0;
                    }
                    // Slow motion: the left thumbstick's click, in VR play outside the calibration.
                    const bool slowAllowed = immersive && calibration.phase() == CalibrationPhase::idle;
                    // A click the game names (its L3, its R3) is not slow motion.
                    const uint32_t clicksNamed = namedPress.claimed();
                    const bool slowLeft =
                        frame.hands[0].stickClick && !(clicksNamed & game_pad::NamedPress::leftClick);
                    const bool slowRight =
                        frame.hands[1].stickClick && !(clicksNamed & game_pad::NamedPress::rightClick);
                    const bool slowPressed =
                        slowPress.update(frame.seconds, slowAllowed, slowLeft, slowRight);
                    stepSlowMotion(frame.seconds, slowPressed, slowAllowed);
                    if (motion.active && !raysStarted) {
                        native_rays::Config rays;
                        rays.pid = config.pid;
                        rays.base = config.base;
                        rays.durationMs = moduleDuration;
                        check(startRays(&rays), "Start controller world rays");
                        raysStarted = true;
                    }
                    if (raysStarted) {
                        auto input = motion.swing;
                        input.focused = motion.active && !flatScreen;
                        auto aim = controllerAimRays(input, ++serial);
                        // The head's rays, in the slots the hands leave free
                        // (HeadClearance): from the body to past the eyes, and
                        // from the eyes ahead, to each side and up. Until
                        // October 18 these five read the room around the head
                        // for the report alone (Enclosure).
                        if (motion.active && validTrackedPose(motion.headPose)) {
                            const Quat turn = motion.headPose.orientation;
                            const Vec3 ahead = turn.rotate({0, 0, -1}), aside = turn.rotate({1, 0, 0}),
                                       overhead = turn.rotate({0, 1, 0});
                            const auto head = rig.clearanceRays(ahead, aside, overhead);
                            const auto hands = rig.handCollisionRays(motion.headPose.position);
                            // And the room over his feet, along the surface he
                            // stands on: how far the head can go before it is
                            // through the roof of a vent or a duct. From the
                            // FEET, where the other five are from the head - a
                            // head already through that roof would cast from
                            // the far side of it and find nothing.
                            //
                            // Along the up the head is actually placed on: the
                            // surface's normal while the view stands on it, the
                            // world's up otherwise, because that is the axis the
                            // rig caps. The actor's own up is neither while
                            // Spidy holds him on a wall - the game has him
                            // upright in its air state there - and casting along
                            // it found the shaft's ceiling 0.16 m over his feet
                            // and brought his head down to a crouch for 22
                            // frames (reports/game-vr-20261014-124656.json).
                            const Vec3 up = motion.standing && finite(hold.normal) && length(hold.normal) > .5f
                                                ? normalized(hold.normal)
                                                : Vec3{0, 1, 0};
                            const native_rays::Ray roof{feet + up * .1f, headroomReach, up, 7u};
                            appendTrackingRays(aim, head, hands, roof, collisionRayPhase++);
                        }
                        check(submitRays(&aim), "Controller world rays");
                        // What the roof's ray found, from the batch the bridge
                        // last finished (the head's go to the rig before its
                        // update, above).
                        if (rayResults) {
                            for (unsigned i = 0; i < native_rays::capacity && i < rayResults->count; ++i) {
                                const auto& hit = rayResults->hits[i];
                                if (hit.ray.tag != 7)
                                    continue;
                                // The surface over the feet, from where the ray
                                // started a tenth of a metre up. A reading
                                // tighter than a crouch is not a roof - a step,
                                // a strut, a lip at his feet - and the head is
                                // left alone for it: the rule is for a space a
                                // player is inside, not for whatever his feet
                                // are against.
                                const float found = native_rays::fixedSurface(hit) && std::isfinite(hit.fraction)
                                                        ? hit.fraction * hit.ray.distance + .1f
                                                        : 0.f;
                                headroomSeen = found >= leastHeadroom + headClearance ? found : 0.f;
                            }
                        }
                    }
                    if (motion.active && !flatScreen && controlsVisible && !swingStarted) {
                        game_swing::Config swing;
                        swing.pid = config.pid;
                        swing.base = config.base;
                        swing.record = player.record;
                        swing.mover = player.mover;
                        swing.motionModule = config.motionModule;
                        swing.durationMs = moduleDuration;
                        swing.maxSpeed = values.swingSpeed;
                        swing.gravity = vr_settings::gravity(values.weight);
                        swing.grabKinds = !values.webGrab || !sampleGrab ? 0 : game_grab::movableKinds;
                        check(startSwing(&swing), "Start native swinging");
                        swingStarted = true;
                        // Fists and the web shooter take the swing's input samples;
                        // both are optional and, like the grab, can be switched on later.
                        applySettings();
                    }
                    if (swingStarted) {
                        game_swing::Command input;
                        input.wallRotate = motion.wallRotate;
                        input.serial = ++serial;
                        input.focused = motion.active && !flatScreen && controlsVisible;
                        input.sampleTimeNs = frame.predictedDisplayTime > 0 ? frame.predictedDisplayTime : 1;
                        input.sampleSeconds =
                            std::isfinite(frame.seconds) && frame.seconds > 0 && frame.seconds <= .1f
                                ? frame.seconds
                                : 1.f / 90.f;
                        input.jump = motion.swing.jump;
                        input.move = motion.swing.move;
                        input.trackingYaw = motion.swing.trackingYaw;
                        input.tilt = motion.swing.tilt;
                        input.leap = input.focused ? leapOut : 0.f;
                        input.asks = input.focused && askingGame ? 1u : 0u;
                        if (const Vec3 look = motion.headPose.orientation.rotate({0, 0, -1});
                            input.focused && finite(look) && length(look) > .5f)
                            input.look = normalized(look);
                        // EYE AIM: where the eyes look, for the web balls they aim
                        // and for what the aim previews say they meet (the zip's
                        // camera is turned onto that).
                        if (const Vec3 gaze = motion.gazePose.orientation.rotate({0, 0, -1});
                            values.eyeAim && input.focused && motion.gazeValid && finite(gaze) &&
                            length(gaze) > .5f) {
                            input.gazeOrigin = motion.gazePose.position;
                            input.gaze = normalized(gaze);
                            input.eyeAim = (values.eyeAim & vr_settings::eyeAimShots) ? 1u : 0u;
                        }
                        const Input climbInput = wallClimbInput(motion);
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& hand = climbInput.hands[i];
                            input.hands[i] = {hand.aim, hand.gripRelativeToHead, hand.tracked, hand.trigger,
                                              hand.grip};
                            input.wallPositions[i] = motion.grips[i].position;
                            input.wallGrips[i] = hand.tracked ? frame.hands[i].squeeze : 0.f;
                        }
                        check(submitSwing(&input), "Tracked swing input");
                        check(sampleSwing(&swingState), "Native swing feedback");
                        game_swing::SlingshotData slingshot;
                        slingshotPulseSeconds += frame.seconds;
                        if (values.webSlingshot && input.focused && sampleSlingshot &&
                            !sampleSlingshot(&slingshot) && slingshot.magic == 0x53534c47 &&
                            slingshot.version == 1 && slingshot.bytes == sizeof(slingshot) &&
                            slingshot.charge > 0 && slingshot.charge <= 1) {
                            if (slingshotPulseSeconds >= .06f) {
                                for (unsigned i = 0; i < 2; ++i)
                                    runtime.haptic(i, .15f + .65f * slingshot.charge, .045f);
                                slingshotPulseSeconds = 0;
                            }
                        } else {
                            slingshotPulseSeconds = .06f;
                        }
                        if (!sampleClimb || sampleClimb(&climbState) || climbState.magic != 0x53434c44 ||
                            climbState.version != 2 || climbState.bytes != sizeof(climbState))
                            climbState = {};
                        const unsigned wallPulses =
                            climbPulses.update(climbState, input.focused && values.handClimb);
                        for (unsigned i = 0; i < 2; ++i)
                            if (wallPulses & (1u << i)) {
                                runtime.haptic(i, .45f, .045f);
                                feel(body_haptics::Kind::wallGrip, static_cast<int>(i));
                            }
                        if (!sampleBends || sampleBends(&swingBends))
                            swingBends = {};
                        // Nothing here changes who holds the player on a wall
                        // behind his back. Every way of doing it from the room
                        // around him dropped him (October 14: switching the
                        // walls off, refusing the next wall of a climb, and a
                        // push into the wall with the walls off, each a fall of
                        // up to 7.5 m), and the player put the reason plainly:
                        // "the shaft shouldnt affect anything, the vent in the
                        // top is the target, there are similiar events in the
                        // game with vents outside". The room is still read, for
                        // the report.
                        check(swingState.error, "Native swinging");
                        if (!sampleGrab || sampleGrab(&grabState))
                            grabState = {};
                        // What each free hand's web button would do: the aim markers.
                        aims = aimed = {};
                        if (sampleAim && input.focused) {
                            game_swing::AimData sample;
                            const game_swing::AimData expected;
                            if (!sampleAim(&sample) && sample.magic == expected.magic &&
                                sample.version == expected.version && sample.bytes == expected.bytes &&
                                sample.status == 2)
                                aimed = sample;
                        }
                        if (values.aimMarkers)
                            aims = aimed;
                        // A free hand come onto the other hand's web: a light
                        // tick, a press here takes hold of that web.
                        for (unsigned i = 0; i < 2; ++i) {
                            const bool on =
                                aimed.hands[i].kind == static_cast<uint32_t>(game_swing::AimKind::strand);
                            if (on && !onStrandBefore[i] &&
                                frame.predictedDisplayTime - strandTickAt[i] > 400'000'000) {
                                runtime.haptic(i, .2f);
                                strandTickAt[i] = frame.predictedDisplayTime;
                            }
                            onStrandBefore[i] = on;
                        }
                        bool trails[2]{};
                        Vec3 trailEnds[2]{};
                        // The hands whose webs a zip pulled: one zip on the body, in the arm of
                        // each (both: a web held in both hands).
                        unsigned zipHands{};
                        for (unsigned i = 0; i < 2; ++i) {
                            // A web that caught a thug ends on him, wherever he goes.
                            const auto& held = grabState.hands[i];
                            const bool grabbing = input.focused && held.phase != 0;
                            const bool wasGrabbing = grabPhases[i] != 0;
                            const int64_t displayAt = frame.predictedDisplayTime;
                            if (grabbing) {
                                swingState.webs[i] = {1, 0, held.end, held.length, held.taut ? 1.f : 0.f};
                                // Caught, yanked, at the hand: each its own pulse.
                                const auto phase = static_cast<GrabPhase>(held.phase);
                                if (held.phase != grabPhases[i]) {
                                    runtime.haptic(i, phase == GrabPhase::Yanked ? 1.f
                                                      : phase == GrabPhase::Held ? .4f
                                                                                 : .55f);
                                    pulseUntil[i] = displayAt + 30'000'000;
                                    feel(phase == GrabPhase::Yanked ? body_haptics::Kind::grabYanked
                                         : phase == GrabPhase::Held ? body_haptics::Kind::grabHeld
                                                                    : body_haptics::Kind::grabCaught,
                                         static_cast<int>(i));
                                } else if (held.tension > .05f && displayAt >= pulseUntil[i]) {
                                    // Between pulses the hand feels the web pull: a
                                    // hanging trash can faintly, one swung hard or too
                                    // heavy to lift strongly.
                                    runtime.haptic(i, std::min(.6f, .6f * held.tension));
                                }
                            } else if (wasGrabbing) {
                                runtime.haptic(i, .6f); // thrown or let go
                                feel(body_haptics::Kind::grabLetGo, static_cast<int>(i));
                            }
                            grabPhases[i] = grabbing ? held.phase : 0;
                            // The web let go of trails what it held as it dissolves.
                            trails[i] = input.focused && !grabbing && held.trailing;
                            trailEnds[i] = held.end;
                            const bool attached =
                                input.focused && swingState.status && swingState.webs[i].attached;
                            // A web taken hold of, not shot: the hand closes on it,
                            // a softer pulse than a shot's.
                            const bool taken = attached && !grabbing && (swingState.webs[i].attached & 4);
                            if (attached != attachedBefore[i] && !grabbing && !wasGrabbing) {
                                runtime.haptic(i, !attached ? .2f : taken ? .4f : .65f);
                                feel(!attached ? body_haptics::Kind::webRelease
                                     : taken   ? body_haptics::Kind::webTaken
                                               : body_haptics::Kind::webAttach,
                                     static_cast<int>(i));
                            }
                            if (attached && swingState.zips > zipBefore && !grabbing) {
                                runtime.haptic(i, 1);
                                zipHands |= 1u << i;
                            }
                            attachedBefore[i] = attached;
                            webTaken[i] = taken;
                            webTimes.update(i, attached, swingState.webs[i].anchor,
                                            webShooter(motion.grips[i], i).position,
                                            frame.predictedDisplayTime);
                        }
                        zipBefore = swingState.zips;
                        if (zipHands) {
                            // On the body toward where the zipped webs pull it.
                            Vec3 toward{};
                            for (unsigned i = 0; i < 2; ++i)
                                if (zipHands & (1u << i))
                                    toward += swingState.webs[i].anchor - swingState.position;
                            feelFrom(body_haptics::Kind::zip, zipHands == 3 ? -1 : zipHands == 2 ? 1 : 0, 1, toward);
                        }
                        {
                            // One web in both hands is one of the game's ropes,
                            // from the hand that holds it nearer its end.
                            const bool shared = attachedBefore[0] && attachedBefore[1] &&
                                                (swingState.webs[0].attached & 2) &&
                                                (swingState.webs[1].attached & 2) && !grabPhases[0] && !grabPhases[1];
                            std::array<WebRopes::Hand, 2> held{};
                            for (unsigned i = 0; i < 2; ++i)
                                held[i] = {attachedBefore[i], webTaken[i], shared, swingState.webs[i].length,
                                           webTimes[i].attachedAt};
                            webRopes.update(held);
                        }
                        // A thug the web brought to a player in the air has
                        // arrived within reach: time slows, to hit him there.
                        // (Until the fifth build of October 12 it slowed as the
                        // yank began, with the thug still far off.)
                        if (grabState.catches > catchesBefore && input.focused &&
                            game_swing::airborne(swingState, now.QuadPart, frequency.QuadPart)) {
                            moments.start(Moment::airPull);
                            runtime.haptic(0, .45f);
                            runtime.haptic(1, .45f);
                            feel(body_haptics::Kind::airPull);
                        }
                        catchesBefore = grabState.catches;
                        // Onto a wall, and off it with a jump: both hands feel it.
                        if (input.focused && swingState.status &&
                            (swingState.walls > wallsBefore || swingState.wallJumps > wallJumpsBefore)) {
                            const float pulse = swingState.wallJumps > wallJumpsBefore ? .6f : .45f;
                            runtime.haptic(0, pulse);
                            runtime.haptic(1, pulse);
                            // On the body's side toward the wall (behind its normal).
                            feelFrom(swingState.wallJumps > wallJumpsBefore ? body_haptics::Kind::wallJump
                                                                            : body_haptics::Kind::wallLand,
                                     -1, 1, -swingState.wallNormal);
                        }
                        wallsBefore = swingState.walls;
                        wallJumpsBefore = swingState.wallJumps;
                        // Every landed blow, a fist's or a web strike's: the hand
                        // that threw it kicks, as hard and as long as the blow was
                        // (a jab 60 ms, a blow that sends him flying 180 ms and the
                        // other hand with it); the game's time stops for a moment;
                        // a burst shows where it landed, with its count in the
                        // flow; and it sounds. Until October 11 a landed punch was
                        // one 25 ms pulse at half strength and nothing else.
                        game_punch::Strikes strikes{};
                        const game_punch::Strikes expectedStrikes;
                        const bool fighting =
                            punchStarted && sampleStrike && !sampleStrike(&strikes) &&
                            strikes.magic == expectedStrikes.magic && strikes.version == expectedStrikes.version &&
                            strikes.bytes == expectedStrikes.bytes;
                        if (fighting) {
                            // Punching switched off and on again counts from nothing.
                            blowsBefore = std::min(blowsBefore, strikes.blows);
                            strikesBefore = std::min(strikesBefore, strikes.opened);
                            knockoutsBefore = std::min(knockoutsBefore, strikes.knockouts);
                            const bool diving =
                                strikes.phase == static_cast<uint32_t>(WebStrike::Phase::Flying) &&
                                strikes.hand == static_cast<uint32_t>(WebStrike::diveHand);
                            if (strikes.opened > strikesBefore && input.focused) {
                                // Off to him: a tick in both hands, and the rush. A
                                // dive's is harder, and the last of its way slows.
                                runtime.haptic(0, diving ? .7f : .35f, diving ? .08f : .05f);
                                runtime.haptic(1, diving ? .7f : .35f, diving ? .08f : .05f);
                                feel(body_haptics::Kind::strikeRush);
                                // BLOW SOUNDS: OFF silences the rush as well.
                                if (values.blowSounds != 2)
                                    play(ImpactSound::Rush);
                                if (diving) {
                                    moments.start(Moment::dive);
                                    ++dives;
                                }
                            }
                            // The dive's legs kick out at the one it flies at (his
                            // chest, where the lock's mark is), and a moment after.
                            if (diving) {
                                diveAt = strikes.cueTarget == strikes.target && finite(strikes.cuePoint)
                                             ? strikes.cuePoint
                                             : strikes.destination;
                                kickUntil = frame.predictedDisplayTime + 250'000'000;
                            }
                            if (strikes.blows > blowsBefore && input.focused) {
                                const auto kind = static_cast<BlowKind>(strikes.lastKind);
                                const bool called = strikes.lastCalled != 0;
                                const bool finisher = kind == BlowKind::Finisher,
                                           heavy = kind != BlowKind::Hit || called;
                                // Time slows for a moment at the blows worth a look.
                                if (called)
                                    moments.start(Moment::called);
                                else if (finisher || kind == BlowKind::Launch || kind == BlowKind::Slam)
                                    moments.start(Moment::blow);
                                const unsigned hand = strikes.lastHand & 1;
                                const float strength =
                                    heavy ? 1.f
                                          : std::isfinite(strikes.lastStrength)
                                                ? std::clamp(strikes.lastStrength, 0.f, 1.f)
                                                : 0.f;
                                const float kick = finisher ? .32f : heavy ? .18f : .06f + .08f * strength;
                                runtime.haptic(hand, heavy ? 1.f : .6f + .4f * strength, kick);
                                if (heavy)
                                    runtime.haptic(1 - hand, finisher ? 1.f : .5f, finisher ? kick : .1f);
                                feel(finisher ? body_haptics::Kind::finisher
                                     : heavy  ? body_haptics::Kind::heavyBlow
                                              : body_haptics::Kind::punch,
                                     static_cast<int>(hand), strength);
                                for (unsigned i = 0; i < 2; ++i)
                                    if (i == hand || heavy)
                                        pulseUntil[i] = frame.predictedDisplayTime +
                                                        static_cast<int64_t>(kick * 1e9f) + 20'000'000;
                                hitStop.hit(strength, finisher);
                                if (finite(strikes.lastPoint)) {
                                    auto& burst = bursts[nextBurst++ % bursts.size()];
                                    burst.view = {strikes.lastPoint, strength, kind,
                                                  static_cast<int>(std::min(strikes.lastCount, 999u)), 0, called};
                                    burst.since = frame.predictedDisplayTime;
                                }
                                // BLOW SOUNDS: the game's own impact plays on him
                                // while it can (game_sound.hpp), then none of the
                                // headset's; SPIDY, the headset's; OFF, none.
                                const bool gameSounds =
                                    values.blowSounds == 0 && soundResults &&
                                    *static_cast<const volatile uint32_t*>(&soundResults->live) == 1;
                                if (values.blowSounds != 2 && !gameSounds) {
                                    play(finisher ? ImpactSound::Finisher
                                         : heavy ? ImpactSound::Heavy
                                         : strength >= .35f ? ImpactSound::Firm
                                                            : ImpactSound::Light);
                                    ++ownSounds;
                                }
                            }
                            // A knockout of the player's: time eases for an instant, both
                            // hands feel it, and it is lettered where he stood. This one is
                            // done.
                            if (strikes.knockouts > knockoutsBefore && input.focused &&
                                finite(strikes.knockoutPoint)) {
                                moments.start(Moment::knockout);
                                runtime.haptic(0, .8f, .09f);
                                runtime.haptic(1, .8f, .09f);
                                feel(body_haptics::Kind::knockout);
                                auto& mark = knockoutMarks[nextKnockout++ % knockoutMarks.size()];
                                mark.view = {strikes.knockoutPoint, 0};
                                mark.since = frame.predictedDisplayTime;
                            }
                            blowsBefore = strikes.blows;
                            strikesBefore = strikes.opened;
                            knockoutsBefore = strikes.knockouts;
                        }
                        // The mark over the thug the next punch would go to, with
                        // the arrow of the blow the flow calls. A new call ticks
                        // in both hands.
                        if (fighting && input.focused && strikes.cueTarget && finite(strikes.cuePoint)) {
                            const bool locked = strikes.cueLocked != 0;
                            if (strikes.cueTarget != cueTarget || strikes.cueWay != cueWay || locked != cueLocked)
                                cueSince = frame.predictedDisplayTime;
                            cueLocked = locked;
                            if (strikes.cueWay && strikes.cueWay != cueWay) {
                                runtime.haptic(0, .12f);
                                runtime.haptic(1, .12f);
                            }
                            cueTarget = strikes.cueTarget;
                            cueWay = strikes.cueWay;
                            cue = {strikes.cuePoint, static_cast<CueWay>(strikes.cueWay), strikes.cueFinisher != 0,
                                   strikes.cueLeft, 0, locked};
                        } else {
                            cue = {};
                            cueTarget = 0;
                            cueWay = 0;
                            cueLocked = false;
                        }
                        // Spider-Sense (spider_sense.hpp): a mark for each attacker the
                        // game warned the player of, from the display time it showed.
                        // A new one tingles in the hand on his side (both for one
                        // ahead or behind), a dodge's harder. A counter breaks its
                        // mark; time slows for a moment and both hands feel it.
                        game_sense::Data senseNow{};
                        const game_sense::Data expectedSense;
                        const bool sensing = punchStarted && sampleSense && !sampleSense(&senseNow) &&
                                             senseNow.magic == expectedSense.magic &&
                                             senseNow.version == expectedSense.version &&
                                             senseNow.bytes == expectedSense.bytes && senseNow.status == 2;
                        for (auto& mark : senseMarks)
                            mark.live = false;
                        if (sensing && input.focused) {
                            const Pose& eyes = motion.headPose;
                            for (uint32_t i = 0; i < std::min<uint32_t>(senseNow.count, 6); ++i) {
                                const auto& t = senseNow.threats[i];
                                if (!t.attacker || !finite(t.head) || !t.answer || t.answer > 3)
                                    continue;
                                SenseMark* mark{};
                                for (auto& m : senseMarks)
                                    if (m.attacker == t.attacker)
                                        mark = &m;
                                if (!mark) {
                                    for (auto& m : senseMarks)
                                        if (!m.attacker && !mark)
                                            mark = &m;
                                    if (!mark)
                                        continue;
                                    *mark = {};
                                    mark->attacker = t.attacker;
                                    mark->since = frame.predictedDisplayTime;
                                    ++senseShown;
                                    const Vec3 to = t.head - eyes.position;
                                    const Vec3 right = eyes.orientation.rotate({1, 0, 0});
                                    const bool placed = length(to) > 1e-3f && validTrackedPose(eyes);
                                    const float side = placed ? dot(normalized(to), right) : 0.f;
                                    const bool dodge = t.answer == static_cast<uint8_t>(SenseAnswer::Dodge);
                                    const int64_t until = frame.predictedDisplayTime + 90'000'000;
                                    for (unsigned h = 0; h < 2; ++h)
                                        if ((h == 0 && side < .35f) || (h == 1 && side > -.35f)) {
                                            runtime.haptic(h, dodge ? .6f : .35f, dodge ? .07f : .045f);
                                            pulseUntil[h] = std::max(pulseUntil[h], until);
                                        }
                                    ++senseTingles;
                                    // In the head, from his side.
                                    feelFrom(dodge ? body_haptics::Kind::senseDodge : body_haptics::Kind::spiderSense,
                                             -1, 1, placed ? to : Vec3{});
                                }
                                mark->live = true;
                                mark->head = t.head;
                                mark->answer = static_cast<SenseAnswer>(t.answer);
                                mark->left = std::isfinite(t.left) ? std::clamp(t.left, 0.f, 1.f) : 1.f;
                                mark->timed = t.timed != 0;
                                if (t.countered && !mark->brokenAt) {
                                    mark->brokenAt = frame.predictedDisplayTime;
                                    ++senseBroken;
                                }
                            }
                            // The attack to land first, timed ones before the rest: a
                            // blow the player takes next comes from him (the hits below).
                            const game_sense::Threat* first{};
                            for (uint32_t i = 0; i < std::min<uint32_t>(senseNow.count, 6); ++i) {
                                const auto& t = senseNow.threats[i];
                                if (t.attacker && finite(t.middle) &&
                                    (!first || (t.timed && (!first->timed || t.toImpact < first->toImpact))))
                                    first = &t;
                            }
                            if (first) {
                                blowFrom = first->middle;
                                blowSeen = frame.predictedDisplayTime;
                            }
                            // A counter made: its attacker's mark breaks, even one the
                            // game has taken off already.
                            const uint64_t counters = senseNow.thrown + senseNow.landed;
                            countersBefore = std::min(countersBefore, counters);
                            if (counters > countersBefore) {
                                for (auto& m : senseMarks)
                                    if (m.attacker && m.attacker == senseNow.lastCounter && !m.brokenAt) {
                                        m.brokenAt = frame.predictedDisplayTime;
                                        ++senseBroken;
                                    }
                                moments.start(Moment::called);
                                const int64_t until = frame.predictedDisplayTime + 100'000'000;
                                for (unsigned h = 0; h < 2; ++h) {
                                    runtime.haptic(h, .8f, .08f);
                                    pulseUntil[h] = std::max(pulseUntil[h], until);
                                }
                                feel(body_haptics::Kind::counter);
                            }
                            countersBefore = counters;
                        }
                        // A dodge on a web the game took and that answered a warning
                        // (game_dodge): the whoosh across the body as a dodge's, from
                        // the side the web carries the player away from, and a breath
                        // in both hands. It is a dodge while a warning stands.
                        if (dodgeResults) {
                            const auto* answers = static_cast<const volatile uint64_t*>(dodgeResults->answers);
                            const uint64_t answered = answers[1] + answers[2] + answers[3];
                            // Those of an earlier session in this game are not this one's.
                            if (webDodgesBefore == UINT64_MAX)
                                webDodgesBefore = answered;
                            if (answered > webDodgesBefore && input.focused) {
                                const volatile float* way = &dodgeResults->lastWay.x;
                                const float right =
                                    way[0] * motion.walkRightAxis.x + way[2] * motion.walkRightAxis.z;
                                feel(body_haptics::Kind::dodge, -1, 1, right > 0 ? -1.f : 1.f);
                                const int64_t until = frame.predictedDisplayTime + 60'000'000;
                                for (unsigned h = 0; h < 2; ++h) {
                                    runtime.haptic(h, .3f, .05f);
                                    pulseUntil[h] = std::max(pulseUntil[h], until);
                                }
                                ++senseDodges;
                            }
                            webDodgesBefore = answered;
                        }
                        // A warning gone (its blow landed, or the game took it off) is
                        // gone at once; a broken mark plays out its poses first.
                        for (auto& m : senseMarks) {
                            const double sinceBroken =
                                static_cast<double>(frame.predictedDisplayTime - m.brokenAt) * 1e-9;
                            const bool breaking = m.brokenAt && sinceBroken < SenseView::brokenSeconds;
                            if (m.attacker && !m.live && !breaking)
                                m = {};
                        }
                        game_punch::Data punch{};
                        if (punchStarted && !samplePunch(&punch))
                            for (unsigned i = 0; i < 2; ++i) {
                                // With an older ray module: a landed punch knocks back
                                // into the hand, as hard as it was; the web's rumble
                                // waits for it.
                                if (!fighting && punch.hands[i].punches > punchesBefore[i] && input.focused) {
                                    runtime.haptic(i, .5f + .5f * punch.hands[i].lastStrength);
                                    pulseUntil[i] = frame.predictedDisplayTime + 40'000'000;
                                    feel(body_haptics::Kind::punch, static_cast<int>(i), punch.hands[i].lastStrength);
                                }
                                punchesBefore[i] = punch.hands[i].punches;
                            }
                        game_shooter::Data shots{};
                        if (shooterStarted && !sampleShooter(&shots)) {
                            listed = shots;
                            for (unsigned i = 0; i < 2; ++i) {
                                // A web ball leaving the wrist: a short, crisp tick.
                                if (shots.hands[i].shots > shotsBefore[i] && input.focused) {
                                    runtime.haptic(i, .35f);
                                    pulseUntil[i] = frame.predictedDisplayTime + 25'000'000;
                                    ballShot[i] = true;
                                    feel(body_haptics::Kind::webShot, static_cast<int>(i));
                                    eyeShots += input.eyeAim != 0;
                                }
                                shotsBefore[i] = shots.hands[i].shots;
                            }
                        }
                        fightNear = input.focused && ((fighting && strikes.cueTarget != 0) || punch.bots != 0);
                        if (input.focused && ((fighting && strikes.enemies != 0) || punch.bots != 0))
                            fightAt = now.QuadPart;
                        if (websStarted && motion.active && !flatScreen) {
                            // The game draws these webs from the tracked web shooters.
                            native_webs::Request webs;
                            native_webs::Bent bent[2]{};
                            webs.feet = motion.anchor;
                            for (unsigned i = 0; i < 2; ++i) {
                                auto& hand = webs.hands[i];
                                // Each rope draws the web WebRopes gave it, from the
                                // web shooter of the hand that holds that web nearer
                                // its end.
                                const auto& rope = webRopes.rope(i);
                                const auto from =
                                    static_cast<unsigned>(rope.hands ? rope.from : rope.last >= 0 ? rope.last : i);
                                // A web let go of a thrown prop goes with it as it dissolves.
                                const bool trail = !rope.hands && rope.last >= 0 && !attachedBefore[from] && trails[from];
                                hand.attached = rope.hands ? 1 : trail ? 2 : 0;
                                hand.tracked = motion.swing.hands[from].tracked;
                                hand.attachedAt = rope.key;
                                hand.anchor = trail ? trailEnds[from] : swingState.webs[from].anchor;
                                hand.wrist = webShooter(motion.grips[from], from).position;
                                // A web round edges: to where it sticks, round each of them. The
                                // swing's anchor is the nearest edge, and drawn to it the web's end
                                // jumped to each new one. Where Spidy cannot lay the rope's points
                                // (no hand points hook) it stays drawn to that edge, not straight
                                // through the building to its end.
                                if (const auto& edges = swingBends.hands[from];
                                    hand.attached == 1 && !grabPhases[from] && edges.count &&
                                    finite(edges.end) && native_webs::drawsHeld()) {
                                    hand.anchor = edges.end;
                                    bent[i].count =
                                        std::min<uint32_t>(edges.count, native_webs::maxRopeBends);
                                    for (uint32_t k = 0; k < bent[i].count; ++k)
                                        bent[i].points[k] = edges.points[k];
                                }
                            }
                            // One web in both hands: its rope comes to the upper
                            // wrist from the lower one, in the game's own material.
                            if (const int upper = webRopes.upper(); upper >= 0) {
                                const auto lower = static_cast<unsigned>(1 - upper);
                                const int slot = webRopes.slot(lower);
                                if (slot >= 0 && motion.swing.hands[lower].tracked) {
                                    const Vec3 wrist = webShooter(motion.grips[lower], lower).position;
                                    webs.held.rope = static_cast<uint32_t>(slot) + 1;
                                    webs.held.wrist = wrist;
                                    webs.held.slack =
                                        std::max(swingState.webs[lower].length - swingState.webs[upper].length -
                                                     length(webs.hands[slot].wrist - wrist),
                                                 0.f);
                                }
                            }
                            native_webs::submit(webs, 200, bent);
                        }
                    }
                    const bool steering = motion.active && !flatScreen && controlsVisible;
                    motion.nativeKeys = airJump.update(
                        motion.nativeKeys, game_swing::airborne(swingState, now.QuadPart, frequency.QuadPart));
                    // Going up onto a wall (game_swing::Data::mount), the game
                    // gets the jump alone: the stick walked the player on into
                    // the wall, and into the game's own crawl.
                    const bool mounting =
                        swingState.status && swingState.mount && swingState.qpc &&
                        static_cast<int64_t>(swingState.qpc) > now.QuadPart - frequency.QuadPart / 10;
                    auto keys = swingNativeKeys(
                        steering, swingState.owned != 0 || mounting, motion.nativeKeys,
                        static_cast<SwingTakeoff::Phase>(swingState.takeoffPhase), swingState.takeoff != 0);
                    // A held on the ground charges a leap, the swing's own, let
                    // go of with A. A tap is the jump as before, made as it is
                    // let go. The swing's own jumps (a takeoff, a wall, the
                    // leap's) are the game's A as they are.
                    const bool standing = steering && padInstalled && swingState.status && swingState.grounded &&
                                          !swingState.owned && !mounting && !swingState.takeoff &&
                                          !swingState.takeoffPhase && !swingState.webs[0].attached &&
                                          !swingState.webs[1].attached && !calibrationInput;
                    const auto leap = chargeJump.update(
                        frame.seconds, (keys & swingJumpKey) && !swingState.takeoff, standing);
                    if (!swingState.takeoff)
                        keys = (keys & ~swingJumpKey) | (leap.jump ? swingJumpKey : 0u);
                    leapOut = leap.leap;
                    if (chargeJump.charges() != chargesBefore) {
                        chargesBefore = chargeJump.charges();
                        runtime.haptic(1, .3f);
                        feel(body_haptics::Kind::leapCharge);
                    } else if (chargeJump.charging()) {
                        // It builds in both hands, and holds at full.
                        const float charge = chargeJump.charge();
                        runtime.haptic(0, .08f + .3f * charge);
                        runtime.haptic(1, .1f + .4f * charge);
                    }
                    if (chargeJump.leaps() != leapsBefore) {
                        leapsBefore = chargeJump.leaps();
                        runtime.haptic(0, .8f, .12f);
                        runtime.haptic(1, 1.f, .12f);
                        feel(body_haptics::Kind::leap);
                    }
                    controls(keys);
                    askingGame = false;
                    if (padInstalled) {
                        // The same walking and jumping on the virtual Xbox
                        // controller: after the VR menus, the game plays the
                        // player with it and ignores the bridge's keys.
                        game_pad::Walk walk;
                        const bool playing = steering && !swingState.owned;
                        // Reaching for a thing is not walking: a player standing on a
                        // wall does it as much as one on the ground. The swing owns
                        // him there (it has taken the walls since October 10), so
                        // `playing` was false for the whole climb and the game's Y
                        // never went out. The user, October 13, at a closed vent
                        // reached by walking up the wall it is on: "its right in from
                        // of me and its closed and i cant iteract with it". That
                        // session's report has the swing owning the player in every
                        // one of its 1106 samples on a wall, none of them grounded,
                        // and the game's Y in 2 of 4230 samples.
                        const bool onWall = swingState.wall != 0;
                        const bool reaching =
                            steering && (!swingState.owned || onWall) && !calibrationInput;
                        // And in flight, the swing carrying him: the game's zip to a
                        // point (the stick up) and its web strike (Y, a car's in a
                        // chase) go out there too since October 18, and the swing
                        // lets the game have him once its move has begun
                        // (game_swing::AirYield). Held back until then: a hand-over
                        // in mid-flight dropped players at the game's fall speed.
                        const bool inFlight = steering && swingState.owned && !onWall && !calibrationInput;
                        // Clicking both thumbsticks latches the whole pad, for the
                        // controls play has no room for (the game's X, its L3, each
                        // bumper and trigger on its own, three ways of its D-pad).
                        // Neither stick's own press is half of the chord (PullPress,
                        // slow_motion's StickPress both drop one), so nothing else
                        // answers it. A press of any kind keeps the layer open.
                        const bool layerBusy =
                            frame.buttons != 0 || frame.hands[0].squeeze > .6f ||
                            frame.hands[1].squeeze > .6f || frame.hands[0].trigger > .6f ||
                            frame.hands[1].trigger > .6f ||
                            std::hypot(frame.hands[1].stickX, frame.hands[1].stickY) > .5f;
                        const bool layerOn =
                            padLayer.update(frame.seconds, playing && !calibrationInput,
                                            frame.hands[0].stickClick, frame.hands[1].stickClick, layerBusy);
                        if (padLayer.entries() != padEntriesBefore) {
                            padEntriesBefore = padLayer.entries();
                            runtime.haptic(0, .5f);
                            runtime.haptic(1, .5f);
                            message(3, 0, "Pad layer on - your grips are the game's LB and RB, your triggers its "
                                          "LT and RT, X its X, the right stick its D-pad; click both sticks "
                                          "again to leave it");
                        } else if (padLayerWas && !layerOn) {
                            runtime.haptic(0, .25f);
                            runtime.haptic(1, .25f);
                            message(3, 0, "Pad layer off - the webs are back on your grips and triggers");
                        }
                        padLayerWas = layerOn;
                        if (playing) {
                            walk.right = mounting ? 0 : motion.walkRight;
                            walk.forward = mounting ? 0 : motion.walkForward;
                            // Hand pulls become ordinary native crawl input. The
                            // game keeps vent collision and its supported state.
                            const bool handCrawl = motion.ventCrawl && values.handClimb && motion.active &&
                                !flatScreen && !calibrationInput && !layerOn && !mounting &&
                                std::hypot(motion.walkRight, motion.walkForward) <= .2f;
                            const Vec3 pull =
                                game_swing::crawlPull(climbState, handCrawl, frame.predictedDisplayTime);
                            if (handCrawl && climbState.crawl && climbState.hands) {
                                const Vec3 stick =
                                    game_swing::crawlStick(pull, motion.walkForwardAxis,
                                                          motion.walkRightAxis);
                                walk.right = stick.x;
                                walk.forward = stick.z;
                            }
                            if (motion.ventCrawl != ventCrawlBefore) {
                                ventCrawlBefore = motion.ventCrawl;
                                message(1, 0, ventCrawlBefore ?
                                                  "Vent hand crawling active (native crawl, close view)" :
                                                  "Vent hand crawling ended");
                            }
                            // On the game's own crawl, with the view turned
                            // onto the wall, what the game makes of that stick
                            // is the one thing no session has measured.
                            if (hold.crawl && motion.standing) {
                                ++crawlStood;
                                crawlStick.update(hold.normal, motion.walkForwardAxis, walk.right, walk.forward,
                                                  swingState.velocity);
                            }
                            // Y is the game's Y: interact, or web strike in a fight.
                            // B is its B: the dodge, the way the stick points. Not
                            // while a web carries the player, nor while B skips the
                            // calibration.
                            walk.dodge = (origins.play() & buttonB) && !calibrationInput;
                        }
                        // What each hand points at among the game's interactions near
                        // the player, on Spidy's wall only: off it the game offers its
                        // own, and its interact uses them. Read every frame all the
                        // same: a use goes on once the game's crawl has him.
                        const bool onOwnWall =
                            swingState.status && swingState.wall == static_cast<uint32_t>(game_swing::Surface::wall);
                        const bool offersRead = sampleOffers && !sampleOffers(&offers) && offers.status == 1;
                        std::array<offer_pointing::Offer, game_offers::maxOffers> candidates{};
                        unsigned candidateCount{};
                        if (offersRead && onOwnWall)
                            for (uint32_t k = 0; k < offers.offers && k < game_offers::maxOffers; ++k) {
                                const auto& o = offers.nearby[k];
                                candidates[candidateCount++] = offer_pointing::fromGame(
                                    o.handle, o.position, o.reasons, o.enabled != 0, o.reachable != 0);
                            }
                        offersSeen = std::max(offersSeen, candidateCount);
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& hand = motion.swing.hands[i];
                            pointing[i] = std::nullopt;
                            if (steering && !calibrationInput && candidateCount)
                                pointing[i] = offer_pointing::pick(
                                    {hand.tracked, hand.aim.position, hand.aim.orientation.rotate({0, 0, -1})},
                                    candidates.data(), candidateCount);
                            // A tick in the hand as it comes onto one it can use.
                            const uint32_t usable = pointing[i] && pointing[i]->usable ? pointing[i]->handle : 0;
                            if (usable && usable != pointedBefore[i])
                                runtime.haptic(static_cast<int>(i), .25f);
                            pointedBefore[i] = usable;
                        }
                        const auto pointed = offer_pointing::either(pointing[0], pointing[1]);
                        // The game tries the wall round the one pointed at for a
                        // spot within reach (game_offers::aim).
                        if (uint32_t aim = pointed ? pointed->handle : 0; aimOffer && aim != offerAimed) {
                            offerAimed = aim;
                            aimOffer(&aim);
                        }
                        offerPointFrames += pointed && pointed->usable;
                        offerReachFrames += pointed && !pointed->usable;
                        // Y reaches for a thing wherever the player stands, a wall
                        // included; the step up to it below stays on the ground. On
                        // Spidy's wall at one of the game's interactions a hand points
                        // at, it is that one's use: the game's crawl under the player
                        // there, then the game's interact for him.
                        // A Y the game names goes out as it is (NamedPress), with no
                        // turn or step of the interact's.
                        const uint32_t named = namedPress.claimed();
                        walk.interact = offerUse.update(
                            frame.seconds,
                            (reaching || inFlight) && (origins.play() & buttonY) != 0 &&
                                !(named & game_pad::NamedPress::controlY),
                            onOwnWall, pointed,
                            {offers.usesTaken, offers.usesDeclined, offers.best},
                            [&](uint32_t handle) { return useOffer && !useOffer(&handle); });
                        if (offerUse.asks() != offerAsksBefore) {
                            offerAsksBefore = offerUse.asks();
                            runtime.haptic(0, .4f);
                            runtime.haptic(1, .4f);
                        }
                        if (offerUse.failures() != offerFailuresBefore) {
                            offerFailuresBefore = offerUse.failures();
                            message(3, 0, "The game would not take that from here - come closer to it and press "
                                          "again");
                        }
                        // A press of Y turns the hero to where the player looks; held
                        // on, he walks there, until the game takes the press. The
                        // player's own stick goes first.
                        const bool fight = moverTable == config.base + bridge::combatCamera;
                        const float stepIn = interactStep.update(
                            frame.seconds, walk.interact,
                            playing && !mounting && swingState.grounded && !fight && !motion.standing,
                            std::hypot(swingState.velocity.x, swingState.velocity.z));
                        if (interactStep.steps() != interactStepsBefore) {
                            interactStepsBefore = interactStep.steps();
                            runtime.haptic(0, .2f);
                        }
                        // The game's Y: the player's, and after a tap the time the
                        // hero takes to turn to what they look at.
                        walk.interact = interactStep.pressed();
                        // What the game names now, for a pull (below). Not in the pad
                        // layer: there the player presses the game's own buttons.
                        const auto asked = layerOn ? prompt_buttons::Demand{} : game_prompts::demand();
                        // The game's throw of a prop where the hand X is not on
                        // points (throw_aim.hpp, game_throw): while X sends the
                        // game's LB + RB, at the enemy that hand's aim marker is
                        // on, or the way from the player to what it points at.
                        // The user, October 24: "really hard or not possible to
                        // aim where the throw will go".
                        {
                            const bool pair = (asked.buttons & game_pad::bumpers) == game_pad::bumpers;
                            const bool holding = playing && pair && (named & game_pad::NamedPress::controlX);
                            const float dt = std::isfinite(frame.seconds) ? std::clamp(frame.seconds, 0.f, .1f) : 0.f;
                            throwAimTail = holding ? 1.f : std::max(0.f, throwAimTail - dt);
                            if (aimGameThrow && throwAimTail > 0) {
                                const unsigned xHand =
                                    runtime.controllers()[1] == ControllerKind::frame ? 1u : 0u;
                                const unsigned hand = 1 - xHand;
                                auto request = throw_aim::fromHand(
                                    aimed.hands[hand], motion.swing.hands[hand].aim.orientation.rotate({0, 0, -1}),
                                    swingState.position);
                                aimGameThrow(&request);
                            }
                        }
                        // The game takes its own interact away the moment the
                        // player leaves the surface, and the swing's wall is
                        // the player off it: hand him back for the press. Only
                        // where the game is offering nothing, which is the one
                        // case the press does nothing at all today.
                        const bool handing = wallHandover.update(
                            frame.seconds, walk.interact,
                            swingState.wall == static_cast<uint32_t>(game_swing::Surface::wall),
                            game_prompts::live().demand != 0);
                        // The walls are NOT let go of yet, and October 13 is why:
                        // they were, and the player fell. Leaving the wall takes
                        // the swing's hold off a hero the game still has in its
                        // air state, and the game's own crawl does not pick him
                        // up where he stands - it takes him when a stick pushes
                        // him into a wall, which is the half this does not do.
                        // Until that half is built and measured, the rule only
                        // counts: `wall_handovers` says how often a player asked
                        // to interact on a wall of the swing's with the game
                        // offering nothing, which is the size of the thing to
                        // fix. At a vent today the answer is wall running off.
                        wallsHandedBack = handing;
                        (void)swingSettingsSent;
                        (void)toldHandoverRefused;
                        // Not on a wall the player stands on: the game's camera
                        // there is not the follow camera's level turn.
                        const bool cameraSteered = steering && !motion.onSurface;
                        // EYE AIM's zips (game_pad::EyeZip): from the stick going up
                        // for a zip to the zip's end, the camera is brought closely
                        // onto what the eyes look at, from where it stands - the point
                        // the game zips to. Not while basic flips have the stick.
                        const auto& zipStick = frame.hands[1];
                        const bool zipWanted =
                            zipPress.active() || (zipStick.stickY >= game_pad::StickPush::pushed &&
                                                  std::abs(zipStick.stickX) <= game_pad::StickPush::aside);
                        const Vec3 gazeLook = motion.gazePose.orientation.rotate({0, 0, -1});
                        const bool eyesZip = (values.eyeAim & vr_settings::eyeAimZips) && motion.gazeValid &&
                                             finite(gazeLook) && length(gazeLook) > .5f && zipWanted &&
                                             (!motion.basicFlipControls || zipPriority || zipPress.active());
                        const auto& seen = aimed.gaze;
                        const Vec3 cameraLookTo =
                            eyesZip ? game_pad::cameraLookAt(
                                          {camera[12], camera[13], camera[14]}, seen.point,
                                          seen.kind == static_cast<uint32_t>(game_swing::AimKind::anchor) ||
                                              seen.kind == static_cast<uint32_t>(game_swing::AimKind::air),
                                          normalized(gazeLook))
                                    : motion.headPose.orientation.rotate({0, 0, -1});
                        walk.look = cameraLook.update(cameraLookTo, {camera[8], camera[9], camera[10]}, cameraSteered,
                                                      eyesZip);
                        walk.lookUp = cameraPitch.update(cameraLookTo, {camera[8], camera[9], camera[10]},
                                                         cameraSteered, eyesZip);
                        if (stepIn > 0 && std::hypot(walk.right, walk.forward) < .1f) {
                            Vec3 look = motion.headPose.orientation.rotate({0, 0, -1});
                            Vec3 ahead{camera[8], 0, camera[10]};
                            look.y = 0;
                            // Looking straight down or up says no way to go.
                            if (length(look) > .3f && length(ahead) > .3f) {
                                look = normalized(look);
                                ahead = normalized(ahead);
                                walk.forward = stepIn * dot(look, ahead);
                                walk.right = stepIn * dot(look, cross(ahead, {0, 1, 0}));
                            }
                        }
                        // While a use is under way the game's crawl is where the
                        // offer was checked: the stick does not walk him off it.
                        if (offerUse.busy())
                            walk.right = walk.forward = 0;
                        // The game's LB + RB, its web pull where it shows the two
                        // (a door, a grate): both web buttons on the thing, which
                        // no web holds on, and a yank. X sends them too, where
                        // the game names them (NamedPress, above).
                        std::array<WebPullHand, 2> pulling{};
                        bool free = swingStarted && swingState.grounded && !calibrationInput;
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& hand = motion.swing.hands[i];
                            const auto& aim = aimed.hands[i];
                            free = free && !swingState.webs[i].attached && !grabPhases[i] &&
                                   !(motion.ventCrawl && climbState.status && (climbState.hands & (1u << i)));
                            pulling[i] = {hand.tracked && hand.grip > .5f,
                                          aim.kind == static_cast<uint32_t>(game_swing::AimKind::blocked), aim.point,
                                          hand.aim.position, hand.gripRelativeToHead};
                        }
                        const bool yanked =
                            webPull.update(frame.seconds, playing && free, trackingTurn(motion.swing), pulling);
                        if (webPull.pulls() != pullsBefore) {
                            pullsBefore = webPull.pulls();
                            runtime.haptic(0, 1);
                            runtime.haptic(1, 1);
                            feel(body_haptics::Kind::webPull);
                            message(3, 0, "Web pull - the game's LB + RB, for as long as you hold both web buttons");
                        }
                        const bool pulled = yanked && playing;
                        // A yank at a thing the game is prompting both triggers for
                        // sends those (its LT + RT for a vent as readily as its
                        // LB + RB for a door): the gesture is the player's first way
                        // in. With nothing of the kind named it is the pull it was.
                        if (pulled && asked.leftTrigger && asked.rightTrigger)
                            walk.demand = {0, true, true};
                        else
                            walk.pull = pulled;
                        if (walk.pull && !pulling_)
                            game_prompts::note(onWall ? "the player's pull -> the game's LB + RB, on a wall"
                                                      : "the player's pull -> the game's LB + RB");
                        pulling_ = walk.pull;
                        // Outside airborne basic flips, the right stick down heals,
                        // the ground; pushed up it is the game's LT + RT, while
                        // a web carries the player too (inFlight). Left and
                        // right turn.
                        // The stick as it is, whether or not the controller's pose is tracked: a pose
                        // lost for a moment (the controller out of the cameras' sight, as it is held
                        // down by the hip) took the stick to rest and spent a heal under the thumb.
                        const auto& turning = frame.hands[1];
                        // Not a push the game names a D-pad way for (NamedPress).
                        const bool flipZip = motion.basicFlipControls && (zipPriority || zipPress.active());
                        if (flipZipPriority.began()) stickPush.update(0, true, 0, 0);
                        const bool sticking =
                            steering && !calibrationInput && !layerOn &&
                            (!motion.basicFlipControls || flipZip) &&
                            !(named & (game_pad::NamedPress::stickUp | game_pad::NamedPress::stickLeft |
                                       game_pad::NamedPress::stickRight));
                        const auto push = motion.basicFlipControls && !flipZip ?
                            game_pad::StickPush::Way::none :
                            stickPush.update(frame.seconds, sticking, turning.stickX, turning.stickY);
                        if (motion.basicFlipControls && !flipZip) {
                            stickPush.cancel();
                            zipPress.cancel();
                        }
                        walk.heal = !motion.basicFlipControls && push == game_pad::StickPush::Way::down;
                        // Held on into the game's aim mode (its own time slowed), LT stays down alone,
                        // and letting go zips to the point its aim holds (ZipPress).
                        // With EYE AIM's zips, not before the game's camera is on the
                        // eyes' point (EyeZip; a moment at most).
                        const bool zipUp = eyeZip.update(
                            frame.seconds, (reaching || inFlight) && push == game_pad::StickPush::Way::up,
                            eyesZip && cameraSteered, cameraLook.error(), cameraPitch.error());
                        const auto gameTime = game_time::telemetry();
                        const bool slow = gameTime.installed && gameTime.own < game_pad::ZipPress::slowBelow;
                        const auto zipKeys = zipPress.update(frame.seconds, zipUp, sticking, slow);
                        walk.zip = zipKeys.zip;
                        walk.aim = zipKeys.aim;
                        if ((walk.heal && !healing) || (walk.zip && !zipping))
                            runtime.haptic(1, .2f);
                        if (walk.heal && !healing)
                            feel(body_haptics::Kind::heal);
                        if (walk.zip && !zipping)
                            feel(body_haptics::Kind::zip);
                        // The swing hears of it with the next command: the zip from its own wall
                        // too, where Y is the wall's interactions' (offerUse).
                        // And of any button the game names, pressed in flight: the
                        // dive it asks for, its web swing, its Ground Strike.
                        askingGame = steering && swingState.owned && !calibrationInput &&
                                     (walk.zip ||
                                      (inFlight && (walk.interact || walk.demand.any() || named != 0)));
                        walk.jump = (keys & swingJumpKey) != 0;
                        // A jump on the game's own crawl - the player's, a web's
                        // takeoff, or the mount's that walks him on from it onto
                        // Spidy's wall - goes out as its RT + A, the only one
                        // that leaves the wall; A alone leaps along it. Only
                        // while the crawl holds him: RT held on in the air is
                        // the game's own web swing.
                        //
                        // From the moment a web holds, too, so the RT is down
                        // before the takeoff's A: the same RT + A pressed at
                        // once just after a web took hold was ignored the first
                        // time, and only the takeoff's retry 0.6 s later left
                        // the wall (October 14). RT alone on the crawl does
                        // nothing, so holding it early costs nothing.
                        //
                        // No longer up the wall with the stick: that was the
                        // game's own wall run (October 14), and since October 15
                        // the walls are Spidy's alone - the stick on the game's
                        // crawl walks the player on, onto them.
                        {
                            const bool onCrawl = swingState.status &&
                                                 swingState.wall == static_cast<uint32_t>(game_swing::Surface::game);
                            const bool webbed = swingState.webs[0].attached || swingState.webs[1].attached;
                            walk.parkour = onCrawl && (walk.jump || (!motion.ventCrawl && webbed));
                        }
                        walk.scan = templePress.held() && steering;
                        walk.suit = suitPress.held() && steering;
                        walk.menu = menuTap > 0;
                        // Out of a warned attack's way by moving: a quick step or
                        // lean across the attacker's line, a step back or a duck is
                        // the game's B, the way the head went (a duck: the game's
                        // own way). Where B may go out, as B itself.
                        {
                            std::array<DodgeThreat, 6> warned{};
                            size_t count{};
                            for (const auto& m : senseMarks)
                                if (m.attacker && m.live && !m.brokenAt && count < warned.size())
                                    warned[count++] = {m.attacker, m.head, m.answer};
                            const Quat roomToWorld = motion.headPose.orientation * frame.head.orientation.conjugate();
                            const bool dodgePlay = playing && !calibrationInput && validTrackedPose(frame.head);
                            if (moveDodge.update(frame.seconds, dodgePlay, motion.headPose.position, frame.head.position,
                                                 roomToWorld, {warned.data(), count})) {
                                const Vec3 way = moveDodge.way();
                                walk.dodge = true;
                                walk.right = dot(way, motion.walkRightAxis);
                                walk.forward = dot(way, motion.walkForwardAxis);
                            }
                        }
                        // A perfect dodge: the game slows its own time within a
                        // moment of a B that went down with it at full speed.
                        if (walk.dodge && !dodging) {
                            dodgeWatch = game_time::telemetry().own > .95f ? .7f : 0.f;
                            // A whoosh across the body the way the stick sends him.
                            dodgeFrom = walk.right > 0 ? -1.f : 1.f;
                            feel(body_haptics::Kind::dodge, -1, 1, dodgeFrom);
                        }
                        // A dodge while a warning stands: the answer a red mark asks for.
                        if (walk.dodge && !dodging &&
                            std::any_of(senseMarks.begin(), senseMarks.end(),
                                        [](const SenseMark& m) { return m.live && !m.brokenAt; }))
                            ++senseDodges;
                        if (dodgeWatch > 0) {
                            dodgeWatch -= std::isfinite(frame.seconds) ? std::clamp(frame.seconds, 0.f, .1f) : .1f;
                            if (const auto time = game_time::telemetry(); time.installed && time.own < .9f) {
                                dodgeWatch = 0;
                                moments.start(Moment::dodge);
                                runtime.haptic(0, .5f);
                                runtime.haptic(1, .5f);
                                feel(body_haptics::Kind::perfectDodge, -1, 1, dodgeFrom);
                            }
                        }
                        auto buttons = origins.filtered(frame, mapping);
                        // On the pad layer a grip pressed at a back pocket is the
                        // phone's draw, not the game's bumper.
                        if (mapping == game_pad::Mapping::pad)
                            for (unsigned i = 0; i < 2; ++i)
                                if (pocketDraw.hands() & (1u << i))
                                    buttons.hands[i].squeeze = 0;
                        // An X the game names is its X: no wheel (NamedPress).
                        if (named & game_pad::NamedPress::controlX)
                            buttons.buttons &= ~static_cast<uint32_t>(buttonX);
                        // The gadget wheel: X held opens it in front of the hand
                        // X is on (the left on Touch and Index, the right on the
                        // Steam Frame); X tapped does nothing (the game menu until
                        // October 23: the phone's MENU now). Chosen, a gadget is
                        // equipped in the game and the free hand's trigger fires
                        // it; the web shooter at the top has it fire web balls
                        // again. A game that offers no gadget yet (a new game's
                        // first missions: the user's second profile, 2% in, on
                        // October 24) still opens it, the web shooter alone and
                        // NO GADGETS YET in the middle: until then X held did
                        // nothing at all, and that read as a broken wheel.
                        {
                            const unsigned hand = runtime.controllers()[1] == ControllerKind::frame ? 1u : 0u;
                            const uint32_t owned = std::min(listed.gadgetCount, 8u);
                            uint8_t places = 1;
                            for (uint32_t g = 0; g < owned; ++g)
                                if (listed.gadgets[g].place < gadgetPlaces)
                                    places |= static_cast<uint8_t>(1u << listed.gadgets[g].place);
                            const bool offered = wheelShooter && shooterStarted && !listed.gadgetError &&
                                                 mapping == game_pad::Mapping::gameplay;
                            const bool x = (buttons.buttons & buttonX) != 0;
                            if (offered)
                                buttons.buttons &= ~static_cast<uint32_t>(buttonX);
                            const auto chosen =
                                gadgetWheel.update(frame.seconds, x, offered && steering && !calibrationInput,
                                                   frame.hands[hand].grip.position, frame.head.position, places);
                            if (chosen.opened) {
                                runtime.haptic(hand, .3f);
                                feel(body_haptics::Kind::gadget, static_cast<int>(hand), .3f);
                            }
                            if (chosen.lit) {
                                runtime.haptic(hand, .12f);
                                feel(body_haptics::Kind::gadget, static_cast<int>(hand), .15f);
                            }
                            if (chosen.chosen >= 0) {
                                runtime.haptic(hand, .5f);
                                feel(body_haptics::Kind::gadget, static_cast<int>(hand), .5f);
                                game_shooter::GadgetCommand command;
                                for (uint32_t g = 0; g < owned; ++g)
                                    if (listed.gadgets[g].place == chosen.chosen)
                                        command.equip = listed.gadgets[g].weapon;
                                gadgetFire = command.equip != 0;
                                command.fire = gadgetFire ? 1u : 0u;
                                wheelShooter(&command);
                            }
                        }
                        auto pad = game_pad::fromControllers(buttons, mapping, walk);
                        // What the controls of the names the game shows send it.
                        if (mapping == game_pad::Mapping::gameplay) {
                            pad.buttons |= namedPad.buttons;
                            pad.leftTrigger = std::max(pad.leftTrigger, namedPad.leftTrigger);
                            pad.rightTrigger = std::max(pad.rightTrigger, namedPad.rightTrigger);
                        }
                        if (namedPress.presses() != namedPressesBefore) {
                            namedPressesBefore = namedPress.presses();
                            const bool slam = namedPress.slams() != slamsBefore;
                            slamsBefore = namedPress.slams();
                            char line[96];
                            sprintf_s(line, "the player's press of a button the game names -> 0x%04x%s%s%s",
                                      namedPad.buttons, namedPad.leftTrigger ? " LT" : "",
                                      namedPad.rightTrigger ? " RT" : "",
                                      slam ? ", both fists slammed down"
                                      : named & game_pad::NamedPress::fist ? ", a thrown fist"
                                                                           : "");
                            game_prompts::note(line);
                        }
                        if (menuTap > 0)
                            menuTap -= std::isfinite(frame.seconds) ? std::clamp(frame.seconds, 0.f, .1f) : .1f;
                        // The phone's CAMERA, as its own D-pad up would: the
                        // game's camera out (its overlay up, the triggers then
                        // its aim and shutter: prompt_buttons::cameraDemand).
                        if (cameraTap > 0) {
                            pad.buttons |= game_pad::dpadUp;
                            cameraTap -= std::isfinite(frame.seconds) ? std::clamp(frame.seconds, 0.f, .1f) : .1f;
                        }
                        screenControls.apply(frame.seconds,
                                             flatScreen && motion.active && mapping == game_pad::Mapping::menus,
                                             buttons, pad);
                        // Both thumbsticks held in pause the game in every mapping, the menu button as
                        // before: under SteamVR a Quest's menu button opens SteamVR's dashboard instead.
                        if (pausePress.update(frame.seconds, mapping != game_pad::Mapping::none,
                                              frame.hands[0].stickClick, frame.hands[1].stickClick))
                            pad.buttons |= game_pad::start;
                        if (pausePress.spent())
                            pad.buttons &=
                                static_cast<uint16_t>(~(game_pad::leftThumb | game_pad::rightThumb));
                        if (pausePress.pauses() != pausesBefore) {
                            pausesBefore = pausePress.pauses();
                            runtime.haptic(0, .4f);
                            runtime.haptic(1, .4f);
                        }
                        game_pad::submit(pad, 200);
                        if (walk.interact && !interacting) {
                            ++interacts;
                            wallInteracts += onWall;
                            char line[96];
                            sprintf_s(line, "the player's Y -> the game%s%s", onWall ? ", on a wall" : "",
                                      walk.demand.any() ? ", carrying a prompt's demand" : "");
                            game_prompts::note(line);
                        }
                        interacting = walk.interact;
                        dodges += walk.dodge && !dodging;
                        dodging = walk.dodge;
                        heals += walk.heal && !healing;
                        healing = walk.heal;
                        zips += walk.zip && !zipping;
                        zipping = walk.zip;
                    }
                    // Body haptics: what goes on, every frame, in VR play only (a
                    // menu, a scene, the game screen: all quiet).
                    {
                        body_haptics::State felt;
                        felt.live = immersive;
                        felt.groups = hapticGroups();
                        // The room's turn into the world: the world's head over the room's.
                        if (validTrackedPose(frame.head) && validTrackedPose(motion.headPose))
                            torso = facing.update(frame.head.orientation,
                                                  motion.headPose.orientation * frame.head.orientation.conjugate(),
                                                  frame.seconds);
                        const bool fresh = swingStarted && swingState.status && swingState.qpc &&
                                           static_cast<int64_t>(swingState.qpc) > now.QuadPart - frequency.QuadPart / 10 &&
                                           finite(swingState.velocity);
                        felt.airborne = fresh && game_swing::airborne(swingState, now.QuadPart, frequency.QuadPart);
                        felt.onWall = fresh && swingState.wall != 0;
                        if (fresh)
                            felt.velocity = torso.local(swingState.velocity);
                        for (unsigned i = 0; i < 2; ++i) {
                            const bool grabbing = grabPhases[i] != 0;
                            felt.webTension[i] = fresh && attachedBefore[i] && !grabbing ? swingState.webs[i].tension : 0.f;
                            felt.grabTension[i] = grabbing ? grabState.hands[i].tension : 0.f;
                        }
                        felt.charge = chargeJump.charging() ? chargeJump.charge() : 0.f;
                        felt.slowMotion = slowMotion.active();
                        // A fall in the hero's hit points is a blow: from the attack
                        // the game warned of last, within a second, else on the chest
                        // and the back at once.
                        float hitPoints = std::numeric_limits<float>::quiet_NaN();
                        if (const auto h = players.health(); h && player.hero)
                            read(h + game_player::hitPointsAt, &hitPoints, sizeof(hitPoints));
                        const float lost = heroHealth.update(hitPoints);
                        felt.health = heroHealth.fraction();
                        if (lost > 0 && immersive) {
                            ++hits;
                            const float strength = std::clamp(lost * 4, 0.f, 1.f);
                            const Vec3 to = blowFrom - (motion.headPose.position - Vec3{0, .4f, 0});
                            if (blowSeen && frame.predictedDisplayTime - blowSeen < 1'000'000'000 && length(to) > .1f) {
                                ++aimedHits;
                                feelFrom(body_haptics::Kind::hit, -1, strength, to);
                            } else {
                                feel(body_haptics::Kind::hit, -1, strength);
                            }
                        }
                        const float landed =
                            landingWatch.update(frame.seconds, felt.airborne, fresh ? -swingState.velocity.y : 0.f);
                        if (landed > 0 && immersive) {
                            ++landings;
                            feel(body_haptics::Kind::landing, -1, landed);
                        }
                        haptics.set(felt);
                        // The player hears once what the devices do: on, and which; or
                        // why not. Without a Player installed (most players), nothing.
                        const auto told = haptics.counters();
                        if (told.status != hapticsTold ||
                            (told.status == bhaptics::Status::playing && told.devices != devicesTold)) {
                            std::string text;
                            switch (told.status) {
                            case bhaptics::Status::playing:
                                text = "Body haptics on: " + bhaptics::deviceNames(told.devices);
                                break;
                            case bhaptics::Status::connected:
                                text = "Body haptics: the bHaptics Player has no device on - pair them in it";
                                break;
                            case bhaptics::Status::noLibrary:
                                text = "Body haptics: bhaptics_library.dll is missing beside Spidy's modules";
                                break;
                            case bhaptics::Status::noApp:
                                text = "Body haptics: this build of Spidy has no bHaptics app";
                                break;
                            case bhaptics::Status::failed:
                                text = "Body haptics: the bHaptics SDK would not start";
                                break;
                            case bhaptics::Status::rejected:
                                text = "Body haptics: the bHaptics Player did not take this build's bHaptics app";
                                break;
                            default:
                                break;
                            }
                            if (!text.empty())
                                message(3, 0, text.c_str());
                            hapticsTold = told.status;
                            devicesTold = told.devices;
                        }
                        // Installed but not answering for ten seconds: not running.
                        if (told.status == bhaptics::Status::waiting && hapticsSince &&
                            GetTickCount64() - hapticsSince > 10000) {
                            hapticsSince = 0;
                            message(3, 0, "Body haptics: start the bHaptics Player to feel webs, blows and "
                                          "Spider-Sense on your vest - it connects by itself");
                        }
                    }
                    {
                        std::lock_guard lock(telemetry);
                        auto& d = SpidyXrData;
                        InterlockedIncrement64(&d.sequence);
                        ++d.frames;
                        d.tracked += motion.active;
                        d.leftHands += frame.hands[0].valid;
                        d.rightHands += frame.hands[1].valid;
                        d.nativeKeys = keys;
                        d.gate = gate;
                        d.cameraMover = moverTable > config.base && moverTable - config.base < 0x10000000
                                            ? static_cast<uint32_t>(moverTable - config.base)
                                            : 0;
                        d.players = playersFound;
                        d.playerRecord = player.record;
                        const auto pad = game_pad::telemetry();
                        d.padButtons = pad.buttons;
                        d.padInstalled = pad.installed;
                        d.padReads = pad.reads;
                        d.cameraCommits = game.cameraCalls;
                        d.playerCommits = game.matched;
                        d.interacts = interacts;
                        d.dodges = dodges;
                        d.heals = heals;
                        d.zips = zips;
                        d.zipReleases = zipPress.releases();
                        d.aimMarkers = values.aimMarkers;
                        d.settings = (values.webGrab ? 1u : 0u) | (values.punch ? 2u : 0u) | (values.body ? 4u : 0u) |
                                     (values.airWebs ? 8u : 0u) | (values.webShooter ? 16u : 0u) |
                                     (values.flips ? 32u : 0u) | (values.triggerWebs ? 64u : 0u) |
                                     (values.standOnWalls ? 128u : 0u) | (values.arcadeWebs ? 512u : 0u);
                        d.scans = templePress.scans();
                        d.chargeJumps = chargeJump.charges();
                        const auto prompts = game_prompts::live();
                        d.promptInvokes = prompts.invokes;
                        d.promptCalls = prompts.calls;
                        d.promptsRaised = prompts.raised;
                        d.promptsCleared = prompts.cleared;
                        d.promptTexts = prompts.named;
                        d.promptRewrites = prompts.rewritten;
                        d.promptDemand = prompts.demand;
                        d.promptLastDemand = prompts.lastDemand;
                        d.promptBeyondPlay = prompts.beyondPlay;
                        d.promptKeyboardTexts = prompts.keyboardTexts;
                        d.contextPresses = namedPress.presses();
                        d.padEntries = padLayer.entries();
                        d.padLayerNow = padLayer.latched() ? 1u : 0u;
                        d.wallInteracts = wallInteracts;
                        d.wallHandovers = wallHandover.handovers();
                        d.wallHandoversOffered = wallHandover.offered();
                        d.headPushFrames = rig.clearance().pushedFrames();
                        d.headPushMostMm = static_cast<uint32_t>(std::lround(rig.clearance().most() * 1000));
                        d.crawlFrames = crawlFrames;
                        d.crawlStood = crawlStood;
                        d.crawlStickSamples = crawlStick.samples();
                        d.crawlStickTurn = crawlStick.turn();
                        d.crawlStickAgreement = crawlStick.agreement();
                        d.crawlStickSpeed = crawlStick.speed();
                        d.headDroppedFrames = headDroppedFrames;
                        d.headroomLeast = headroomLeast;
                        if (sampleWarp && !sampleWarp(&warp)) {
                            d.crawlAsked = static_cast<uint32_t>(warp.asked);
                            d.crawlApplied = static_cast<uint32_t>(warp.applied);
                            d.crawlRefused = static_cast<uint32_t>(warp.refused);
                            d.crawlFaulted = static_cast<uint32_t>(warp.faulted);
                            d.crawlTaken = static_cast<uint32_t>(warp.taken);
                            d.crawlDeclined = static_cast<uint32_t>(warp.declined);
                            gameRunFrames += (warp.swing & 32u) != 0;
                        }
                        d.gameRunFrames = gameRunFrames;
                        d.holdGraceFrames = holdGraceFrames;
                        d.trackingGapFrames = trackingGapFrames;
                        d.trackingGapReasons = trackingGapReasons;
                        d.floorCrawlFrames = floorCrawlFrames;
                        d.crawlCapFrames = crawlCapFrames;
                        d.kneelFrames = kneelFrames;
                        d.offerPointFrames = offerPointFrames;
                        d.offerReachFrames = offerReachFrames;
                        d.offerAsks = offerUse.asks();
                        d.offerRefusals = offerUse.refusals();
                        d.offerPresses = offerUse.presses();
                        d.offerPicked = offerUse.picked();
                        d.offerFailures = offerUse.failures();
                        d.offersSeen = offersSeen;
                        d.offersState = offers.status | offers.error << 8;
                        d.offersMasked = static_cast<uint32_t>(offers.masked);
                        d.offersPosed = static_cast<uint32_t>(offers.posed);
                        d.offersGathered = offers.gathered;
                        d.offersMostChecked = offers.mostChecked;
                        d.offersSearches = offers.searches;
                        d.offersSpots = offers.spotsFound;
                        d.offersNear = offers.offers;
                        d.offerRetries = offerUse.retries();
                        d.senseShown = senseShown;
                        d.senseFrames = senseFrames;
                        d.senseSightFrames = senseSightFrames;
                        d.senseTingles = senseTingles;
                        d.senseBroken = senseBroken;
                        d.senseDodges = senseDodges;
                        d.suitPowers = suitPress.powers();
                        d.moveDodges = moveDodge.dodges();
                        d.dives = dives;
                        d.ownSounds = ownSounds;
                        d.seated = (values.seated ? 1u : 0u) | (seatLift.measures() ? 2u : 0u);
                        d.heightCm = static_cast<uint32_t>(values.heightCm);
                        d.seatedEyeMm = static_cast<uint32_t>(std::lround(seatLift.seatedEye() * 1000));
                        d.seatedLiftMm = static_cast<uint32_t>(std::lround(seatLift.lift() * 1000));
                        d.seatedMeasures = seatLift.measures();
                        {
                            const auto& p = pocketDraw.stats();
                            d.pocketPresses = p.presses;
                            d.pocketBegun = p.begun;
                            d.pocketDraws = p.draws;
                            d.pocketSlow = p.tooSlow;
                            d.pocketLetGo = p.letGo;
                            for (unsigned m = 0; m < PocketDraw::misses; ++m)
                                d.pocketMissed[m] = p.missed[m];
                            d.pocketLastMiss = p.lastMiss < PocketDraw::misses ? p.lastMiss + 1 : 0;
                            const float hand[3]{p.lastHand.x, p.lastHand.y, p.lastHand.z};
                            for (unsigned a = 0; a < 3; ++a)
                                d.pocketLastHand[a] =
                                    static_cast<int32_t>(std::lround(std::clamp(hand[a], -9.f, 9.f) * 1000));
                            d.pocketLastLostMs = static_cast<uint32_t>(std::lround(p.lastLost * 1000));
                            d.pocketLastLook = static_cast<int32_t>(std::lround(p.lastLook * 1800 / 3.14159265f));
                        }
                        d.eyeGaze = (static_cast<uint32_t>(runtime.eyeTracking()) + 1) |
                                    static_cast<uint32_t>(values.eyeAim) << 8;
                        d.eyeAimFrames = eyeAimFrames;
                        d.gazeFrames = gazeFrames;
                        d.eyeShots = eyeShots;
                        d.eyeZips = eyeZip.settled();
                        d.eyeZipsLate = eyeZip.late();
                        for (unsigned i = 0; i < 3; ++i) {
                            d.cameraForward[i] = camera[8 + i];
                            d.cameraUp[i] = camera[4 + i];
                        }

                        d.vrButtons = trackedFrame.buttons;
                        d.vrButtonsPlayed = vrButtonsPlayed;
                        auto& p = SpidyPromptData;
                        InterlockedIncrement64(&p.sequence);
                        p.invokes = prompts.invokes;
                        p.calls = prompts.calls;
                        p.raised = prompts.raised;
                        p.cleared = prompts.cleared;
                        p.texts = prompts.named;
                        p.rewrites = prompts.rewritten;
                        p.demand = prompts.demand;
                        p.lastDemand = prompts.lastDemand;
                        p.beyondPlay = prompts.beyondPlay;
                        p.keyboardTexts = prompts.keyboardTexts;
                        // A name can join the census with no prompt call of its own, so
                        // the lists are copied out every so often as well as on a call.
                        if (prompts.calls != promptCallsBefore || ++promptRefresh % 64 == 0) {
                            promptCallsBefore = prompts.calls;
                            game_prompts::Calls seen;
                            game_prompts::recentCalls(seen);
                            p.lines = seen.count;
                            std::memcpy(p.perName, seen.perName, sizeof(p.perName));
                            std::memcpy(p.line, seen.line, sizeof(p.line));
                            p.names = seen.names;
                            std::memcpy(p.censusCalls, seen.calls, sizeof(p.censusCalls));
                            std::memcpy(p.example, seen.example, sizeof(p.example));
                        }
                        InterlockedIncrement64(&p.sequence);
                        {
                            const auto felt = haptics.counters();
                            auto& h = SpidyHapticsData;
                            InterlockedIncrement64(&h.sequence);
                            h.status = static_cast<uint32_t>(felt.status);
                            h.level = static_cast<uint32_t>(values.bodyHaptics);
                            h.devices = felt.devices;
                            h.devicesSeen = felt.devicesSeen;
                            h.connects = felt.connects;
                            h.events = felt.events;
                            h.frames = felt.frames;
                            h.refused = felt.refused;
                            h.hits = hits;
                            h.aimedHits = aimedHits;
                            h.landings = landings;
                            h.liveTicks = felt.liveTicks;
                            std::copy(felt.onTicks.begin(), felt.onTicks.end(), h.onTicks);
                            std::copy(felt.peakSum.begin(), felt.peakSum.end(), h.peakSum);
                            std::copy(felt.kinds.begin(), felt.kinds.end(), h.kinds);
                            h.groups = hapticGroups();
                            InterlockedIncrement64(&h.sequence);
                        }
                        const auto controllers = runtime.controllers();
                        d.controllers[0] = static_cast<uint32_t>(controllers[0]);
                        d.controllers[1] = static_cast<uint32_t>(controllers[1]);
                        d.controllerLayouts = runtime.layoutsTaken();
                        d.snapTurn = static_cast<uint32_t>(values.snapTurn);
                        d.smoothTurn = static_cast<uint32_t>(values.smoothTurn);
                        d.haptics = static_cast<uint32_t>(values.haptics);
                        d.screenSize = static_cast<uint32_t>(values.screenSize);
                        d.swingSpeed = values.swingSpeed;
                        d.weight = static_cast<uint32_t>(values.weight);
                        d.hud = static_cast<uint32_t>(values.hud);
                        d.flipSpeed = static_cast<uint32_t>(values.flipSpeed);
                        d.viewTilt = motion.active ? motion.viewTilt * 180 / 3.14159265f : 0.f;
                        d.interactSteps = interactStep.steps();
                        d.lookError = cameraLook.error();
                        d.lookTurns = cameraLook.turns();
                        d.screenDepth = static_cast<uint32_t>(values.screenDepth);
                        d.settingChanges = settingChanges;
                        const auto menu = game_menu::telemetry();
                        d.menuTabs = menu.tabs;
                        d.menuInstalled = menu.installed;
                        d.menuStatus = menuCode ? menuCode : menu.status;
                        d.surfaceEntries = surfaceEntries;
                        d.surfaceFrames = surfaceFrames;
                        std::memcpy(d.heroUp, body.data() + 4, 12);
                        d.standOff = length(motion.standOff);
                        if (motion.active && motion.onSurface) {
                            d.surfaceHeight = motion.surfaceHeight;
                            d.surfaceClearance = motion.surfaceClearance;
                        }
                        const bool calibrating = calibration.phase() != body_calibration::Phase::idle;
                        d.calibrationPhase = static_cast<uint32_t>(calibration.phase());
                        d.calibrationHint = calibrating ? static_cast<uint32_t>(calibration.hint()) : 0u;
                        d.calibrationProgress = calibrating ? calibration.progress() : 0.f;
                        d.eyeHeightMm = static_cast<uint32_t>(std::lround(calibratedEye * 1000));
                        d.armLengthMm = static_cast<uint32_t>(std::lround(calibratedArm * 1000));
                        d.calibrations = calibrations;
                        d.calibrationSkips = calibrationSkips;
                        d.calibrationFlags = calibrationPrompt ? 0u : 1u;
                        d.calibrationReach[0] = lastCalibration.reach[0];
                        d.calibrationReach[1] = lastCalibration.reach[1];
                        std::memcpy(d.head, motion.head.data(), 64);
                        const Mat4 basis = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto pose = native_view::relativePose(basis, motion.hands[i]);
                            std::memcpy(d.hands[i], pose.data(), 64);
                        }
                        InterlockedIncrement64(&d.sequence);
                    }
                    // Copies of the game's presented frame, for the screen.
                    if (gpuStarted)
                        native_gpu::wantScreen(viewing && !vrPlay);
                    showScreen = screen;
                    if (!viewing)
                        return false;
                    if (!gpuStarted) {
                        // From here the headset shows the game: its screen until
                        // gameplay, then the eye views.
                        native_gpu::Config gpu;
                        gpu.pid = config.pid;
                        gpu.base = config.base;
                        gpu.queue = config.queue;
                        gpu.durationMs = moduleDuration;
                        gpu.copyToXr = 2;
                        gpu.captureImages = config.options & 1;
                        check(native_gpu::start(&gpu), "Start native GPU bridge");
                        gpuStarted = true;
                        nativeDeadline =
                            config.durationMs ? GetTickCount64() + config.durationMs : UINT64_MAX;
                        message(3, 0, "Headset active");
                        return false; // initialization can exceed one predicted display interval
                    }
                    // Without gameplay the screen shows the game's presented
                    // frame; until it is up, the last image stays. The eye
                    // views get no command and stop rendering meanwhile.
                    if (!vrPlay && !scene)
                        return screen || gameScreen.holding();
                    if (!nativeStarted) {
                        struct NativeConfig {
                            uint32_t magic, version, bytes, pid;
                            uint64_t base;
                            uint32_t duration, flags, width, height;
                        };
                        // Flag 8: while immersive, the game's active view follows the
                        // head, so work the game does only for that view fits the eyes.
                        // Flag 16: each eye culls what its own previous frame hid, as the
                        // game's views do; without it the render thread did 2-4 times the
                        // work and held VR at 40-65 frames a second.
                        // Flag 32: the game's own view, on the head for that work, is not
                        // rendered as well; it cost as much as an eye and only the monitor
                        // showed it, which gets the left eye instead.
                        // Flag 64 (options 0x80000, --eye-lens-distances): the eyes keep the
                        // distances their own wide lenses give, about half the game's.
                        const uint32_t flags = ((config.options & 4) ? 5 : 13) | ((config.options & 16) ? 0 : 16) |
                                               ((config.options & (4 | 0x10000)) ? 0 : 32) |
                                               ((config.options & 0x80000) ? 64 : 0);
                        // The eyes' scene at `upscale` percent of their size, with a
                        // jittered lens and the engine's temporal anti-aliasing (bits 1,
                        // 2, 4): taken when the views are made.
                        const bool antialiased = !(config.options & 0x8000);
                        // Bit 8: no targets for screen-space reflections (options
                        // 0x40000: the game has them off, or --no-eye-reflections).
                        const uint32_t reflections = (config.options & 0x40000) ? 8u << 8 : 0;
                        check(SpidyEyeTemporal(reinterpret_cast<void*>(static_cast<uintptr_t>(
                                  (antialiased ? config.upscale | 7u << 8 : 100u) | reflections))),
                              "Eye anti-aliasing");
                        check(SpidyEyeShadows(reinterpret_cast<void*>(
                                  static_cast<uintptr_t>((config.options & 0x20000) ? 0 : 1))),
                              "Eye shadows");
                        check(SpidyMonitorRate(reinterpret_cast<void*>(static_cast<uintptr_t>(config.monitorRate))),
                              "Monitor rate");
                        NativeConfig views{
                            0x53534346, 1, 40, config.pid, config.base, moduleDuration, flags, dimensions[0],
                            dimensions[1]};
                        check(SpidyStart(&views), "Start native views");
                        nativeStarted = true;
                        // Game-drawn webs are optional: without them the overlay draws webs.
                        websStarted = !(config.options & 2) && !native_webs::start(config.base, player.record);
                        // The player's body is optional too: without it the hero stays
                        // hidden and the overlay draws gloves, as before.
                        bodyStarted = values.body && !native_body::start(config.base);
                        firstImageDeadline = GetTickCount64() + 3000;
                        message(3, 0, vrPlay ? "Gameplay found; waiting for the first eye images"
                                             : "A scene found; waiting for its first eye images");
                        // Initialization can exceed one predicted display
                        // interval. A scene's screen keeps the game's frame.
                        return !vrPlay;
                    }
                    if (!vrPlay) {
                        // The scene's two eyes: the game's own camera, set apart
                        // for this viewer and this screen (native_eyes::Stereo).
                        native_eyes::Command eyes;
                        eyes.enabled = 3;
                        eyes.serial = ++serial;
                        eyes.leaseMs = 250;
                        native_eyes::setStereo(
                            eyes, native_eyes::screenStereo(
                                      eyeDistance(frame.eyes[0].pose.position, frame.eyes[1].pose.position),
                                      vr_settings::screenWidth(values.screenSize), vr_settings::screenDistance,
                                      vr_settings::screenDepth(values.screenDepth)));
                        NativeEyeFrame saved;
                        saved.serial = eyes.serial;
                        // A held pair ages by the time it was asked for.
                        saved.motion.predictedDisplayTime = frame.predictedDisplayTime;
                        saved.flatScreen = saved.stereoScreen = true;
                        saved.screenPose = screenPose;
                        saved.screenAspect = SpidyFlatAspect();
                        history.remember(saved);
                        const auto code = SpidySetEyes(&eyes);
                        if (code && code != 4003)
                            check(code, "Scene eye command");
                        stereoScene = !code;
                        return true;
                    }
                    if (const float h = frame.head.position.y + seatRaise; std::isfinite(h) && h > .8f && h < 2.4f)
                        standingEye = standingEye <= 0 ? h
                                      : h > standingEye
                                          ? standingEye + (h - standingEye) * std::min(1.f, frame.seconds * 4)
                                          : standingEye - std::min(standingEye - h, frame.seconds * .01f);
                    if (bodyStarted) {
                        // The hero's body follows the headset and the controllers,
                        // placed from the same feet as the eyes.
                        native_body::Command pose;
                        pose.serial = serial;
                        // Under a roof that holds the head below the player's
                        // own: the legs kneel rather than fold up in front of
                        // his eyes.
                        const bool kneels = (motion.standing || crawlsFloor) && motion.headDrop > .05f;
                        kneelFrames += kneels;
                        // Standing on a wall or a ceiling - Spidy's own or the
                        // game's crawl - the body stands on it with the player,
                        // never in the air the swing keeps the hero in there.
                        pose.flags = (flatScreen || !values.body ? 0u : native_body::bodyOn) | native_body::hideHead |
                                     native_body::handTurn |
                                     (swingState.owned && !motion.standing ? native_body::airborne : 0u) |
                                     (calibratedEye > 0 || values.heightCm ? native_body::calibrated : 0u) |
                                     (motion.standing || crawlsFloor ? native_body::standing : 0u) |
                                     (kneels ? native_body::kneel : 0u);
                        pose.standUp = motion.standUp;
                        pose.floor = motion.floor;
                        // A dive: the legs kick out at him (body::Targets::kick).
                        if (kickUntil > frame.predictedDisplayTime && finite(diveAt)) {
                            pose.flags |= native_body::kick;
                            pose.kick = diveAt - motion.anchor;
                        }
                        const bool wallGrip = game_swing::outdoorClimbGrip(
                            climbState, frame.predictedDisplayTime > 0 ? frame.predictedDisplayTime : 0);
                        const bool runningPose = wallRunBody.update(motion, hold, swingState.velocity,
                            swingFresh && values.handClimb && values.body && !flatScreen &&
                            !(pose.flags & native_body::kick), swingState.grounded != 0, wallGrip ? climbState.hands : 0, frame.seconds,
                            vr_settings::fixedMomentumStepPace * .01f);
                        if (motion.ventCrawl && values.handClimb && values.body && !flatScreen) {
                            pose.flags |= native_body::ventHands |
                                          native_body::crawlTouchFlags(motion.wallTouches);
                            const auto sampleTime = frame.predictedDisplayTime > 0 ?
                                static_cast<uint64_t>(frame.predictedDisplayTime) : 0;
                            const bool fresh = climbState.status && climbState.crawl == 1 && sampleTime &&
                                climbState.sampleTimeNs && sampleTime >= climbState.sampleTimeNs &&
                                sampleTime - climbState.sampleTimeNs <= 100'000'000;
                            pose.flags |= native_body::crawlHandFlags(fresh ? climbState.hands : 0);
                            pose.crawlNormal = hold.normal;
                            pose.floor = hold.anchor - motion.anchor;
                        }
                        if (wallCrawlBody.update(motion, hold,
                            values.handClimb && values.body && !flatScreen && !runningPose,
                            frame.seconds, wallGrip ? climbState.hands : 0)) {
                            pose.flags |= native_body::crawl | native_body::crawlPlanted |
                                          native_body::crawlHandSync;
                            pose.flags |= native_body::crawlHandFlags(wallGrip ? climbState.hands : 0);
                            pose.flags |= native_body::crawlTouchFlags(motion.wallTouches) |
                                          native_body::crawlTurnAway;
                            pose.floor = wallCrawlBody.floor();
                            pose.crawlNormal = wallCrawlBody.normal();
                            pose.crawlAhead = wallCrawlBody.ahead();
                            pose.crawlStep = wallCrawlBody.step();
                            pose.crawlPhase = wallCrawlBody.phase();
                            pose.crawlMotion = wallCrawlBody.motion();
                            pose.crawlOrigin = motion.anchor;
                            pose.crawlStrokes[0] = wallCrawlBody.handSteps()[0];
                            pose.crawlStrokes[1] = wallCrawlBody.handSteps()[1];
                            pose.crawlDistance = static_cast<float>(vr_settings::fixedWallDistance) / 100.f;
                        }
                        if (runningPose) {
                            pose.flags &= ~(native_body::standing | native_body::kneel);
                            pose.flags |= native_body::wallRun | native_body::airborne;
                            pose.floor = wallRunBody.floor();
                            pose.crawlNormal = wallRunBody.normal();
                            pose.crawlAhead = wallRunBody.ahead();
                            pose.crawlPhase = wallRunBody.phase();
                            pose.crawlMotion = wallRunBody.speed();
                            pose.crawlDistance = vr_settings::fixedRunTorsoAngle * .0174532925f;
                            pose.runBank = std::tan(vr_settings::fixedMomentumBodyTilt * .0174532925f);
                            pose.runLean = vr_settings::fixedMomentumForwardLean * .0174532925f;
                            pose.runStride = vr_settings::fixedMomentumStepLength * .01f;
                        }
                        pose.eyes = Vec3{(motion.eyes[0][12] + motion.eyes[1][12]) / 2,
                                         (motion.eyes[0][13] + motion.eyes[1][13]) / 2,
                                         (motion.eyes[0][14] + motion.eyes[1][14]) / 2} -
                                    motion.anchor;
                        pose.facing = motion.headPose.orientation;
                        for (unsigned i = 0; i < 2; ++i) {
                            const auto& hand = frame.hands[i];
                            const bool tracked = hand.valid && validTrackedPose(hand.grip);
                            const Vec3 relative = hand.grip.position - frame.head.position;
                            const float seconds =
                                std::isfinite(frame.seconds) && frame.seconds > 0 ? frame.seconds : 1.f / 90;
                            const float speed =
                                tracked && gripsSeen[i] ? length(relative - gripsBefore[i]) / seconds : 0.f;
                            gripsBefore[i] = relative;
                            gripsSeen[i] = tracked;
                            HandPoseInput doing;
                            doing.tracked = tracked;
                            doing.wallContact = motion.active && !flatScreen && values.handClimb &&
                                ((motion.wallTouches & (1u << i)) ||
                                 (climbState.status && (climbState.hands & (1u << i))));
                            doing.web = motion.swing.hands[i].grip;
                            doing.squeeze = hand.squeeze;
                            doing.speed = speed;
                            doing.gripping = onStrandBefore[i] || webTaken[i];
                            doing.holding = attachedBefore[i];
                            doing.shot = ballShot[i];
                            doing.fighting = fightNear;
                            ballShot[i] = false;
                            const auto fingered = fingers[i].update(seconds, doing);
                            pose.hands[i] = {motion.grips[i].position - motion.anchor, motion.grips[i].orientation,
                                             tracked ? 1u : 0u, fingered.fist};
                            pose.shoot[i] = fingered.shoot;
                        }
                        // The player's size: HEIGHT when set, else the T-pose
                        // calibration's, else the headset's running height.
                        pose.height = values.heightCm ? seated::eyeHeight(values.heightCm)
                                      : calibratedEye > 0 ? calibratedEye
                                                          : standingEye;
                        pose.armLength = calibratedArm;
                        pose.trackingYaw = motion.swing.trackingYaw;
                        // A flip's tilt in world axes: the body turns with the player.
                        // Standing on a surface, the body already stands along it:
                        // only a flip turns its planted pose further (none on a wall).
                        // The running body follows the existing wall-standing
                        // view about its eyes. Momentum runs retain their pose;
                        // residual surface rotation also follows on stick release.
                        const Quat bodyTilt = runningPose
                            ? (motion.viewTilt > .0001f ? motion.swing.tilt : Quat{}) : motion.bodyTilt();
                        if (const Quat tilt = bodyTilt;
                            tilt.x != 0 || tilt.y != 0 || tilt.z != 0) {
                            const Quat yaw = Quat::yaw(motion.swing.trackingYaw);
                            pose.tilt = yaw * tilt * yaw.conjugate();
                        }
                        native_body::submit(pose);
                    }
                    // Keep publishing tracking while the engine renders earlier
                    // commands. The presentation loop no longer waits for this pose.
                    native_eyes::Command eyes;
                    eyes.enabled = flatScreen ? 2 : 1;
                    eyes.serial = ++serial;
                    eyes.leaseMs = 250;
                    eyes.anchor = motion.anchor;
                    eyes.anchored = 1;
                    for (unsigned i = 0; i < 2; ++i) {
                        eyes.eyes[i].world = motion.eyes[i];
                        const auto& f = motion.fovs[i];
                        eyes.eyes[i].fov[0] = f.left;
                        eyes.eyes[i].fov[1] = f.right;
                        eyes.eyes[i].fov[2] = f.down;
                        eyes.eyes[i].fov[3] = f.up;
                    }
                    // The HUD panel for this command's frame, as the head sees it:
                    // fixed in the room, so a reused image the headset turns to a
                    // newer head shows it where it was.
                    const Quat hudFacing = hudFollow.update(frame.head.orientation, frame.seconds);
                    native_hud::follow(eyes.serial, frame.head.orientation.conjugate() * hudFacing);
                    NativeEyeFrame saved;
                    saved.serial = eyes.serial;
                    saved.motion = motion;
                    saved.trackingEyes = frame.eyes;
                    saved.flatScreen = flatScreen;
                    saved.uiStyle = values.uiStyle == 1 ? UiStyle::tech : UiStyle::comic;
                    saved.combatVisuals = values.combatVisuals;
                    saved.screenPose = screenPose;
                    saved.screenAspect = SpidyFlatAspect();
                    saved.slow = values.combatVisuals ? moments.view(slowMotion.view()) : slowMotion.view();
                    saved.padLayer = padLayer.latched();
                    if (gadgetWheel.open()) {
                        // The gadget wheel, from tracking space into this frame's
                        // world, with the gadgets in their places.
                        const Pose toWorld = compose(motion.headPose, inverse(frame.head));
                        const auto& f = gadgetWheel.frame();
                        auto& view = saved.gadgetWheel;
                        view.open = true;
                        view.age = gadgetWheel.age();
                        view.litAge = gadgetWheel.litAge();
                        view.lit = gadgetWheel.lit();
                        view.frame = {compose(toWorld, Pose{f.centre, {}}).position, toWorld.orientation.rotate(f.right),
                                      toWorld.orientation.rotate(f.up), toWorld.orientation.rotate(f.out)};
                        wheelPlaces(view);
                    }
                    if (phone.live()) {
                        // The phone: this image's way from tracking space into its
                        // world, and the screen as it is now, which every image
                        // shows (phoneShown).
                        saved.phone = {true, compose(motion.headPose, inverse(frame.head))};
                        auto& view = phoneShown;
                        view.screen = phone.ui().screen();
                        view.app = phone.ui().app();
                        view.lit = phone.ui().button();
                        // GEAR: the gadget wheel's places in its order, the web
                        // shooter's first, and the one equipped as the wheel has it.
                        GadgetWheelView wheel;
                        wheelPlaces(wheel);
                        view.gearCount = 0;
                        for (int p = 0; p < gadgetPlaces; ++p) {
                            if (wheel.places[p] == Gadget::none)
                                continue;
                            auto& row = view.gear[view.gearCount++];
                            strncpy_s(row.name, gadgetName(wheel.places[p]), _TRUNCATE);
                            row.ammo = wheel.ammo[p];
                            row.most = wheel.most[p];
                            row.equipped = wheel.equipped == p;
                        }
                        view.gearTop = phone.ui().gearTop();
                        const auto music = phoneMusic.status();
                        view.playing = music.playing;
                        view.autoplay = music.autoplay;
                        view.failed = music.failed;
                        view.track = music.track;
                        view.tracks = music.tracks;
                        view.volume = music.volume;
                        strncpy_s(view.song, music.song, _TRUNCATE);
                    }
                    for (unsigned i = 0; i < 2; ++i) {
                        saved.webs[i] = swingState.webs[i];
                        saved.webTimes[i] = webTimes[i];
                        saved.aims[i] = aims.hands[i];
                        saved.offers[i] = pointing[i] ? *pointing[i] : offer_pointing::Pointed{};
                    }
                    saved.webSlots = {webRopes.slot(0), webRopes.slot(1)};
                    saved.strandUpper = webRopes.upper();
                    if (calibration.phase() != body_calibration::Phase::idle) {
                        // The panel stays where it appeared in the room; the tracking
                        // space is where this frame's head puts it in the world.
                        auto& view = saved.calibration;
                        view.phase = calibration.phase();
                        view.hint = calibration.hint();
                        view.progress = calibration.progress();
                        view.ready = calibration.armsReady();
                        view.result = calibration.result();
                        view.result.eyeHeight = calibratedEye;
                        view.result.armLength = calibratedArm;
                        view.panel = compose(compose(motion.headPose, inverse(frame.head)), calibrationPanel);
                        for (unsigned i = 0; i < 2; ++i) {
                            view.handTracked[i] = frame.hands[i].valid && validTrackedPose(frame.hands[i].grip);
                            view.grips[i] = motion.grips[i];
                        }
                    }
                    history.remember(saved);
                    const auto code = SpidySetEyes(&eyes);
                    if (code && code != 4003)
                        check(code, "Tracked eye command");
                    return !code;
                },
                [&](std::array<XrRuntime::EyeTarget, 2>& targets) {
                    const auto copyStart = std::chrono::steady_clock::now();
                    if (showScreen && stereoScene && native_hud::onScreen(sceneHoldMs)) {
                        // The scene's newest pair, once the game has rendered
                        // one with the HUD's panel on its screen; until then
                        // the game's own frame below.
                        uint64_t pairSerial{}, pairGeneration{};
                        const bool copied = native_gpu::copyLatest(targets[0].texture, targets[1].texture,
                                                                   pairSerial, pairGeneration);
                        check(native_gpu::error(), "GPU eye bridge");
                        const auto* saved = copied ? history.find(pairSerial, displayTime) : nullptr;
                        if (saved && saved->stereoScreen) {
                            copyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                               copyStart)
                                         .count();
                            runtime.presentation(true, screenPose, saved->screenAspect,
                                                 vr_settings::screenWidth(values.screenSize), true);
                            drawn = Presentation::stereoScreen;
                            D3D12Renderer::Captured pixels;
                            if (snapshotTicket && overlay.captured(snapshotTicket, pixels)) {
                                publishSnapshot(pendingSnapshot, &pixels);
                                snapshotTicket = 0;
                            }
                            if (!snapshotTicket && snapshots.due(GetTickCount64(), 0, false) &&
                                (snapshotTicket = overlay.capture(targets[0].texture)) != 0) {
                                pendingSnapshot = {};
                                pendingSnapshot.flatScreen = 1;
                                pendingSnapshot.generation = pairGeneration;
                                pendingSnapshot.serial = pairSerial;
                            }
                            std::lock_guard lock(telemetry);
                            InterlockedIncrement64(&SpidyXrData.sequence);
                            SpidyXrData.serial = pairSerial;
                            SpidyXrData.generation = pairGeneration;
                            InterlockedIncrement64(&SpidyXrData.sequence);
                            return true;
                        }
                    }
                    native_gpu::Screen image;
                    if (showScreen && native_gpu::latestScreen(image, imageHoldMs)) {
                        // The game's presented frame, scaled into the image the screen shows.
                        const auto& left = targets[0];
                        overlay.blit(image.texture.Get(), image.view, image.linear,
                                     {left.texture, left.format, left.width, left.height, {}});
                        copyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                           copyStart)
                                     .count();
                        runtime.presentation(true, screenPose,
                                             static_cast<float>(image.width) / static_cast<float>(image.height),
                                             vr_settings::screenWidth(values.screenSize));
                        drawn = Presentation::gameScreen;
                        D3D12Renderer::Captured pixels;
                        if (snapshotTicket && overlay.captured(snapshotTicket, pixels)) {
                            publishSnapshot(pendingSnapshot, &pixels);
                            snapshotTicket = 0;
                        }
                        if (!snapshotTicket && snapshots.due(GetTickCount64(), 0, false) &&
                            (snapshotTicket = overlay.capture(left.texture)) != 0) {
                            pendingSnapshot = {};
                            pendingSnapshot.flatScreen = 1;
                        }
                        std::lock_guard lock(telemetry);
                        InterlockedIncrement64(&SpidyXrData.sequence);
                        SpidyXrData.serial = SpidyXrData.generation = 0;
                        InterlockedIncrement64(&SpidyXrData.sequence);
                        return true;
                    }
                    uint64_t imageSerial{}, imageGeneration{};
                    bool copied = native_gpu::copyLatest(targets[0].texture, targets[1].texture, imageSerial,
                                                         imageGeneration);
                    check(native_gpu::error(), "GPU eye bridge");
                    const auto* saved = copied ? history.find(imageSerial, displayTime) : nullptr;
                    copied = copied && saved;
                    copyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                       copyStart)
                                 .count();
                    if (copied) {
                        // A scene's pair stays on its screen until play's first image.
                        drawn = saved->stereoScreen ? Presentation::stereoScreen
                                : saved->flatScreen ? Presentation::flat
                                                    : Presentation::immersive;
                        runtime.presentation(saved->flatScreen, saved->screenPose, saved->screenAspect,
                                             vr_settings::screenWidth(values.screenSize), saved->stereoScreen);
                        const auto& rendered = saved->motion;
                        for (unsigned eye = 0; eye < 2; ++eye) {
                            const auto& pose = saved->trackingEyes[eye].pose;
                            const auto& f = saved->trackingEyes[eye].fov;
                            targets[eye].view.pose = {{pose.orientation.x, pose.orientation.y,
                                                       pose.orientation.z, pose.orientation.w},
                                                      {pose.position.x, pose.position.y, pose.position.z}};
                            targets[eye].view.fov = {f.left, f.right, f.up, f.down};
                        }
                        const auto overlayStart = std::chrono::steady_clock::now();
                        // The native views moved this command's eyes with the
                        // player to the rendered frame. Hands move with them, so
                        // world-space web anchors line up with the rendered city.
                        Vec3 travel{};
                        native_eyes::renderOffset(imageGeneration, travel);
                        const Vec3 viewer =
                            Vec3{(rendered.eyes[0][12] + rendered.eyes[1][12]) / 2,
                                 (rendered.eyes[0][13] + rendered.eyes[1][13]) / 2,
                                 (rendered.eyes[0][14] + rendered.eyes[1][14]) / 2} +
                            travel;
                        const auto& lens = rendered.fovs[0];
                        const float pixelAngle =
                            (std::tan(lens.right) - std::tan(lens.left)) / static_cast<float>(targets[0].width);
                        const auto shown = saved->motion.predictedDisplayTime;
                        std::vector<Vertex> vertices;
                        // Where the overlay puts each tracked web shooter, and
                        // where the game's rope for this image's frame starts.
                        Vec3 shooters[2]{}, ropeStarts[2]{};
                        uint32_t shooterHands{}, ropeHands{};
                        float webGap = -1;
                        for (unsigned hand = 0; !saved->flatScreen && hand < 2; ++hand) {
                            const auto& web = saved->webs[hand];
                            const auto& timing = saved->webTimes[hand];
                            // The game's rope is part of the scene image already: the
                            // rope that draws this hand's web, which may be the other
                            // side's (one web in both hands).
                            const int slot = saved->webSlots[hand];
                            const bool gameWeb = native_webs::drawing(slot >= 0 ? static_cast<unsigned>(slot) : hand);
                            // One web in both hands: the strand comes to the other hand
                            // first, and this hand's part runs from wrist to wrist, as
                            // slack as the rope between the two holds is long.
                            const int upper = saved->strandUpper;
                            const bool lower = web.attached && upper >= 0 && upper != static_cast<int>(hand);
                            WebLine line;
                            const auto between = [&](Vec3 start) {
                                auto other = rendered.grips[static_cast<size_t>(upper)];
                                other.position += travel;
                                line.start = start;
                                line.end = webShooter(other, static_cast<unsigned>(upper)).position;
                                line.slack = std::max(
                                    web.length - saved->webs[static_cast<size_t>(upper)].length -
                                        length(line.end - line.start),
                                    0.f);
                                line.extended = 1;
                                line.splat = false;
                            };
                            // The timeline keeps the anchor through the release, so
                            // the splat keeps its shape while the web snaps back.
                            line.seed =
                                static_cast<float>(hand) * 1.7f + timing.anchor.x * .37f + timing.anchor.z * .11f;
                            if (!rendered.swing.hands[hand].tracked) {
                                if (!gameWeb && WebAnimation::released((shown - timing.releasedAt) * 1e-9f,
                                                                       timing.wrist + travel, timing.anchor, line))
                                    appendWeb(vertices, line, viewer, pixelAngle);
                                continue;
                            }
                            auto grip = rendered.grips[hand];
                            grip.position += travel;
                            const Vec3 shooter = webShooter(grip, hand).position;
                            shooters[hand] = shooter;
                            shooterHands |= 1u << hand;
                            if (gameWeb) {
                                // The game draws the piece between the hands
                                // itself where it can (native_webs::Held).
                                if (lower && !native_webs::drawsHeld()) {
                                    between(shooter);
                                    appendWeb(vertices, line, viewer, pixelAngle);
                                } else if (web.attached && slot == static_cast<int>(hand) &&
                                           native_eyes::renderWebStart(imageGeneration, hand, ropeStarts[hand])) {
                                    ropeHands |= 1u << hand;
                                    webGap = std::max(webGap, length(shooters[hand] - ropeStarts[hand]));
                                }
                                continue;
                            } else if (lower) {
                                between(shooter);
                                appendWeb(vertices, line, viewer, pixelAngle);
                            } else if (web.attached) {
                                line.start = shooter;
                                line.end = web.anchor;
                                line.slack = web.tension > 0 ? 0.f
                                                             : std::max(web.length - length(line.end - line.start), 0.f);
                                line.extended = WebAnimation::extended((shown - timing.attachedAt) * 1e-9f);
                                appendWeb(vertices, line, viewer, pixelAngle);
                            } else if (WebAnimation::released((shown - timing.releasedAt) * 1e-9f, shooter,
                                                              timing.anchor, line)) {
                                appendWeb(vertices, line, viewer, pixelAngle);
                            }
                        }
                        // Aim markers: where each free hand's web button would send its
                        // web, on the line that hand points along in this image, that
                        // line steadied from image to image. Translucent, the farthest
                        // drawn first.
                        std::vector<AimMarker> marks;
                        for (unsigned hand = 0; hand < 2; ++hand) {
                            auto& steady = aimMotion[hand];
                            const auto& input = rendered.swing.hands[hand];
                            if (saved->flatScreen) {
                                steady.reset();
                                continue;
                            }
                            steady.begin(shown);
                            if (!input.tracked) {
                                steady.markers(nullptr, {}, {}, marks);
                                continue;
                            }
                            const auto& aim = saved->aims[hand];
                            const auto kind = static_cast<game_swing::AimKind>(aim.kind);
                            // The hand moved with the player to this image, as its glove does.
                            const Vec3 origin = input.aim.position + travel;
                            const Vec3 direction =
                                steady.aim(normalized(input.aim.orientation.rotate({0, 0, -1})),
                                           trackingTurn(rendered.swing));
                            const float reach = length(aim.point - input.aim.position);
                            AimMarker marker;
                            marker.squeeze = input.grip;
                            marker.hand = hand;
                            // A hand pointed at one of the game's interactions on
                            // Spidy's wall shows that instead: a turning ring round
                            // one the interact would use, a faint one round one to
                            // come closer to.
                            if (const auto& offer = saved->offers[hand]; offer.handle) {
                                marker.kind = offer.usable ? AimMark::target : AimMark::air;
                                marker.point = offer.position;
                                marker.radius = offer_pointing::markRadius;
                                steady.markers(&marker, origin, direction, marks);
                                ++markersDrawn;
                                continue;
                            }
                            switch (kind) {
                            case game_swing::AimKind::prop:
                            case game_swing::AimKind::character:
                                marker.kind = AimMark::target;
                                marker.point = aim.point;
                                marker.radius = aim.radius;
                                break;
                            case game_swing::AimKind::anchor:
                                marker.point = onAimLine(aim.point, aim.normal, origin, direction);
                                break;
                            case game_swing::AimKind::air:
                                marker.kind = AimMark::air;
                                marker.point = origin + direction * reach;
                                break;
                            default:
                                marker.kind = AimMark::blocked;
                                marker.point = length(aim.normal) > .5f
                                                   ? onAimLine(aim.point, aim.normal, origin, direction)
                                                   : origin + direction * reach;
                                break;
                            }
                            // Not on the floor a lowered hand points at, by the player's feet.
                            const float away = length(marker.point - origin);
                            const float nearest = marker.kind == AimMark::target    ? .5f
                                                  : marker.kind == AimMark::blocked ? 3.f
                                                                                    : 2.f;
                            const bool wanted = kind != game_swing::AimKind::none &&
                                                kind <= game_swing::AimKind::character &&
                                                !saved->webs[hand].attached && away >= nearest;
                            steady.markers(wanted ? &marker : nullptr, origin, direction, marks);
                            markersDrawn += wanted;
                        }
                        std::sort(marks.begin(), marks.end(), [&](const AimMarker& a, const AimMarker& b) {
                            return length(a.point - viewer) > length(b.point - viewer);
                        });
                        std::vector<Vertex> translucent;
                        for (const auto& mark : marks)
                            appendAimMarker(translucent, mark, viewer, pixelAngle * markerPixels,
                                            saved->uiStyle);
                        // Slow motion's focus meter over the back of the left wrist, moved
                        // with the player to this image as the glove is.
                        if (!saved->flatScreen && saved->slow.meter > 0 && rendered.swing.hands[0].tracked) {
                            auto wrist = rendered.hands[0];
                            wrist.position += travel;
                            const double clock = std::fmod(static_cast<double>(shown) * 1e-9, 3600.);
                            appendSlowMotionMeter(translucent, saved->slow, wrist, viewer, pixelAngle,
                                                  static_cast<float>(clock), saved->uiStyle);
                        }
                        // The pad layer's sign over the back of the right wrist while
                        // it is on: until October 25 only a buzz said so.
                        if (!saved->flatScreen && saved->padLayer && rendered.swing.hands[1].tracked) {
                            auto wrist = rendered.hands[1];
                            wrist.position += travel;
                            appendPadLayerSign(translucent, wrist, viewer, pixelAngle, saved->uiStyle);
                        }
                        // Hide combat artwork only; targeting, damage, counters and haptics still update.
                        const bool combatVisuals = values.combatVisuals && saved->combatVisuals && !saved->flatScreen;
                        // The bursts where the latest blows landed: fixed in the world.
                        if (combatVisuals)
                            for (const auto& burst : bursts) {
                                if (!burst.since)
                                    continue;
                                auto view = burst.view;
                                view.age = static_cast<float>(static_cast<double>(shown - burst.since) * 1e-9);
                                appendImpact(translucent, view, viewer, pixelAngle, saved->uiStyle);
                            }
                        // The called blow's arrow, over its thug.
                        if (combatVisuals && cue.left > 0) {
                            auto view = cue;
                            view.age = static_cast<float>(static_cast<double>(shown - cueSince) * 1e-9);
                            if (view.age >= 0)
                                appendCue(translucent, view, viewer, pixelAngle, saved->uiStyle);
                        }
                        // A knockout's lettering, where he stood.
                        if (combatVisuals)
                            for (const auto& mark : knockoutMarks) {
                                if (!mark.since)
                                    continue;
                                auto view = mark.view;
                                view.age = static_cast<float>(static_cast<double>(shown - mark.since) * 1e-9);
                                appendKnockout(translucent, view, viewer, pixelAngle, saved->uiStyle);
                            }
                        // Spider-Sense's marks: over each attacker's head, and for one out of
                        // view in the sightline as well, moved with the player as the eyes are.
                        // Attack warnings remain visible independently of optional combat artwork.
                        if (!saved->flatScreen && validTrackedPose(rendered.headPose)) {
                            Pose eyes = rendered.headPose;
                            eyes.position += travel;
                            bool marked{}, sighted{};
                            const auto since = [&](int64_t at) {
                                return static_cast<float>(static_cast<double>(shown - at) * 1e-9);
                            };
                            for (auto& mark : senseMarks) {
                                if (!mark.attacker)
                                    continue;
                                const float age = since(mark.since);
                                const float broken = mark.brokenAt ? since(mark.brokenAt) : -1.f;
                                if (age < 0)
                                    continue;
                                SenseView view{mark.head, mark.answer, mark.left, mark.timed, age, broken};
                                appendSense(translucent, view, viewer, pixelAngle, saved->uiStyle);
                                mark.sight = senseSight(eyes, mark.head, mark.sight);
                                if (mark.sight.shown) {
                                    const SenseSightView sight{mark.answer, mark.sight, mark.left,
                                                               mark.timed,  age,        broken};
                                    appendSenseSight(translucent, sight, eyes, pixelAngle, {},
                                                     saved->uiStyle);
                                }
                                marked = true;
                                sighted |= mark.sight.shown;
                            }
                            senseFrames += marked;
                            senseSightFrames += sighted;
                        }
                        // The T-pose calibration's panel and controller rings, moved with
                        // the player to this image as the hands are. The tech style's
                        // panel is translucent, as the game's menus are.
                        if (!saved->flatScreen && saved->calibration.phase != body_calibration::Phase::idle) {
                            auto view = saved->calibration;
                            view.panel.position += travel;
                            for (auto& grip : view.grips)
                                grip.position += travel;
                            auto& panel = saved->uiStyle == UiStyle::tech ? translucent : vertices;
                            body_calibration::appendView(panel, view, viewer, saved->uiStyle);
                        }
                        // The gadget wheel in front of its hand, moved with the
                        // player to this image as the hands are: printed, so
                        // laid down in order over all the rest.
                        if (!saved->flatScreen && saved->gadgetWheel.open) {
                            auto view = saved->gadgetWheel;
                            view.frame.centre += travel;
                            appendGadgetWheel(translucent, view, viewer, pixelAngle, saved->uiStyle);
                        }
                        // From here on the overlay is drawn about the eyes' middle,
                        // not the world's origin. The city is kilometres across, and
                        // a vertex there taken through the eye's view-projection in
                        // single floats was off by a tenth of a millimetre or so,
                        // each by its own amount and anew every frame: the phone's
                        // screen, 0.3 mm over its glass, lost to the glass at a
                        // fifth of its points 1158 m out (the user, October 24:
                        // "still z-fighting between screen and its body"). About
                        // the eyes the error is a thousandth of that.
                        const Vec3 about = viewer;
                        for (auto& v : vertices)
                            v.position = v.position - about;
                        for (auto& v : translucent)
                            v.position = v.position - about;
                        // The phone where it is now - in the hand, where the hand is
                        // now - through this image's own way into its world, and
                        // moved with the player to this image as the wheel is: its
                        // body lit as it turns, its screen a picture of its own
                        // (D3D12Renderer::Picture) on the glass. Built about the
                        // eyes from the start, its parts a tenth of a millimetre
                        // apart stay so.
                        std::vector<Vertex> phoneScreen;
                        D3D12Renderer::Picture phonePicture;
                        if (!saved->flatScreen && saved->phone.open) {
                            auto view = phoneShown;
                            const Pose& toWorld = saved->phone.toWorld;
                            const auto& f = phone.place();
                            view.open = true;
                            view.age = phone.age();
                            const Quat turn = toWorld.orientation;
                            view.frame = {compose(toWorld, Pose{f.centre, {}}).position + travel - about,
                                          turn.rotate(f.right), turn.rotate(f.up), turn.rotate(f.out)};
                            appendPhone(vertices, view, viewer - about);
                            appendPhoneScreen(phoneScreen, view);
                            phonePicture = {phoneScreen, phoneScreenHalfWidth, phoneScreenHalfHeight,
                                            phoneScreenCorners(view), phoneScreenGlass()};
                        }
                        std::array<D3D12Renderer::ViewTarget, 2> overlayViews;
                        // Slow motion recolours the image under the overlay, about each eye's lens.
                        D3D12Renderer::Grade grade;
                        grade.amount = saved->slow.blend;
                        grade.ripple = saved->slow.ripple;
                        grade.rippleStrength = saved->slow.entering ? 1.f : .7f;
                        grade.entering = saved->slow.entering;
                        for (unsigned eye = 0; eye < 2; ++eye) {
                            const auto& f = rendered.fovs[eye];
                            grade.tangents[eye] = {std::tan(f.left), std::tan(f.right), std::tan(f.up),
                                                   std::tan(f.down)};
                        }
                        for (unsigned eye = 0; eye < 2; ++eye) {
                            const auto& target = targets[eye];
                            const auto& f = rendered.fovs[eye];
                            // The eye about the eyes' middle, as the overlay is.
                            auto pose = rendered.eyes[eye];
                            pose[12] += travel.x - about.x;
                            pose[13] += travel.y - about.y;
                            pose[14] += travel.z - about.z;
                            const auto vp = multiply(projection(f.left, f.right, f.down, f.up, .03f, 200.f),
                                                     native_view::view(pose));
                            overlayViews[eye] = {target.texture, target.format, target.width, target.height,
                                                 vp};
                        }
                        if (!saved->flatScreen)
                            overlay.renderViews(overlayViews, vertices, true, false, translucent, &grade,
                                                &phonePicture);
                        overlayMs = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - overlayStart)
                                        .count();
                        // The game's window mirrors the left eye as it is now, the
                        // drawings above on it; copied only when the window took the last.
                        native_gpu::keepShownEye(targets[0].texture);
                        if (webGap >= 0 && imageGeneration != gapGeneration) {
                            native_eyes::reportWebGap(webGap);
                            gapGeneration = imageGeneration;
                        }
                        // Session report: publish a finished copy of the left
                        // eye, then queue the next when one is due.
                        D3D12Renderer::Captured pixels;
                        if (snapshotTicket && overlay.captured(snapshotTicket, pixels)) {
                            publishSnapshot(pendingSnapshot, &pixels);
                            snapshotTicket = 0;
                        }
                        const float speed = swingState.status ? length(swingState.velocity) : 0;
                        if (!snapshotTicket && snapshots.due(GetTickCount64(), speed, ropeHands != 0) &&
                            (snapshotTicket = overlay.capture(targets[0].texture)) != 0) {
                            pendingSnapshot = {};
                            pendingSnapshot.flags = ropeHands;
                            pendingSnapshot.generation = imageGeneration;
                            pendingSnapshot.serial = imageSerial;
                            pendingSnapshot.flatScreen = saved->flatScreen;
                            pendingSnapshot.speed = speed;
                            pendingSnapshot.webGap = webGap;
                            const auto& eye = overlayViews[0];
                            for (unsigned hand = 0; hand < 2; ++hand) {
                                if (shooterHands & (1u << hand))
                                    eyePixel(eye.viewProjection, shooters[hand], eye.width, eye.height,
                                             pendingSnapshot.wrist[hand][0], pendingSnapshot.wrist[hand][1]);
                                if (ropeHands & (1u << hand))
                                    eyePixel(eye.viewProjection, ropeStarts[hand], eye.width, eye.height,
                                             pendingSnapshot.ropeStart[hand][0],
                                             pendingSnapshot.ropeStart[hand][1]);
                            }
                        }
                    }
                    std::lock_guard lock(telemetry);
                    auto& d = SpidyXrData;
                    InterlockedIncrement64(&d.sequence);
                    d.dropped += !copied;
                    d.serial = imageSerial;
                    d.generation = imageGeneration;
                    d.markers = markersDrawn;
                    InterlockedIncrement64(&d.sequence);
                    return copied;
                });
            // No headset frame for a while: slow motion ends on its own, in real time.
            if (const auto now = GetTickCount64(); now - slowSteppedMs >= 200)
                stepSlowMotion(static_cast<float>(now - slowSteppedMs) / 1000, false, false);
            const auto& timing = runtime.lastFrameTiming();
            if (submittedFrames >= 30 && timing.period > 0) {
                const double stages[] = {timing.total,   timing.wait,  timing.tracking, timing.prepare,
                                         timing.acquire, copyMs,       overlayMs,       timing.release,
                                         timing.end,     timing.period};
                std::lock_guard lock(telemetry);
                InterlockedIncrement64(&SpidyXrTimingData.sequence);
                for (unsigned i = 0; i < 10; ++i)
                    SpidyXrTimingData.stages[i].add(stages[i]);
                InterlockedIncrement64(&SpidyXrTimingData.sequence);
            }
            // Count only a successful xrEndFrame containing a projection layer.
            // A GPU copy alone does not confirm that OpenXR accepted the frame.
            const bool submitted = runtime.lastFrameSubmitted();
            if (submitted) {
                ++submittedFrames;
                lastPresentedMs = GetTickCount64();
                if (!presented || drawn != shownKind) {
                    presented = true;
                    shownKind = drawn;
                    // The game played on its screen shows it there, flat or in 3D.
                    const bool screenPlay =
                        flatScreen && (drawn == Presentation::gameScreen || drawn == Presentation::stereoScreen);
                    message(3, 0,
                            screenPlay
                                ? "The game on its screen: the controllers work as an Xbox controller; hold the "
                                  "menu button for its Back (the map), hold the left stick's click and push the "
                                  "right stick for its D-pad - pause, SWITCH TO VR goes back to VR"
                            : drawn == Presentation::gameScreen
                                ? "Game screen (menus, loading, cutscenes): the controllers work as an Xbox "
                                  "controller, VR settings are in Settings > SPIDY VR - VR resumes with gameplay"
                            : drawn == Presentation::stereoScreen
                                ? "Game screen in 3D (a cutscene or another scene the game plays): the "
                                  "controllers work as an Xbox controller - Settings > SPIDY VR > GAME SCREEN 3D "
                                  "sets its depth"
                            : drawn == Presentation::flat
                                ? "Flat screen - pause, SWITCH TO VR goes back to VR"
                                // 255 characters, all the message holds.
                                : "VR active - A jumps (hold: Charge Jump), B dodges, Y interacts, X: LB + RB "
                                  "where shown (hold: gadget wheel), menu or both sticks held: pause, left "
                                  "stick click: slow time, trigger at temple: scan; right stick down: heal, up: "
                                  "zip (hold to aim, let go)");
                }
            }
            {
                std::lock_guard lock(telemetry);
                auto& d = SpidyXrData;
                InterlockedIncrement64(&d.sequence);
                d.presentation = static_cast<uint32_t>(submitted ? drawn : Presentation::none);
                if (submitted) {
                    ++d.submitted;
                    // Game-screen frames are not eye images: neither new nor reused ones.
                    if (drawn == Presentation::gameScreen || drawn == Presentation::stereoScreen) {
                        ++d.screenSubmitted;
                        d.stereoSubmitted += drawn == Presentation::stereoScreen;
                    } else if (d.generation != previousPresentedGeneration) {
                        ++d.uniqueSubmitted;
                        lastNewImageMs = lastPresentedMs;
                        previousPresentedGeneration = d.generation;
                    } else {
                        ++d.reusedSubmitted;
                    }
                }
                InterlockedIncrement64(&d.sequence);
            }
            if (!active)
                break;
        }
        message(6, 0, "Stopping VR and restoring game hooks");
    } catch (const std::exception& e) {
        error = 1;
        failure = e.what();
    }
    players.stop();
    if (padInstalled)
        game_pad::uninstall();
    game_menu::uninstall();
    // The game's own time back, and no blow still sounding. The ticket's
    // guard stays for as long as the game runs: it is what keeps a quit to
    // the main menu after VR from closing the game.
    game_time::uninstall();
    PlaySoundW(nullptr, nullptr, 0);
    bridge::Control release;
    release.serial = ++serial;
    release.leaseMs = 0;
    if (submitInput)
        submitInput(&release);
    if (swingStarted) {
        const auto code = stopSwing(nullptr);
        if (code) {
            error = code;
            failure = "Native swinging did not stop cleanly";
        }
    }
    if (websStarted) {
        const auto code = native_webs::stop();
        if (code) {
            error = code;
            failure = "Game web lines did not stop cleanly";
        }
    }
    if (bodyStarted) {
        const auto code = native_body::stop();
        if (code) {
            error = code;
            failure = "The player's body did not stop cleanly";
        }
    }
    if (nativeStarted) {
        const auto code = SpidyStop(nullptr);
        if (code) {
            error = code;
            failure = "Native views did not retire cleanly";
        }
    }
    if (gpuStarted) {
        const auto code = native_gpu::stop();
        if (code) {
            error = code;
            failure = "Native GPU bridge did not stop cleanly";
        }
    }
    if (raysStarted) {
        const auto code = stopRays(nullptr);
        if (code) {
            error = code;
            failure = "Controller world rays did not stop cleanly";
        }
    }
    if (stopBridge) {
        const auto code = stopBridge(nullptr);
        if (code) {
            error = code;
            failure = "Input bridge did not stop cleanly";
        }
    }
    message(error ? 5 : 4, error, error ? failure.c_str() : "VR session finished");
    return error;
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI SpidyXrStart(void* input) {
    std::lock_guard life(lifecycle);
    if (worker)
        return 1000; // one bounded XR session per process during validation
    if (!read(reinterpret_cast<uintptr_t>(input), &config, sizeof(config)) || config.magic != 0x53585243 ||
        config.version != 24 || config.bytes != sizeof(config) || config.pid != GetCurrentProcessId() ||
        config.runtime[std::size(config.runtime) - 1] ||
        config.base != reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)) ||
        !GetModuleHandleW(L"Spider-Man.exe") || !config.queue || !config.bridgeModule || !config.rayModule ||
        !config.motionModule || config.options > 0x3fffff || config.snapTurn > 90 || config.haptics > 100 ||
        !validEyeUpscale(config.upscale) || !validMonitorRate(config.monitorRate) ||
        config.screenSize > 2 || config.screenDepth > 3 || config.bodyHaptics > 100 || config.smoothTurn > 360 ||
        config.hud > 3 || config.flipSpeed < 90 ||
        config.flipSpeed > 480 || !std::isfinite(config.swingSpeed) || config.swingSpeed < 1 ||
        config.swingSpeed > 65 || (config.durationMs && config.durationMs < 2000) || config.durationMs > 25000 ||
        (config.eyeSize && !validEyeSize(config.eyeSize)) || !validRenderScale(config.renderScale) ||
        (config.eyeSize && config.renderScale != 100) || config.weight < 40 || config.weight > 300)
        return 1001;
    // A calibration is both measurements or neither.
    if ((config.eyeHeightMm == 0) != (config.armLengthMm == 0) ||
        (config.eyeHeightMm &&
         (config.eyeHeightMm < body_calibration::minEyeHeightMm || config.eyeHeightMm > body_calibration::maxEyeHeightMm ||
          config.armLengthMm < body_calibration::minArmLengthMm || config.armLengthMm > body_calibration::maxArmLengthMm)))
        return 1001;
    auto module = reinterpret_cast<HMODULE>(config.bridgeModule);
    submitInput = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidySubmit"));
    sampleBridge = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidyBridgeSample"));
    stopBridge = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidyStop"));
    retargetBridge = reinterpret_cast<BridgeCall>(GetProcAddress(module, "SpidyRetarget"));
    auto rays = reinterpret_cast<HMODULE>(config.rayModule);
    startRays = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyRayStart"));
    submitRays = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyRaySubmit"));
    stopRays = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyRayStop"));
    startSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingStart"));
    submitSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingSubmit"));
    sampleSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingSample"));
    sampleSlingshot = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySlingshotSample"));
    sampleBends = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingBends"));
    sampleClimb = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyClimbSample"));
    rayResults = reinterpret_cast<const native_rays::Data*>(GetProcAddress(rays, "SpidyRayData"));
    stopSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingStop"));
    retargetSwing = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingRetarget"));
    // Optional: an older ray module has no web grab, nor punching, nor aim previews.
    sampleGrab = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyGrabSample"));
    sampleAim = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyAimSample"));
    startPunch = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyPunchStart"));
    samplePunch = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyPunchSample"));
    // Optional too: without them the VR settings' grab, speed and punch
    // switches take effect only at the next session.
    stopPunch = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyPunchStop"));
    // Optional: an older ray module has no web strike, and a punch only its short pulse.
    sampleStrike = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyStrikeSample"));
    sampleSense = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySenseSample"));
    aimGameThrow = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyThrowAim"));
    soundResults = reinterpret_cast<const game_sound::Data*>(GetProcAddress(rays, "SpidySoundData"));
    soundMode = reinterpret_cast<DWORD(WINAPI*)(void*)>(GetProcAddress(rays, "SpidySoundMode"));
    if (const game_sound::Data expected; soundResults && (soundResults->magic != expected.magic ||
                                                          soundResults->version != expected.version ||
                                                          soundResults->bytes != expected.bytes))
        soundResults = nullptr;
    dodgeResults = reinterpret_cast<const game_dodge::Data*>(GetProcAddress(rays, "SpidyDodgeData"));
    if (const game_dodge::Data expected; dodgeResults && (dodgeResults->magic != expected.magic ||
                                                          dodgeResults->version != expected.version ||
                                                          dodgeResults->bytes != expected.bytes))
        dodgeResults = nullptr;
    swingSettings = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidySwingSettings"));
    // Optional: an older ray module has no web shooter.
    startShooter = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyShooterStart"));
    sampleShooter = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyShooterSample"));
    sampleWarp = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyWarpSample"));
    sampleOffers = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyOffersSample"));
    useOffer = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyOffersUse"));
    aimOffer = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyOffersAim"));
    stopShooter = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyShooterStop"));
    wheelShooter = reinterpret_cast<BridgeCall>(GetProcAddress(rays, "SpidyShooterGadgets"));
    if (!submitInput || !sampleBridge || !stopBridge || !retargetBridge || !startRays || !submitRays ||
        !stopRays || !startSwing || !submitSwing || !sampleSwing || !stopSwing || !retargetSwing)
        return 1002;
    stopRequested = false;
    // The worker attaches each player it finds; none yet.
    native_appearance::setPlayerRecord(0);
    keepAliveDeadline = GetTickCount64() + 10000;
    message(1, 0, "Starting OpenXR");
    worker = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
    return worker ? 0 : 1003;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyXrKeepAlive(void*) {
    if (stopRequested)
        return 1102;
    keepAliveDeadline = GetTickCount64() + 5000;
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI SpidyXrStop(void*) {
    std::lock_guard life(lifecycle);
    stopRequested = true;
    if (!worker)
        return 0;
    const auto status = WaitForSingleObject(worker, 15000);
    return status == WAIT_OBJECT_0 ? 0 : 1101;
}
