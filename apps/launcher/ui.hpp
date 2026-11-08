#pragma once
#include "system.hpp"
#include "update.hpp"
#include "imgui.h"
#include <functional>
#include <vector>

namespace launcher {

// Spidy's print (the house style of its videos and README), and Windows' own for the rest.
struct Fonts {
    ImFont* body{};      // Barlow Medium: what the launcher says
    ImFont* semibold{};  // Barlow SemiBold: the names of options
    ImFont* label{};     // Barlow Condensed Bold: buttons, tabs, stamps, the name SPIDY
    ImFont* kicker{};    // Barlow Condensed SemiBold: card titles and small lines in capitals
    ImFont* display{};   // Barlow Condensed Black Italic: headlines, START VR
    ImFont* narration{}; // Comic Neue Bold Italic: the caption box, the narrator
    ImFont* mono{};      // Consolas: the session log
    bool icons{};
};

// A picture compiled into the launcher (main.cpp): its own size in pixels, and a texture of it that many pixels
// wide (none if it could not be read).
struct Picture {
    ImVec2 size;
    std::function<ImTextureID(int width)> texture;
};

// Colours and metrics before DPI scaling.
void theme(ImGuiStyle& style);

class App {
public:
    // `controls`: the README's pictures of the controls, in their order. `updatedFrom`: the version an update just
    // replaced (the launcher started again), else empty.
    App(HWND window, Fonts fonts, std::vector<Picture> controls, std::string updatedFrom = {});
    ~App();
    // Reads the scan, checks and session; runs every loop, even while minimized.
    void poll();
    void frame(float scale);
    // Something on screen moves (a check or a session): draw more often in the background.
    bool busy();
    // The window's close button: false while VR runs (the window asks first).
    bool allowClose();
    bool shouldQuit() const { return quit_; }
    // After an update: the folder whose new launcher starts once this one has closed (main.cpp).
    std::wstring restartFolder() const {
        return quit_ && update_.state == UpdateState::installed ? installFolder_ : std::wstring();
    }
    // A profile file to import: one picked, or dropped on the window (main.cpp).
    void importFile(const std::wstring& path);

private:
    enum class Tab { play, options, controls, about };
    enum class Mark { ok, warning, error, busy, info };
    // A button in the red of the one thing to do, in the tip jar's yellow (Ko-fi), or plain.
    enum class Ink { plain, primary, support };
    // The caption's bar: the narrator's yellow, red for what went wrong, grey for plain facts.
    enum class Caption { narrator, alert, fact };
    // Which saved profile these settings are.
    struct ProfileState {
        const SavedProfile* current{};    // the one they are: last used or saved, else the first that holds them
        const SavedProfile* remembered{}; // the one last used or saved, changed since or not
        bool changed{};                   // changed since `remembered` was used
    };

    float S(float value) const { return value * scale_; }
    void header();
    void tabs();
    void play(ImVec2 origin, ImVec2 size);
    void options(ImVec2 origin, ImVec2 size);
    void controls(ImVec2 origin, ImVec2 size);
    void about(ImVec2 origin, ImVec2 size);
    // START VR, what the session does in words, and how far it got.
    void startPanel(ImVec2 size);
    void setupCard(float width);
    void logCard(ImVec2 size);
    void shortcutBanner(float width);
    // The update offer, its progress or the update just made, above the Play tab; returns its height (0: none).
    float updateBanner(float width);
    void updatesCard(float width);
    // The README's Ko-fi and Discord cards, one under the other.
    void supportCards(float width);
    // Why the update cannot start now; empty when it can.
    std::string updateBlocker();
    void modals();

    bool beginCard(const char* id, ImVec2 size, int childFlags = 0);
    void endCard();
    void cardTitle(const char* title, float rightWidth = 0);
    void setupRow(Mark mark, const char* title, const std::string& detail, float actionWidth,
                  const std::function<void()>& action, bool last = false);
    // `warning`: the help is a warning (yellow).
    void option(const char* title, const char* help, float controlWidth, const std::function<void()>& control,
                bool warning = false);
    // Settings profiles (spidy/launcher_profile.hpp): the Options tab's Profile card and its menu.
    void profileCard(float width);
    void playProfile(float width, bool running);
    std::optional<SavedProfile> profileChoices(const ProfileState& state);
    ProfileState profileState();
    spidy::launcher::PlaySettings playSettings() const { return {settings_.options, tabOnly_}; }
    // Reads the saved profiles and the tab's own settings again.
    void refreshProfiles();
    // Puts the profile's settings in place of these: the options, and the tab's own settings in spidy_vr.ini.
    // Settings no profile holds are kept as kPreviousProfile first, so that this can be undone.
    void useProfile(const Profile& profile, const std::string& name, bool builtIn = false);
    // Saves it under its name; false (and the card says so) when that failed.
    bool keepProfile(const Profile& profile);
    // A profile to import from a text (`source` names where it came from): its preview, or why not.
    void openImport(std::string_view text, const std::string& fallbackName, const std::string& source);
    // What the Profile card says for a while, in place of its help.
    void profileStatus(std::string message, bool warning = false);
    void profileModals();
    void statusMark(ImDrawList* draw, ImVec2 center, Mark mark);
    // A button in label capitals; "label##id" as ImGui's own; `icon` is a Segoe glyph or null.
    bool button(const char* label, float width, Ink ink = Ink::plain, const char* icon = nullptr);
    // How a sticker is printed: its fill, its fill under the pointer, lettering, keyline and drop (0: none).
    struct Paint {
        ImU32 fill, hover, lettering, keyline, drop;
    };
    // A big button in display capitals, with an optional small line under them; with a drop it sinks onto it
    // while pressed.
    bool sticker(const char* id, const char* icon, const char* label, const char* line, ImVec2 size,
                 const Paint& paint, bool enabled = true);
    bool toggle(const char* id, bool* value);
    // ImGui's slider, drawn as a gauge filled up to the value.
    bool slider(const char* id, int* value, int lowest, int highest, const char* format);
    bool link(const char* label);
    // The narrator's words at `at`, `width` wide: capitals beside a bar of the caption's colour, `emphasis` after
    // them in yellow (red for an alert); returns their height, and with no draw list only measures them.
    float captionBox(ImDrawList* draw, ImVec2 at, float width, const std::string& text,
                     Caption paper = Caption::narrator, const std::string& emphasis = {});
    // A sticker stating one fact, its left edge at `at.x`, centred on `at.y`; a `dot` colour blinks on the beat
    // before the words. Returns its width.
    float stamp(ImDrawList* draw, ImVec2 at, const char* text, ImU32 fill, ImU32 lettering, ImU32 keyline,
                float size, float tilt, ImU32 dot = 0);
    // The name SPIDY in the misprint, `size` high; returns its width.
    float wordmark(ImDrawList* draw, ImVec2 at, float size);
    void badge(ImDrawList* draw, ImVec2 center, float radius);

    std::vector<std::string> blockers();
    // The eye size the options ask for: the render scale of the checked headset's recommendation, or of
    // 3072 x 3264 before a check.
    std::array<uint32_t, 2> eyeSize();
    const Runtime* runtime();
    // What "Copy for Discord" copies: Spidy's version, this PC, the options a helper asks about and the
    // last session, in a few lines.
    std::string summary();
    void chooseDefaultRuntime();
    // closeGame: the session first closes a running game that already holds Spidy (kCloseGameExit).
    void start(bool closeGame = false);
    void restartElevated();
    void rescan();
    void save() { saveSettings(settings_); }
    void copyLog();

    HWND window_;
    Fonts fonts_;
    std::vector<Picture> controlsPictures_;
    float scale_ = 1;
    Tab tab_ = Tab::play;
    int optionPage_ = 0; // category, Profiles, or Diagnostics; navigation is not a saved setting
    Settings settings_;
    Scanner scanner_;
    Scan scan_;
    HeadsetCheck headset_;
    RuntimeInstall vcInstall_;
    Session session_;
    std::vector<LogLine> log_;
    size_t logCount_{};
    bool scrollLog_{};
    std::string startError_;
    bool wantClose_{}, closeAfterStop_{}, quit_{};
    // A session ended because the game runs as administrator and Spidy does not: the offer to start
    // again as administrator, and why that did not work.
    bool sessionRan_{}, wantAdministrator_{};
    std::string administratorError_;
    // A session ended because the running game already holds Spidy: the offer to close it and start again.
    bool wantCloseGame_{};
    Outcome lastInstall_ = Outcome::idle;
    std::string shortcutMessage_;
    // ImGui's time when "Copy for Discord" last copied: its button says "Copied" for two seconds.
    double summaryCopied_ = -2;
    // Updates: the player's copy of Spidy (empty in a checkout), and the banner's state.
    std::wstring installFolder_;
    Updater updater_;
    UpdateStatus update_;
    std::string updatedFrom_;
    bool updateLater_{};
    // Settings profiles: those saved, the SPIDY VR tab's own settings (spidy_vr.ini), the card's message and
    // until when it shows, and the profile being imported, waiting in its preview, with the name it gets.
    std::vector<SavedProfile> profiles_;
    spidy::vr_settings::Values tabOnly_;
    bool profilesStale_{};
    std::string profileStatus_;
    double profileStatusUntil_{};
    bool profileWarning_{};
    std::optional<Profile> importing_;
    std::array<char, 192> profileName_{};
    bool wantSaveProfile_{}, wantImport_{};
    // The option rows of a card draw a rule between them, not above the first.
    bool firstOption_ = true;
};

} // namespace launcher
