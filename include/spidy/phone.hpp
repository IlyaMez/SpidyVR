#pragma once
#include "gadget_wheel.hpp"
#include "math.hpp"
#include "tracking.hpp"
#include "vertex.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Spidy's phone (the player, October 17: "an overlay of something that looks
// like a flat phone screen ... I can start stop go back a track and auto play
// music"). Drawn from a back pocket, it opens in the hand that drew it
// (Phone). The stick picks a button, A presses it, B puts the phone away. The
// music goes on with the phone away. Songs are the files in the Music folder
// beside the launcher (phone_music.hpp).
namespace spidy {
enum class PhoneButton : int { previous, play, next, autoplay };
constexpr int phoneButtons = 4;
// The phone's screens: its home and the apps that have one (the player,
// October 17: "a home screen with apps on it ... look more like the fnsm app
// from the 2nd game").
enum class PhoneScreen : int { home, music, gear };
// The apps on the home screen in their order: music, the game's menu, gear
// and the camera. The menu and the camera have no screen of their own: each
// puts the phone away and opens the game's own. MENU is the game's Back, its
// menu of the map, suits and skills (the user, October 23: "lets move the
// menu we open with x to the phone"; a tap of X was it until then, and MENU
// took the place of MAP, a screen of hideouts that said COMING SOON). CAMERA
// is its D-pad up, its camera (hantop, October 22, for the research stations
// that ask for photos: "add a camera option to the in-game phone"). VR play
// has no Back or D-pad up of its own.
constexpr int phoneApps = 4, phoneMenuApp = 1, phoneCameraApp = 3;
// The controllers while the phone is out, as steps: the stick pushed one way
// (x: -1 left, 1 right; y: -1 down, 1 up), A, B, and the volume from the
// triggers, a pull of the left one a step down and of the right one a step up
// (the player, October 17: "left trigger for down and right trigger for up").
// Each counts once until let go. Each opening counts everything as held, so
// the press that opened the phone does nothing.
class PhoneControls {
  public:
    static constexpr float pulled = .6f, released = .3f, pushed = .6f, centred = .3f;
    struct Event {
        int x{}, y{};
        bool pressed{}; // A
        bool closed{};  // B
        int volume{};   // -1 a step down, 1 a step up
    };
    // stick: -1 to 1 each way. a, b: held now. triggers: 0 to 1.
    Event update(float stickX, float stickY, bool a, bool b, float leftTrigger = 0, float rightTrigger = 0);
    void reset() {
        aHeld_ = bHeld_ = stickHeld_ = leftHeld_ = rightHeld_ = true;
    }

  private:
    bool aHeld_ = true, bHeld_ = true, stickHeld_ = true, leftHeld_ = true, rightHeld_ = true;
};
// What the phone's screens make of those steps. On the home screen the stick
// picks an app and A opens it, B puts the phone away; in an app B goes home.
// In MUSIC left and right pick a button and A presses it; in GEAR up and down
// scroll the list. A on MENU or CAMERA puts the phone away, each its own
// action.
enum class PhoneAction : int { none, close, previous, play, next, autoplay, camera, menu };
class PhoneUi {
  public:
    // Rows GEAR shows at once.
    static constexpr int gearShown = 6;
    // gearRows: how many rows GEAR has now.
    PhoneAction update(const PhoneControls::Event& e, int gearRows);
    PhoneScreen screen() const {
        return screen_;
    }
    int app() const {
        return app_;
    }
    PhoneButton button() const {
        return button_;
    }
    int gearTop() const {
        return gearTop_;
    }
    // Each opening starts on the home screen, MUSIC picked.
    void reset() {
        screen_ = PhoneScreen::home;
        app_ = 0;
        button_ = PhoneButton::play;
        gearTop_ = 0;
    }

  private:
    PhoneScreen screen_ = PhoneScreen::home;
    int app_{}, gearTop_{};
    PhoneButton button_ = PhoneButton::play;
};
// The volume's step, and where it starts until the player sets it, percent.
constexpr int phoneVolumeStep = 10, phoneVolume = 80;
// The phone as it is drawn: open, its frame in the world, and what its screen
// shows.
struct PhoneView {
    bool open{};
    WheelFrame frame{};
    float age{};
    PhoneScreen screen = PhoneScreen::home;
    int app{}; // the app picked on the home screen
    PhoneButton lit = PhoneButton::play;
    // GEAR: the player's gadgets in the gadget wheel's places (gadget_wheel.hpp),
    // from gearTop on.
    struct GearRow {
        char name[24]{};
        int ammo{}, most{}; // most 0: none to count
        bool equipped{};
    };
    GearRow gear[gadgetPlaces]{};
    int gearCount{}, gearTop{};
    // failed: the song could not be played (no player for its kind of file).
    bool playing{}, autoplay = true, failed{};
    int track{}, tracks{}; // track counts from 0; tracks 0: no songs
    int volume = phoneVolume; // percent
    char song[64]{};
    // PC MEDIA (phone_pc_media.hpp): MUSIC is what the PC plays, `source` the
    // app it plays in; tracks 1 while it plays anything, 0 with nothing.
    // noVolume: no volume of the app's to show.
    bool pcMedia{}, noVolume{};
    char source[24]{};
};
// The phone as an eye image has it (NativeEyeFrame): open as the game rendered
// it, and the image's own way from tracking space into its world. Where the
// phone is in tracking space and what its screen shows are the newest as the
// image goes out (Phone::place): a phone in the hand is placed from the
// newest hand pose, not where the hand was when the game rendered the image (a
// frame or more behind: "the phone is laggy kind of when its in my hand",
// October 17).
struct PhoneImage {
    bool open{};
    Pose toWorld{};
};
// The phone held in a hand: just above it, facing the eyes, its top up (the
// player, October 17: "make the phone kinda stick to you hand until u press
// b"). In front of the head instead if the hand is not to be had.
WheelFrame phoneFrameAt(Pose head, Vec3 hand);
// Which of the controllers' controls the phone has (Phone). It takes a
// control, and gives one back, only while that control is let go of: one held
// as the phone comes out stays the game's until let go - a web held keeps
// holding the player (October 19: drawing the phone mid-swing let go of the
// web), a gadget wheel held open stays open -, and one held as the phone goes
// away stays the phone's until let go - the B that put it away, a grip
// squeezed meanwhile. So nothing sees a press or a release the player did not
// make, the phone included: a B held from the pause menu back into play is no
// press of the phone's. The menu button is always the game's.
class PhoneHold {
  public:
    // Let go of: a trigger or a grip under this, a stick nearer its centre.
    static constexpr float released = .2f;
    // live: the phone wants the controllers. `frame` keeps the game's controls
    // and has the phone's let go of; `phone` gets the phone's and has the
    // game's let go of.
    void update(bool live, XrFrame& frame, XrFrame& phone);
    // Whether the phone has any control now.
    bool any() const {
        return mine_ != 0;
    }

  private:
    uint32_t mine_{}; // a bit per control the phone has (phone.cpp)
};
// The phone in VR play (game_xr.cpp's XR worker): open from a back-pocket
// draw until B; live - its screen shown, the controllers its own (PhoneHold)
// - while VR shows play and no calibration has the controllers. It opens in
// the hand that drew it and stays in it until B (a hand the headset loses
// leaves it where it last was).
class Phone {
  public:
    // What the phone made of its controls this frame (take).
    struct Use {
        PhoneControls::Event controls;
        PhoneAction action = PhoneAction::none;
        unsigned stick{}; // the hand whose stick it read
    };
    // First thing each frame: the phone takes its controls out of `frame`
    // (PhoneHold) and, live, works its screens with them. gearRows: how many
    // rows GEAR has. B on the home screen puts the phone away at once, and so
    // does A on MENU or CAMERA (PhoneAction::menu, PhoneAction::camera: the
    // caller opens the game's menu, or takes its camera out).
    Use take(XrFrame& frame, int gearRows);
    // Once the frame knows. play: VR shows play and no calibration has the
    // controllers. head, grips: tracking space; tracked: each grip's.
    // drawHand: the hand a back-pocket draw brought the phone out with this
    // frame (PocketDraw), at `drawnAt`; -1 none. True as the phone opens.
    bool update(float seconds, bool play, const Pose& head, const Pose grips[2], const bool tracked[2],
                int drawHand = -1, Vec3 drawnAt = {});
    bool open() const {
        return open_;
    }
    bool live() const {
        return live_;
    }
    // Seconds since it opened.
    float age() const {
        return age_;
    }
    // The hand that drew it: 0 the left, 1 the right; -1 before any draw.
    int hand() const {
        return hand_;
    }
    // Where it is now, in tracking space.
    const WheelFrame& place() const {
        return place_;
    }
    const PhoneUi& ui() const {
        return ui_;
    }
    // Whether the phone has any control (PhoneHold::any).
    bool holds() const {
        return hold_.any();
    }

  private:
    PhoneControls controls_;
    PhoneUi ui_;
    PhoneHold hold_;
    bool open_{}, live_{};
    int hand_ = -1;
    float age_{};
    WheelFrame place_{};
};
// The phone drawn from a back pocket (the player, October 17: "like im
// pulling it from my back pocket then the screen pops up"). A grip squeezed
// with its hand behind a hip, then that hand brought up in front within
// drawSeconds, still squeezed: the phone opens there. That grip is the
// draw's from its squeeze until it is let go (hands()), so it shoots no web.
// Let go or too slow, nothing opens. Nothing begins or comes out outside VR
// play; a moment out of it (a game hitch) does not end a draw.
//
// The pocket is the body's, not the head's: the body faces where the head
// has for a while - it turns with the head only past yawDeadZone, at up to
// yawSpeed, and drifts toward it at yawDrift a second meanwhile - so a look
// over a shoulder does not bring a hand hanging at the side into a pocket
// (October 19: 20 degrees of a turn did, and a hanging arm's grip shot no
// web), nor turn the pocket away from the hand reaching for it. Players look
// at the pocket they reach for, and with body_ik's 34 degrees and half the
// rest a second the pocket turned with that look (October 25, dasvors: "it's
// based on where your head is pointing"), so it has its own, wider ones. And
// the pocket is measured from the neck, which a turn or a nod of the head
// does not move, not from the eyes, which go round it. A hand the headset loses
// behind the back (the cameras of a headset do not see there) neither lets
// go of the grip nor ends the draw: the grip is the squeeze's alone, and a
// squeeze begins a draw where the hand was last seen, lostSeconds ago at most,
// carried on the way it was going for carrySeconds at most: a Quest loses a
// controller at the side of the hip on its way back, and where it was last
// seen is no pocket (October 25: "I'm going through TSA in my room"; players
// found theirs above the back of the knee, where the cameras still see it). A
// hand hanging still at the side goes nowhere.
class PocketDraw {
  public:
    // The neck: the eyes are neckUp over it and neckAhead in front of it, in
    // the head's own axes.
    static constexpr float neckUp = .1f, neckAhead = .09f;
    // In the body's level axes from the neck, metres: a pocket is
    // `pocketBelow` to `pocketAbove` under it, `pocketSide` to `pocketOut` to
    // either side, and at least `behind` back of it (a hand on a back pocket
    // is about 11 cm behind the neck, one hanging at the side about 1 cm: 20
    // and 8 behind the eyes, as measured until October 27). The hand is out
    // in front once it is `front` ahead of the eyes and no lower than `raised`
    // under them.
    // Seated (SEATED), the back pocket is in the chair: the pocket begins
    // `pocketAboveSeated` under the neck, beside or behind the hip on the
    // seat (GolexPrime, October 31: 17 of 18 squeezes seated missed it as too
    // high, the hand 35 cm under the neck and 16 cm behind it).
    static constexpr float pocketBelow = 1.f, pocketAbove = .45f, pocketAboveSeated = .25f, pocketSide = .05f,
                           pocketOut = .4f, behind = .06f, front = .15f, raised = .65f, drawSeconds = 1.5f,
                           pressed = .6f, released = .3f, yawDeadZone = 1.2f, yawSpeed = 6, yawDrift = .2f,
                           lostSeconds = .5f;
    // A lost hand's way: its last velocity, measured over about `smoothing`,
    // no faster than carrySpeed, for carrySeconds at most.
    static constexpr float carrySeconds = .3f, carrySpeed = 2.5f, smoothing = .05f;
    // Why a grip pressed for the phone began no draw, the first thing wrong:
    // the headset had lost the hand longer than lostSeconds, it was above the
    // pocket or below it, too near the middle or too far out, or not behind.
    enum Miss : uint32_t { lost, high, low, central, wide, ahead, misses };
    // What the session report says of the draws (the player's question, October
    // 27: why can so many not draw it?). A press counts where it may have been
    // one for the phone: in play, the hand lower than `reachBelow` under the
    // neck and no more than `reachAhead` in front of it, or lost.
    static constexpr float reachBelow = .3f, reachBelowSeated = .2f, reachAhead = .1f;
    struct Stats {
        // Such presses; draws begun at a pocket; phones out; draws that ended
        // without one, too slow or let go of on the way; presses that began
        // none, by Miss.
        uint32_t presses{}, begun{}, draws{}, tooSlow{}, letGo{}, missed[misses]{};
        // The latest miss: its Miss, the hand from the neck in the body's
        // level axes (metres: x right, y up, z ahead), how long the headset
        // had lost it (seconds, 0 seen), and how far the head looked from the
        // body's way (radians, toward the left positive).
        uint32_t lastMiss = misses;
        Vec3 lastHand{};
        float lastLost{}, lastLook{};
    };
    const Stats& stats() const {
        return stats_;
    }
    // seconds: since the previous update. play: the controllers play VR.
    // head, grips: in one space; tracked: each grip's pose; squeezes: each
    // hand's. True as the phone comes out: hand() says where.
    bool update(float seconds, bool play, const Pose& head, const Pose grips[2], const bool tracked[2],
                const float squeezes[2]);
    // SEATED on: the pocket is the seated one (pocketAboveSeated).
    void seated(bool on) {
        seated_ = on;
    }
    bool seated() const {
        return seated_;
    }
    // A bit per hand whose grip is the draw's.
    unsigned hands() const {
        return hands_;
    }
    Vec3 hand() const {
        return hand_;
    }
    // The hand that drew the latest phone: 0 the left, 1 the right.
    int drewWith() const {
        return drewWith_;
    }
    unsigned draws() const {
        return draws_;
    }
    // The way the body faces: radians about the up from -z toward -x, as
    // Quat::yaw turns -z.
    float yaw() const {
        return yaw_;
    }

  private:
    bool down_[2]{}, yawSet_{}, seated_{};
    unsigned hands_{}, draws_{};
    int drawing_ = -1, drewWith_{};
    float time_{}, yaw_{}, lost_[2]{lostSeconds + 1, lostSeconds + 1};
    Vec3 hand_{}, seen_[2]{}, going_[2]{};
    Stats stats_;
};
// The phone as the eyes see it (the user, October 24: "currently its very
// flat with tons of zfighting"): a body 9 mm deep with rounded corners and
// edges - black glass on its face, a graphite frame round it with its
// buttons, a red back with the camera's bump - lit as from above and behind
// the player, so its edges catch the light as it turns in the hand. Back,
// sides and then face, in that order, so a drawing without a depth buffer
// (the preview) shows it right from the front. viewer: the eyes' middle.
// Its screen is a picture of its own (appendPhoneScreen), not part of it.
void appendPhone(std::vector<Vertex>& out, const PhoneView& view, Vec3 viewer);
// The screen's picture: half its size in metres, and what it is cleared to
// (the glass's black, which its rounded corners show).
constexpr float phoneScreenHalfWidth = .0375f, phoneScreenHalfHeight = .08f;
Vec3 phoneScreenGlass();
// What the screen shows, in the screen's own plane: x right and y up in
// metres from its middle, z out of it, a layer further out covering one
// further in (D3D12Renderer::Picture).
void appendPhoneScreen(std::vector<Vertex>& out, const PhoneView& view);
// Where the picture goes in the world: just over the glass, bottom left,
// bottom right, top right and top left, as the phone grows out of its middle.
std::array<Vec3, 4> phoneScreenCorners(const PhoneView& view);
// The picture laid onto those corners as overlay vertices, for a drawing
// with no picture of its own (the preview, the tests). In place.
void placePhoneScreen(std::vector<Vertex>& picture, const PhoneView& view);
// What the phone shows for a song's file name (UTF-8): no folder or extension,
// an accented Latin letter as its letter ("Beyoncé" as BEYONCE), and only what
// the overlay's font draws. A name with nothing of that (a Hebrew, Cyrillic or
// Japanese one) is TRACK and its number: `track`, from 0.
std::string phoneSongName(const std::string& fileName, int track);
// PC MEDIA's song (UTF-8): ARTIST - TITLE as phoneSongName shows a name, its
// dots kept ("MR. BRIGHTSIDE"); the title alone without an artist, and
// SONG with neither drawable.
std::string phoneMediaSong(const std::string& artist, const std::string& title);
// The app a song plays in, from the name Windows' media controls give it (its
// AppUserModelID): "Spotify.exe" and Spotify's Store package as SPOTIFY,
// "MSEdge" as EDGE, an unreadable one (Firefox names itself by a hash) as PC.
std::string phoneMediaSource(const std::string& appId);
// That app's audio session in Windows' mixer: a process (its file, with or
// without the folder) whose name, without .exe, is in the app's ID
// ("Spotify.exe" in "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify",
// "msedge.exe" in "MSEdge"), ignoring case.
bool phoneMediaAppMatches(const std::string& appId, const std::string& process);
} // namespace spidy
