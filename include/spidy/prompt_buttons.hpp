#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// The game's own button vocabulary, and the VR controls that answer it.
//
// Spidy's trouble with the game's prompts was never one prompt: VR play can
// send nine of the pad's controls and the game picks the one it asks for from
// all of them, so every story beat or encounter that wanted another one
// stopped a player ("Can't pull objects with LB + RB", "cant use Vents",
// "it says tap x repeat but tapping x opens the menu", Air Launch, the Fisk
// door, the Sable turrets). Each was patched on its own: the right stick's
// click for the game's LB + RB on October 11, that stick's pushes for its
// heal and its LT + RT on October 12.
//
// The game, though, names the button it wants, in a vocabulary it owns and
// that does not change with the moment: 203 icon ids (image offset 0x3a80300
// and up) with one icon map a device family in the UI's boot config
// (X360IconMap, XOneIconMap, DualShockIconMap, DualSenseIconMap,
// SteamDeckIconMap, SwitchProIconMap, GenericIconMap, MkbIconMap,
// SteamIconMap), and YUIIconMarkupConfig turning a text's markup into them.
// Every family's ids end in the same words - A, B, X, Y, DpadUp,
// StickRightPress, LeftShoulder, BothShoulders, BothTriggers - so one table
// over those endings answers for all nine families at once, and the letters
// the texts spell out ("LB + RB (Hold) Throw", the hint line of the Shocker
// fight) go in the same table. Nothing here is a list of the game's moments:
// a prompt says its button, this says what that asks of the pad and what the
// player does in VR instead.
namespace spidy::prompt_buttons {
// XInput's button bits, the ones game_pad::Button holds (game_pad.hpp
// asserts that they agree).
constexpr uint16_t padDpadUp = 0x1, padDpadDown = 0x2, padDpadLeft = 0x4, padDpadRight = 0x8, padStart = 0x10,
                   padBack = 0x20, padLeftThumb = 0x40, padRightThumb = 0x80, padLeftShoulder = 0x100,
                   padRightShoulder = 0x200, padA = 0x1000, padB = 0x2000, padX = 0x4000, padY = 0x8000;
// What a prompt asks of the controller: the buttons held together, each
// trigger pulled, and a stick pushed (-1, 0 or 1 an axis, y up). `hold`: its
// own word says to hold it rather than tap it.
struct Demand {
    uint16_t buttons{};
    bool leftTrigger{}, rightTrigger{}, hold{};
    int8_t leftX{}, leftY{}, rightX{}, rightY{};
    // A stick to be moved with no way named ([LS] and [RS], the QTE's stick
    // motion), and a prompt that named a key or a mouse button instead of a
    // pad control ([MKB_CUSTOM00] and up, the MkbKeyboard and MkbMouse
    // icons). The game prompts for the device it last saw, so a VR player
    // whose keyboard spoke last reads a letter with nothing to press.
    bool leftStickMotion{}, rightStickMotion{}, keyboard{};
    bool operator==(const Demand&) const = default;
    bool any() const {
        return buttons || leftTrigger || rightTrigger || leftX || leftY || rightX || rightY ||
               leftStickMotion || rightStickMotion;
    }
    Demand& operator|=(const Demand& o) {
        buttons |= o.buttons;
        leftTrigger |= o.leftTrigger;
        rightTrigger |= o.rightTrigger;
        hold |= o.hold;
        // A way already asked for stands: two prompts in one text do not cancel.
        leftX = leftX ? leftX : o.leftX;
        leftY = leftY ? leftY : o.leftY;
        rightX = rightX ? rightX : o.rightX;
        rightY = rightY ? rightY : o.rightY;
        leftStickMotion |= o.leftStickMotion;
        rightStickMotion |= o.rightStickMotion;
        keyboard |= o.keyboard;
        return *this;
    }
};
// The game's camera, out from D-pad up until D-pad up puts it away: LT held
// raises its viewfinder and RB takes the photo (measured in the game on
// October 22: the screen flashed white at RB; RT took none). Once a research
// station's hint has gone nothing names them, so while the camera's overlay
// is up (game_prompts) this is what the game asks for.
constexpr Demand cameraDemand{padRightShoulder | padDpadUp, true};
// One icon id of any family ("XBButtonBothShoulders", "PSButtonCircle",
// "SteamButtonRightTrigger", "RightStickUp"), with its family's prefix or
// without it: what it asks of the pad. An id that asks nothing of a
// controller - a gadget, an emoji, a stick with no way - gives {}; a
// keyboard key or a mouse button gives a demand with `keyboard` set and
// nothing to press. The PlayStation families name the face buttons by shape,
// so their X is the pad's A and their Square its X.
Demand forIcon(std::string_view id);
// Every button a prompt text names, together: the icon ids in its markup and
// the letters it spells ("LB + RB", "L1 + R1", "LT + RT", "R3"), with `hold`
// from its own word. A text that names none gives {}.
Demand forText(std::string_view text);
// Whether VR play sends `demand` as it stands (game_pad::Mapping::gameplay),
// with no pad layer. The baseline is Touch and Index; a Steam Frame has the
// game's own D-pad and View besides.
bool reachableInPlay(Demand);
// What the player does in VR for it, in capitals for a prompt line: the
// gameplay control where there is one ("X" for LB + RB), else the pad
// layer's ("PAD + LEFT GRIP"). Null for {} and for a mix of buttons no one
// control gives.
const char* vrControl(Demand);
// `text` with the game's LB + RB - its web yank's word [ACTION_WEB_YANK], or
// the two bumpers' tokens joined by a plus ("[BTN_LB] + [BTN_RB]", "[BTN_L1]
// + [BTN_R1]") - as [BTN_X], the X that sends them in VR play
// (game_pad::NamedPress). The game resolves its own tokens, so its prompt
// then draws its X icon there: measured in the game on October 24, the
// throw's "[ACTION_WEB_YANK] (Hold) Throw" drew "(X) (Hold) Throw". Anything
// else stays as it stands. Returns the length written; 0 where nothing was
// replaced or the text would not fit in `capacity`, and `out` is then no
// text to use.
size_t pairOnX(std::string_view text, char* out, size_t capacity);
// The HUD's marker over a thing in the world: HUD.managers::POIManager's
// AddPOI(id, category, subtype) in POIs.gfx, read from the movie's bytecode in
// the game's memory on October 24. An interact point (category 9) with the
// throwable's subtype (7) is a POIDefault that draws the icons the game gives
// it for [ACTION_WEB_YANK], LB . RB beside a diamond; the same class draws the
// generic left face button's subtype (13) as that one button, X - the button
// that sends the game's LB + RB in VR play. The subtype VR play draws a marker
// with: `subtype` itself for every other one. Measured in the game on October
// 24: the marker over a mailbox drew X. It loses the throwable's ring and its
// fill for a held throw, which only that subtype draws.
constexpr double poiInteract = 9, poiThrowable = 7, poiLeftFace = 13;
constexpr double markerSubtype(double category, double subtype) {
    return category == poiInteract && subtype == poiThrowable ? poiLeftFace : subtype;
}
// What an interact marker asks of the pad, by its own subtype: the
// throwable's web yank, LB + RB held (POIDefault.RefreshThrowable draws
// [ACTION_WEB_YANK], "(Hold) Throw"), and the X of subtype 13, which
// POIDefault draws as icon_face_button_left. {} for any other marker, or a
// subtype not known. Its 12, 14 and 15 (Y, B, A) are left to Spidy: a
// marker stays offered for many seconds (11 s straight in klibo's session),
// and B and A are Spidy's dodge and jump while it is.
// The game names these markers' buttons nowhere else: klibo's mailboxes and
// manhole cover (November 11, 0.8.6) came as AddPOI(id, 9, 7) and
// POIInteractAvailable(id, 1, '') with no text and no line of the HUD's
// prompt list, so X - which the marker showed - sent nothing and held opened
// the gadget wheel. Live while the game offers that marker's interact
// (LiveDemands::available).
constexpr Demand markerDemand(double category, double subtype) {
    if (category != poiInteract)
        return {};
    if (subtype == poiThrowable)
        return {padLeftShoulder | padRightShoulder, false, false, true};
    if (subtype == poiLeftFace)
        return {padX};
    return {};
}
// `text` with the buttons it spells out replaced by the VR controls for
// them: "LB + RB (Hold) Throw" becomes "X (Hold) Throw".
// `interactServes` (the interact button sending what a live prompt asks for,
// which game_pad did until October 20) puts "Y" where VR play has no control
// of its own, so "Tap LB" reads "Tap Y" rather than naming the pad layer. No
// session rewrites a text (game_prompts::setRewriting).
//
// An icon id is left exactly as it stands. The ids sit inside the UI's own
// markup, whose form is not known (YUIIconMarkupConfig, with MarkupText,
// IconMapping and a Separator), and a name changed inside it would be markup
// the game cannot resolve. Reading one is safe and is what feeds the interact
// button; rewriting one is not.
//
// Returns the length written; 0 where nothing was replaced, and `out` is then
// untouched.
size_t rewrite(std::string_view text, char* out, size_t capacity, bool interactServes);
// A demand in one word, so a hook can hand it to the VR thread without a
// lock and a session report can carry it: bits 0-15 the buttons, 16 the left
// trigger, 17 the right, 18 the hold, then two bits a stick axis (0 no way,
// 1 back or left, 2 forward or right) through bit 26, then 27 and 28 a
// stick to be moved any way and 29 a key or a mouse button. An empty demand
// packs to 0. Round trips through unpack.
uint32_t pack(Demand);
Demand unpack(uint32_t);
// What the game names now, kept from its UI's calls by when each came
// (game_prompts feeds it, under a lock of its own; times are milliseconds).
// A prompt call's demand lives liveMs after the last call that named or
// raised it, and its manager's Hide or Clear ends it. A text shown on a
// channel of its own - ShowText(channel, text, ...) - lives until HideText of
// that channel or another text on it, channelMs at most: the help line "Hold
// [BTN_L2] and tap [BTN_R1] to fire webs and seal the leaks" stood on screen
// through its mission's whole timer, and a subtitle's HideText is its own
// channel's. A text any other call of the UI's passes that names a button (a
// prompt this knows by no name) lives otherMs after its latest call: no Hide
// of its own is known to end it. And a state of the game's that asks for
// buttons with no text at all - its camera out, its overlay up - stands
// until the call that ends it, standMs at most. And a line of the HUD's own
// prompt list (the throw's "[ACTION_WEB_YANK] (Hold) Throw", "[BTN_Y]
// Interact"; one a kind, the widget's show and hide) stands from its show
// until its hide, standMs at most. And a marker of the HUD's over a thing in
// the world (AddPOI(id, category, subtype)) asks for what its subtype names
// (markerDemand) while the game offers its interact - POIInteractAvailable(id,
// 1, ...) until (id, 0, ...) or ClearPOI(id), standMs at most.
class LiveDemands {
  public:
    static constexpr unsigned channels = 16, kinds = 21, markers = 16;
    static constexpr uint64_t liveMs = 2000, channelMs = 60000, otherMs = 8000, standMs = 600000;
    // A prompt call: `word` what its text named (pack; 0 for none), `clears`
    // its manager taking a prompt away.
    void call(uint64_t now, uint32_t word, bool clears) {
        if (clears) {
            live_ = 0;
        } else if (word) {
            live_ = word;
            fresh_ = now;
        } else if (live_) {
            fresh_ = now;
        }
    }
    // ShowText on `channel` (clears false) or HideText (clears true).
    void text(uint64_t now, unsigned channel, uint32_t word, bool clears) {
        if (channel >= channels)
            return;
        texts_[channel] = clears ? 0 : word;
        at_[channel] = now;
    }
    // Another call's text that named `word` (0: nothing, which changes nothing).
    void other(uint64_t now, uint32_t word) {
        if (!word)
            return;
        other_ = word;
        otherAt_ = now;
    }
    // A state that asks for `word` begins (clears false) or ends (true).
    void stand(uint64_t now, uint32_t word, bool clears) {
        stand_ = clears ? 0 : word;
        standAt_ = now;
    }
    // The HUD's prompt of `kind` shown naming `word` (clears false), or
    // hidden (true).
    void prompt(uint64_t now, unsigned kind, uint32_t word, bool clears) {
        if (kind >= kinds)
            return;
        prompts_[kind] = clears ? 0 : word;
        promptAt_[kind] = now;
    }
    // The HUD's marker `id` added (AddPOI) asking for `word` (markerDemand,
    // packed; 0 for nothing), or taken away (ClearPOI: clears true). Added
    // again it keeps its offer; the oldest gives way when all are taken.
    void marker(uint64_t now, uint32_t id, uint32_t word, bool clears) {
        if (clears) {
            if (Marker* m = find(id))
                *m = {};
            return;
        }
        Marker& m = slot(id);
        m.word = word;
        m.at = now;
    }
    // Its interact offered or withdrawn (POIInteractAvailable(id, on, ...)),
    // in either order with its AddPOI. Returns what it asks for now: its
    // word while offered, else 0.
    uint32_t available(uint64_t now, uint32_t id, bool on) {
        Marker* m = on ? &slot(id) : find(id);
        if (!m)
            return 0;
        m->on = on;
        m->at = now;
        return on ? m->word : 0;
    }
    // A load or the HUD's shutdown: every marker goes with it.
    void clearMarkers() {
        for (auto& m : markers_)
            m = {};
    }
    // Everything live at `now`, together; what has run out is dropped.
    Demand demand(uint64_t now) {
        Demand out;
        if (live_ && now - fresh_ > liveMs)
            live_ = 0;
        if (live_)
            out |= unpack(live_);
        if (other_ && now - otherAt_ > otherMs)
            other_ = 0;
        if (other_)
            out |= unpack(other_);
        if (stand_ && now - standAt_ > standMs)
            stand_ = 0;
        if (stand_)
            out |= unpack(stand_);
        for (unsigned c = 0; c < channels; ++c) {
            if (texts_[c] && now - at_[c] > channelMs)
                texts_[c] = 0;
            if (texts_[c])
                out |= unpack(texts_[c]);
        }
        for (unsigned k = 0; k < kinds; ++k) {
            if (prompts_[k] && now - promptAt_[k] > standMs)
                prompts_[k] = 0;
            if (prompts_[k])
                out |= unpack(prompts_[k]);
        }
        for (auto& m : markers_) {
            if (m.on && now - m.at > standMs)
                m.on = false;
            if (m.on && m.word)
                out |= unpack(m.word);
        }
        return out;
    }
    void clear() {
        *this = {};
    }

  private:
    struct Marker {
        uint32_t id{}, word{};
        uint64_t at{};
        bool used{}, on{};
    };
    Marker* find(uint32_t id) {
        for (auto& m : markers_)
            if (m.used && m.id == id)
                return &m;
        return nullptr;
    }
    // `id`'s own, else a free one, else the oldest, made `id`'s.
    Marker& slot(uint32_t id) {
        if (Marker* m = find(id))
            return *m;
        Marker* m = &markers_[0];
        for (auto& candidate : markers_) {
            if (!candidate.used) {
                m = &candidate;
                break;
            }
            if (candidate.at < m->at)
                m = &candidate;
        }
        *m = {};
        m->used = true;
        m->id = id;
        return *m;
    }
    uint32_t live_{}, texts_[channels]{}, other_{}, stand_{}, prompts_[kinds]{};
    uint64_t fresh_{}, at_[channels]{}, otherAt_{}, standAt_{}, promptAt_[kinds]{};
    Marker markers_[markers]{};
};
// Every control of the pad the game can ask for, so a check can cover them
// all instead of the ones a player has reported.
struct Control {
    const char* name;
    Demand demand;
};
std::span<const Control> controls();
} // namespace spidy::prompt_buttons
