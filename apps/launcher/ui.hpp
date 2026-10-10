#pragma once
#include "system.hpp"
#include "update.hpp"
#include "imgui.h"
#include <functional>

namespace launcher {

struct Fonts {
    ImFont* body{};
    ImFont* semibold{};
    ImFont* bold{};
    ImFont* title{};
    ImFont* mono{};
    bool icons{};
};

// Colours and metrics before DPI scaling.
void theme(ImGuiStyle& style);

class App {
public:
    // `updatedFrom`: the version an update just replaced (the launcher started again), else empty.
    App(HWND window, Fonts fonts, std::string updatedFrom = {});
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

private:
    enum class Tab { play, controls, about };
    enum class Mark { ok, warning, error, busy, info };

    float S(float value) const { return value * scale_; }
    void header();
    void tabs();
    void play(ImVec2 origin, ImVec2 size);
    void controls(ImVec2 origin, ImVec2 size);
    void about(ImVec2 origin, ImVec2 size);
    void setupCard(float width);
    void optionsCard(ImVec2 size);
    void startBlock(ImVec2 size);
    void logCard(ImVec2 size);
    void shortcutBanner(float width);
    // The update offer, its progress or the update just made, above the Play tab; returns its height (0: none).
    float updateBanner(float width);
    void updatesCard(float width);
    // Why the update cannot start now; empty when it can.
    std::string updateBlocker();
    void modals();

    bool beginCard(const char* id, ImVec2 size, int childFlags = 0);
    void endCard();
    void cardTitle(const char* title, float rightWidth = 0);
    void setupRow(Mark mark, const char* title, const std::string& detail, float actionWidth,
                  const std::function<void()>& action, bool last = false);
    void option(const char* title, const char* help, float controlWidth, const std::function<void()>& control);
    void statusMark(ImDrawList* draw, ImVec2 center, Mark mark);
    bool pillButton(const char* id, const char* icon, const char* label, ImU32 fill, ImU32 hover, ImU32 text,
                    ImVec2 size, ImFont* font = nullptr, float fontSize = 15);
    bool bigButton(const char* id, const char* icon, const char* label, ImVec2 size, bool primary, bool enabled = true);
    bool secondaryButton(const char* label, float width);
    bool toggle(const char* id, bool* value);
    bool link(const char* label);
    void badge(ImDrawList* draw, ImVec2 center, float radius);
    void web(ImDrawList* draw, ImVec2 corner, float radius);

    std::vector<std::string> blockers();
    // The eye size the options ask for: the render scale of the checked headset's recommendation, or of
    // 3072 x 3264 (VR_COMMIT_MB's) before a check; and what the game in VR then commits, in GB.
    std::array<uint32_t, 2> eyeSize();
    double neededCommitGb();
    const Runtime* runtime();
    void chooseDefaultRuntime();
    void start(bool memoryConfirmed);
    void restartElevated();
    void rescan();
    void save() { saveSettings(settings_); }

    HWND window_;
    Fonts fonts_;
    float scale_ = 1;
    Tab tab_ = Tab::play;
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
    bool wantLowMemory_{}, wantClose_{}, closeAfterStop_{}, quit_{};
    // A session ended because the game runs as administrator and Spidy does not: the offer to start
    // again as administrator, and why that did not work.
    bool sessionRan_{}, wantAdministrator_{};
    std::string administratorError_;
    Outcome lastInstall_ = Outcome::idle;
    double lastRefresh_{};
    std::string shortcutMessage_;
    // Updates: the player's copy of Spidy (empty in a checkout), and the banner's state.
    std::wstring installFolder_;
    Updater updater_;
    UpdateStatus update_;
    std::string updatedFrom_;
    bool updateLater_{};
};

} // namespace launcher
