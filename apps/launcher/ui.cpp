#include "ui.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace launcher {
namespace {

// Segoe MDL2 Assets / Segoe Fluent Icons glyphs.
constexpr const char* kHeart = "\xEE\xAD\x92";
constexpr const char* kPlay = "\xEE\x9D\xA8";
constexpr const char* kStop = "\xEE\x9C\x9A";
constexpr const char* kFolder = "\xEE\xA2\xB7";
constexpr const char* kFolderOpen = "\xEE\xA0\xB8";
constexpr const char* kRefresh = "\xEE\x9C\xAC";
constexpr const char* kCopy = "\xEE\xA3\x88";
constexpr const char* kOpen = "\xEE\xA2\xA7";
constexpr const char* kShortcut = "\xEE\x9C\x9B";

// The header and the tab bar, before DPI scaling.
constexpr float kHeaderHeight = 94, kTabsHeight = 50;
// The lean of the print's swipes and colour fields: tan 12 degrees.
constexpr float kLean = 0.2126f;

struct Rgb {
    int r, g, b;
};
// Spidy's print, the style of its videos and README, toned down for a window looked at a while: the night
// ground and surfaces lifted from it, paper, and the plates kept small. Red is the brand and the one thing to
// do; yellow the narrator and what needs a look; cyan what is fine; violet what is switched on; cyan and
// magenta the misprint. The hands' blue and orange stay the headset's.
constexpr Rgb kGround{13, 11, 28}, kSurface{23, 20, 44}, kSurfaceHigh{33, 29, 61}, kSurfaceHover{45, 39, 82},
    kLine{52, 45, 96}, kWell{15, 13, 31}, kDots{36, 30, 78}, kDotsStrong{48, 38, 104}, kWarmDots{62, 52, 40},
    kInk{11, 10, 20}, kPaper{247, 245, 238}, kPaperDim{185, 180, 208}, kFaint{147, 141, 181},
    kLogText{205, 201, 222}, kViolet{59, 31, 179}, kAccent{104, 82, 232}, kAccentHover{124, 104, 240}, kRed{227, 38, 47},
    kRedLift{255, 72, 80}, kRedDrop{120, 16, 26}, kYellow{255, 225, 77}, kCyan{18, 214, 255},
    kMagenta{255, 46, 147}, kWhite{255, 255, 255};

// A colour faded with the current style alpha, so disabled custom widgets fade too.
ImU32 col(Rgb c, float alpha = 1) {
    const float a = std::clamp(alpha * ImGui::GetStyle().Alpha, 0.f, 1.f);
    return IM_COL32(c.r, c.g, c.b, static_cast<int>(a * 255));
}

ImVec4 vec(Rgb c, float alpha = 1) { return ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha); }

ImVec2 add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

std::string format(const char* pattern, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, pattern);
    vsnprintf(buffer, sizeof(buffer), pattern, args);
    va_end(args);
    return buffer;
}

ImVec2 measure(ImFont* font, float size, const char* text, float wrap = 0) {
    return font->CalcTextSizeA(size, FLT_MAX, wrap, text);
}

void handCursor() {
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

// Capitals, as the print sets everything but plain text (ASCII letters; the rest stays).
std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return out;
}

// Text with `spacing` em between its letters, as the print sets labels (0.05) and kickers (0.14).
float spacedWidth(ImFont* font, float size, const char* text, float spacing) {
    float width = 0;
    for (const char* s = text; *s;) {
        unsigned int c = 0;
        const int n = ImTextCharFromUtf8(&c, s, nullptr);
        width += font->CalcTextSizeA(size, FLT_MAX, 0, s, s + n).x + (s[n] ? spacing * size : 0);
        s += n;
    }
    return width;
}

void spacedText(ImDrawList* draw, ImFont* font, float size, ImVec2 at, ImU32 color, const char* text, float spacing) {
    for (const char* s = text; *s;) {
        unsigned int c = 0;
        const int n = ImTextCharFromUtf8(&c, s, nullptr);
        draw->AddText(font, size, at, color, s, s + n);
        at.x += font->CalcTextSizeA(size, FLT_MAX, 0, s, s + n).x + spacing * size;
        s += n;
    }
}

// Lettering printed twice out of register under itself: a cyan ghost to the left, a magenta one to the right.
void misprint(ImDrawList* draw, ImFont* font, float size, ImVec2 at, ImU32 color, const char* text,
              float spacing = 0) {
    const float offset = std::max(1.f, std::round(size * 0.035f));
    const auto print = [&](ImVec2 p, ImU32 c) {
        if (spacing > 0)
            spacedText(draw, font, size, p, c, text, spacing);
        else
            draw->AddText(font, size, p, c, text);
    };
    print(ImVec2(at.x - offset, at.y), col(kCyan));
    print(ImVec2(at.x + offset, at.y), col(kMagenta));
    print(at, color);
}

// Turns what was drawn from vertex `start` on by `degrees` about `center`: nothing printed sits level.
void tilt(ImDrawList* draw, int start, ImVec2 center, float degrees) {
    const float a = degrees * 0.01745329f, c = std::cos(a), s = std::sin(a);
    for (int i = start; i < draw->VtxBuffer.Size; ++i) {
        ImVec2& p = draw->VtxBuffer[i].pos;
        const float x = p.x - center.x, y = p.y - center.y;
        p = ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
    }
}

// Ben-Day dots where another style would put a gradient: a staggered grid of `pitch`, each dot as large as
// `strength` (0 to 1) says at its centre, all of them inside the box.
template <class Strength>
void halftone(ImDrawList* draw, ImVec2 min, ImVec2 max, float pitch, ImU32 color, Strength strength) {
    const float largest = pitch * 0.34f;
    int row = 0;
    for (float y = min.y + pitch * 0.5f; y < max.y; y += pitch * 0.5f, ++row)
        for (float x = min.x + (row % 2 ? pitch : pitch * 0.5f); x < max.x; x += pitch) {
            const float radius = largest * std::clamp(strength(ImVec2(x, y)), 0.f, 1.f);
            if (radius >= 0.75f && x - radius >= min.x && x + radius <= max.x && y - radius >= min.y &&
                y + radius <= max.y)
                draw->AddCircleFilled(ImVec2(x, y), radius, color, radius < 2.5f ? 8 : 12);
        }
}

// A colour field that leans as the print's swipes do: its sides lean right toward the top.
void slanted(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color) {
    const float lean = (max.y - min.y) * kLean;
    draw->AddQuadFilled(ImVec2(min.x + lean, min.y), ImVec2(max.x + lean, min.y), max, ImVec2(min.x, max.y), color);
}

// A tooltip, wrapped.
void tip(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, vec(kSurfaceHover));
    ImGui::PushStyleColor(ImGuiCol_Border, vec(kFaint));
    ImGui::PushStyleColor(ImGuiCol_Text, vec(kPaper));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y));
    if (ImGui::BeginTooltip()) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0], 14);
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

// The steps the game's Settings offer too (SPIDY VR, vr_settings.hpp).
constexpr const char* kSnapTurns[] = {"Off", "15\xC2\xB0", "30\xC2\xB0", "45\xC2\xB0", "60\xC2\xB0", "90\xC2\xB0"};
constexpr int kSnapValues[] = {0, 15, 30, 45, 60, 90};
constexpr const char* kSmoothTurns[] = {"Off",          "60\xC2\xB0/s",  "90\xC2\xB0/s",
                                        "120\xC2\xB0/s", "180\xC2\xB0/s", "240\xC2\xB0/s"};
constexpr int kSmoothValues[] = {0, 60, 90, 120, 180, 240};
constexpr const char* kHaptics[] = {"Off", "25%", "50%", "75%", "100%"};
constexpr int kHapticValues[] = {0, 25, 50, 75, 100};
// Upscaling: the percent of the eye's size, per side, the scene is rendered at (SessionOptions::upscale).
constexpr const char* kUpscales[] = {"Off (100%)", "High (77%)", "Quality (67%)", "Balanced (58%)",
                                     "Performance (50%)"};
constexpr int kUpscaleValues[] = {100, 77, 67, 58, 50};
// The monitor in VR: how often the game window gets the left eye (SessionOptions::monitorRate).
constexpr const char* kMonitorRates[] = {"Every frame", "60 a second", "30 a second", "15 a second", "5 a second"};
constexpr int kMonitorRateValues[] = {0, 60, 30, 15, 5};
constexpr const char* kScreenSizes[] = {"Small", "Medium", "Large"};
constexpr const char* kHudSizes[] = {"Off", "Small", "Medium", "Large"};
constexpr const char* kScreenDepths[] = {"Off", "Low", "Medium", "High"};
// The button that shoots and holds webs (SessionOptions::triggerWebs: the second).
constexpr const char* kWebButtons[] = {"Grip", "Trigger"};
// How webs behave (SessionOptions::arcadeWebs: the second).
constexpr const char* kWebStyles[] = {"Realistic", "Arcade"};
constexpr const char* kWeights[] = {"40%", "60%", "80%", "100%", "125%", "150%", "200%", "250%", "300%"};
constexpr int kWeightValues[] = {40, 60, 80, 100, 125, 150, 200, 250, 300};
constexpr const char* kFlipSpeeds[] = {"90\xC2\xB0/s",  "120\xC2\xB0/s", "150\xC2\xB0/s", "180\xC2\xB0/s",
                                       "240\xC2\xB0/s", "300\xC2\xB0/s", "360\xC2\xB0/s", "480\xC2\xB0/s"};
constexpr int kFlipSpeedValues[] = {90, 120, 150, 180, 240, 300, 360, 480};

// A choice of the SPIDY VR tab as the launcher writes its own: "VERY SLOW", "20 CM", "90°/S" as "Very slow",
// "20 cm", "90°/s".
std::string launcherCase(std::string_view choice) {
    std::string text(choice);
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z')
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    return text;
}

} // namespace

void theme(ImGuiStyle& style) {
    style.WindowPadding = ImVec2(16, 16);
    style.FramePadding = ImVec2(10, 7);
    style.ItemSpacing = ImVec2(10, 10);
    style.ItemInnerSpacing = ImVec2(8, 6);
    // Printed boxes are square: only stickers (buttons, toggles, stamps) round their corners.
    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.FrameRounding = 0;
    style.PopupRounding = 0;
    style.GrabRounding = 0;
    style.ScrollbarRounding = 0;
    style.ScrollbarSize = 10;
    style.GrabMinSize = 12;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 1;
    style.DisabledAlpha = 0.45f;
    style.FontSizeBase = 15;
    style.SeparatorTextBorderSize = 1;
    auto* c = style.Colors;
    c[ImGuiCol_Text] = vec(kPaper);
    c[ImGuiCol_TextDisabled] = vec(kFaint);
    c[ImGuiCol_WindowBg] = vec(kGround);
    c[ImGuiCol_ChildBg] = vec(kSurface);
    c[ImGuiCol_PopupBg] = vec(kSurfaceHigh);
    c[ImGuiCol_Border] = vec(kLine);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = vec(kSurfaceHigh);
    c[ImGuiCol_FrameBgHovered] = vec(kSurfaceHover);
    c[ImGuiCol_FrameBgActive] = vec(kSurfaceHover);
    c[ImGuiCol_Button] = vec(kSurfaceHigh);
    c[ImGuiCol_ButtonHovered] = vec(kSurfaceHover);
    c[ImGuiCol_ButtonActive] = vec(kViolet);
    c[ImGuiCol_Header] = vec(kViolet);
    c[ImGuiCol_HeaderHovered] = vec(kSurfaceHover);
    c[ImGuiCol_HeaderActive] = vec(kViolet);
    c[ImGuiCol_SliderGrab] = vec(kAccent);
    c[ImGuiCol_SliderGrabActive] = vec(kAccentHover);
    c[ImGuiCol_CheckMark] = vec(kAccent);
    c[ImGuiCol_ScrollbarBg] = vec(kGround, 0);
    c[ImGuiCol_ScrollbarGrab] = vec(kSurfaceHover);
    c[ImGuiCol_ScrollbarGrabHovered] = vec(kLine);
    c[ImGuiCol_ScrollbarGrabActive] = vec(kViolet);
    c[ImGuiCol_Separator] = vec(kLine);
    c[ImGuiCol_TextSelectedBg] = vec(kViolet, 0.85f);
    c[ImGuiCol_ModalWindowDimBg] = vec(kGround, 0.82f);
    c[ImGuiCol_NavCursor] = vec(kAccentHover);
}

App::App(HWND window, Fonts fonts, std::vector<Picture> controls, std::string updatedFrom)
    : window_(window), fonts_(fonts), controlsPictures_(std::move(controls)), settings_(loadSettings()),
      installFolder_(installFolder()), updatedFrom_(std::move(updatedFrom)) {
    scanner_.start(settings_);
    refreshProfiles();
    // A player's copy asks GitHub for a newer Spidy as it starts, unless the player turned that off.
    updater_.start(installFolder_, !installFolder_.empty() && settings_.updateCheck);
}

App::~App() {
    if (session_.running())
        session_.requestStop();
}

void App::rescan() {
    headset_.reset();
    scanner_.start(settings_);
}

void App::poll() {
    const Scan latest = scanner_.snapshot();
    // Remember the game's hash so the next start skips reading 120 MB.
    if (latest.done && !latest.hashValue.empty() && latest.hashValue != settings_.hashValue) {
        settings_.hashPath = latest.hashPath;
        settings_.hashSize = latest.hashSize;
        settings_.hashTime = latest.hashTime;
        settings_.hashValue = latest.hashValue;
        save();
    }
    const bool firstResult = latest.done && !scan_.done;
    scan_ = latest;
    if (firstResult || !runtime())
        chooseDefaultRuntime();
    const size_t before = log_.size();
    logCount_ = session_.lines(logCount_, log_);
    if (log_.size() != before)
        scrollLog_ = true;
    // What the VR settings were left at becomes the next session's options.
    for (size_t i = before; i < log_.size(); ++i)
        if (spidy::launcher::headsetSettings(log_[i].text, settings_.options))
            save();
    const Outcome install = vcInstall_.state();
    if (lastInstall_ == Outcome::running && install != Outcome::running)
        rescan();
    lastInstall_ = install;
    // A session that the game's administrator rights kept out: ask once, as it ends.
    const bool sessionRuns = session_.running();
    if (sessionRan_ && !sessionRuns && session_.exitCode() == spidy::launcher::kNeedsAdministratorExit)
        wantAdministrator_ = true;
    // A running game that already holds Spidy: ask once, as the session ends, whether to close it.
    if (sessionRan_ && !sessionRuns && session_.exitCode() == spidy::launcher::kCloseGameExit)
        wantCloseGame_ = true;
    // The SPIDY VR tab may have changed its own settings in spidy_vr.ini during the session.
    if (sessionRan_ && !sessionRuns)
        profilesStale_ = true;
    sessionRan_ = sessionRuns;
    update_ = updater_.status();
    // The new files are in place: this launcher closes, and main.cpp starts the new one.
    if (update_.state == UpdateState::installed)
        quit_ = true;
    if (closeAfterStop_ && !session_.running())
        quit_ = true;
}

bool App::busy() {
    return session_.running() || scanner_.busy() || headset_.state() == Outcome::running ||
           vcInstall_.state() == Outcome::running || updater_.busy();
}

bool App::allowClose() {
    if (!session_.running())
        return true;
    wantClose_ = true;
    return false;
}

const Runtime* App::runtime() {
    for (const auto& r : scan_.runtimes)
        if (_wcsicmp(r.manifest.c_str(), settings_.runtime.c_str()) == 0)
            return &r;
    return nullptr;
}

std::array<uint32_t, 2> App::eyeSize() {
    const auto eye = headset_.eye().value_or(std::array<uint32_t, 2>{3072, 3264});
    return spidy::scaledEyeSize(eye[0], eye[1], static_cast<uint32_t>(settings_.options.renderScale));
}

std::string App::summary() {
    namespace text = spidy::launcher;
    text::SupportFacts facts;
    facts.version = kVersion;
    // The session this launcher ran last; before it has run one, the last one in the reports folder.
    std::string session;
    for (const auto& line : log_)
        session += line.text + "\n";
    if (session.empty() && !scan_.root.empty())
        session = lastConsoleLog(scan_.root);
    facts.session = text::sessionEnd(session);
    if (scan_.done) {
        const Runtime* chosen = runtime();
        facts.runtime = scan_.runtimes.empty() ? "none installed" : chosen ? chosen->name : "Automatic";
        for (const auto& r : scan_.runtimes)
            facts.runtime += (&r == &scan_.runtimes.front() ? "; installed: " : ", ") + r.name +
                             (r.active ? " (Windows' active)" : "");
        // The game's row, without its path.
        const std::string version = scan_.gameVersion.empty() ? "" : " " + scan_.gameVersion;
        if (scan_.gameExe.empty())
            facts.game = scan_.epicFound ? "only the Epic Games Store version, which Spidy does not work with"
                                         : "not found";
        else if (scan_.build == Build::supported)
            facts.game = scan_.gameSource + ", supported version" + version;
        else if (scan_.build == Build::sameVersion)
            facts.game = scan_.gameSource + ", version" + version + " but not Steam's file";
        else if (scan_.build == Build::unsupported)
            facts.game = scan_.gameSource + ", not supported. " +
                         text::unsupportedGame(scan_.gameVersion, scan_.expectedVersion, scan_.steamApp);
        else if (scan_.build == Build::unreadable)
            facts.game = scan_.gameSource + ", Spider-Man.exe could not be read";
    }
    // The headset as the check found it; unchecked, as the session found it.
    if (headset_.state() != Outcome::idle)
        facts.headset = headset_.summary();
    else if (session.find("Headset available: ") != std::string::npos)
        facts.headset = text::headsetSummary(session, true) + " (in the last session)";
    else
        facts.headset = "not checked";
    facts.pc = hardware();
    const auto& o = settings_.options;
    // A value the list of steps does not have counts as the nearest one.
    size_t upscale = 0;
    for (size_t i = 1; i < std::size(kUpscaleValues); ++i)
        if (std::abs(kUpscaleValues[i] - o.upscale) < std::abs(kUpscaleValues[upscale] - o.upscale))
            upscale = i;
    facts.upscaling = kUpscales[upscale];
    facts.renderResolution = format("%d%%", o.renderScale);
    facts.webButton = kWebButtons[o.triggerWebs ? 1 : 0];
    return text::supportSummary(facts);
}

// A chosen runtime that is no longer installed gives way to Automatic.
void App::chooseDefaultRuntime() {
    if (runtime() || settings_.runtime == kAutoRuntime || scan_.runtimes.empty())
        return;
    settings_.runtime = kAutoRuntime;
}

std::vector<std::string> App::blockers() {
    std::vector<std::string> reasons;
    if (!scan_.done)
        return {"Checking this PC..."};
    if (scan_.root.empty() || !scan_.missing.empty() || scan_.python.empty())
        reasons.push_back("Spidy's files are incomplete.");
    else if (!scan_.writable)
        reasons.push_back("Spidy can't write in its folder.");
    if (scan_.gameExe.empty())
        reasons.push_back("The game was not found.");
    else if (scan_.build == Build::checking)
        reasons.push_back("Checking the game version...");
    else if (scan_.build != Build::supported && scan_.build != Build::sameVersion)
        reasons.push_back("This game version is not supported.");
    if (scan_.runtimes.empty())
        reasons.push_back("No VR runtime is installed.");
    if (!scan_.vcCurrent)
        reasons.push_back("The Visual C++ runtime is needed.");
    return reasons;
}

void App::start(bool closeGame) {
    startError_.clear();
    administratorError_.clear();
    const auto report = newReportPath(scan_.root);
    const auto* chosen = runtime();
    const auto args = spidy::launcher::sessionArguments(settings_.options, report, chosen ? chosen->manifest : L"",
                                                        newStopEventName(), closeGame);
    log_.clear();
    logCount_ = 0;
    if (!session_.start(scan_.python, scan_.root, args, report, startError_))
        return;
    save();
}

// Windows asks the player first; this launcher closes once the new one has started.
void App::restartElevated() {
    if (restartAsAdministrator(administratorError_))
        quit_ = true;
}

void App::copyLog() {
    std::string all;
    for (const auto& line : log_)
        all += line.text + "\n";
    ImGui::SetClipboardText(all.empty() ? startError_.c_str() : all.c_str());
}

// --- Widgets -----------------------------------------------------------------

bool App::beginCard(const char* id, ImVec2 size, int childFlags) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vec(kSurface));
    ImGui::PushStyleColor(ImGuiCol_Border, vec(kLine));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(16)));
    const bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | childFlags, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    if (open) {
        // Dots rising into its top right corner.
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetWindowPos(), max = add(min, ImGui::GetWindowSize());
        const float reach = S(170);
        draw->PushClipRect(min, max);
        halftone(draw, ImVec2(max.x - reach, min.y + S(1)), ImVec2(max.x - S(1), min.y + reach), S(8), col(kDots),
                 [&](ImVec2 p) { return 0.85f - std::hypot(max.x - p.x, p.y - min.y) / reach; });
        draw->PopClipRect();
    }
    firstOption_ = true;
    return open;
}

void App::endCard() { ImGui::EndChild(); }

// A card's name as the print's kicker: small spaced capitals after a red slash.
void App::cardTitle(const char* title, float rightWidth) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight(), size = S(14);
    const float y = at.y + (height - size) * 0.5f;
    slanted(draw, ImVec2(at.x, y + size * 0.18f), ImVec2(at.x + S(4), y + size * 0.86f), col(kRed));
    spacedText(draw, fonts_.kicker, size, ImVec2(at.x + S(14), y), col(kPaperDim), upper(title).c_str(), 0.14f);
    // The caller may put buttons on the right of the title line.
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - rightWidth, height));
    if (rightWidth > 0)
        ImGui::SameLine(0, 0);
}

// A dark disc with the mark in its colour; only a fault fills it, to be seen first.
void App::statusMark(ImDrawList* draw, ImVec2 c, Mark mark) {
    const float r = S(11), w = S(2.2f);
    switch (mark) {
    case Mark::ok:
        draw->AddCircleFilled(c, r, col(kSurfaceHover), 24);
        draw->PathLineTo(ImVec2(c.x - r * 0.42f, c.y + r * 0.02f));
        draw->PathLineTo(ImVec2(c.x - r * 0.1f, c.y + r * 0.34f));
        draw->PathLineTo(ImVec2(c.x + r * 0.46f, c.y - r * 0.32f));
        draw->PathStroke(col(kCyan), 0, w);
        break;
    case Mark::error:
        draw->AddCircleFilled(c, r, col(kRed), 24);
        draw->AddLine(ImVec2(c.x - r * 0.36f, c.y - r * 0.36f), ImVec2(c.x + r * 0.36f, c.y + r * 0.36f), col(kWhite), w);
        draw->AddLine(ImVec2(c.x + r * 0.36f, c.y - r * 0.36f), ImVec2(c.x - r * 0.36f, c.y + r * 0.36f), col(kWhite), w);
        break;
    case Mark::warning:
        draw->AddCircleFilled(c, r, col(kSurfaceHover), 24);
        draw->AddLine(ImVec2(c.x, c.y - r * 0.5f), ImVec2(c.x, c.y + r * 0.12f), col(kYellow), w);
        draw->AddCircleFilled(ImVec2(c.x, c.y + r * 0.44f), S(1.5f), col(kYellow));
        break;
    case Mark::info:
        draw->AddCircleFilled(c, r, col(kSurfaceHover), 24);
        draw->AddCircleFilled(ImVec2(c.x, c.y - r * 0.42f), S(1.5f), col(kPaper));
        draw->AddLine(ImVec2(c.x, c.y - r * 0.08f), ImVec2(c.x, c.y + r * 0.48f), col(kPaper), w);
        break;
    case Mark::busy: {
        // Held poses, twelve a second, as the print moves: the arc steps round.
        const float a = std::floor(static_cast<float>(ImGui::GetTime()) * 12) * 0.5235988f;
        draw->AddCircle(c, r * 0.72f, col(kLine), 24, w);
        draw->PathArcTo(c, r * 0.72f, a, a + 2.1f, 16);
        draw->PathStroke(col(kCyan), 0, w);
        break;
    }
    }
}

bool App::button(const char* label, float width, Ink ink, const char* icon) {
    const ImVec2 size(width, ImGui::GetFrameHeight());
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, size);
    handCursor();
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    const bool primary = ink == Ink::primary, support = ink == Ink::support;
    auto* draw = ImGui::GetWindowDrawList();
    // It dips while pressed.
    const ImVec2 sink = held ? ImVec2(0, S(1)) : ImVec2(0, 0);
    const ImVec2 min = add(at, sink), max = add(add(at, size), sink);
    const float rounding = S(4);
    draw->AddRectFilled(min, max, primary ? col(hovered ? kRedLift : kRed) : col(hovered ? kSurfaceHover : kSurfaceHigh),
                        rounding);
    if (!primary)
        draw->AddRect(min, max, support ? col(kYellow) : col(hovered ? kFaint : kLine), rounding, 0,
                      S(support ? 1.5f : 1));
    const char* end = std::strstr(label, "##");
    const std::string text = upper(end ? std::string_view(label, end - label) : std::string_view(label));
    const float fontSize = S(15), spacing = 0.05f;
    const float textWidth = spacedWidth(fonts_.label, fontSize, text.c_str(), spacing);
    const float iconWidth = icon && fonts_.icons ? measure(fonts_.label, fontSize, icon).x + S(8) : 0;
    float x = min.x + std::max(S(8), (size.x - textWidth - iconWidth) * 0.5f);
    const float y = min.y + (size.y - fontSize) * 0.5f;
    const ImU32 color = col(primary ? kWhite : support ? kYellow : kPaper);
    if (iconWidth > 0) {
        draw->AddText(fonts_.label, fontSize, ImVec2(x, y - S(1)), color, icon);
        x += iconWidth;
    }
    spacedText(draw, fonts_.label, fontSize, ImVec2(x, y), color, text.c_str(), spacing);
    return clicked;
}

bool App::sticker(const char* id, const char* icon, const char* label, const char* line, ImVec2 size,
                  const Paint& paint, bool enabled) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float drop = paint.drop ? std::round(std::max(S(4), size.y * 0.07f)) : 0;
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(size.x + drop, size.y + drop));
    ImGui::EndDisabled();
    if (enabled)
        handCursor();
    const bool hovered = enabled && ImGui::IsItemHovered(), held = enabled && ImGui::IsItemActive();
    auto* draw = ImGui::GetWindowDrawList();
    // Pressed, it sinks onto its drop, or dips.
    const float shift = held ? std::max(drop - S(1), S(1)) : 0;
    const ImVec2 min(at.x + shift, at.y + shift), max(at.x + size.x + shift, at.y + size.y + shift);
    const float rounding = S(6);
    if (drop > 0)
        draw->AddRectFilled(ImVec2(at.x + drop, at.y + drop), ImVec2(at.x + size.x + drop, at.y + size.y + drop),
                            enabled ? paint.drop : col(kLine), rounding);
    draw->AddRectFilled(min, max, !enabled ? col(kSurfaceHigh) : hovered ? paint.hover : paint.fill, rounding);
    draw->AddRect(min, max, enabled ? paint.keyline : col(kLine), rounding, 0, S(1.5f));
    const ImU32 text = enabled ? paint.lettering : col(kFaint);
    const std::string words = upper(label);
    const float labelSize = line ? size.y * 0.46f : size.y * 0.5f, lineSize = size.y * 0.24f;
    const float iconSize = labelSize * 0.62f;
    const float iconWidth = icon && fonts_.icons ? measure(fonts_.label, iconSize, icon).x + labelSize * 0.32f : 0;
    const float wordsWidth = measure(fonts_.display, labelSize, words.c_str()).x;
    // The display face's capitals stand in the middle of its line, a little low: lift them.
    const float blockHeight = labelSize * 0.78f + (line ? lineSize * 0.85f : 0);
    const float y = min.y + (size.y - blockHeight) * 0.5f - labelSize * 0.16f;
    float x = min.x + (size.x - wordsWidth - iconWidth) * 0.5f;
    if (iconWidth > 0) {
        draw->AddText(fonts_.label, iconSize, ImVec2(x, y + (labelSize - iconSize) * 0.5f), text, icon);
        x += iconWidth;
    }
    draw->AddText(fonts_.display, labelSize, ImVec2(x, y), text, words.c_str());
    if (line) {
        const std::string small = upper(line);
        const float lineWidth = spacedWidth(fonts_.kicker, lineSize, small.c_str(), 0.12f);
        spacedText(draw, fonts_.kicker, lineSize, ImVec2(min.x + (size.x - lineWidth) * 0.5f, y + labelSize * 0.84f),
                   text, small.c_str(), 0.12f);
    }
    return clicked;
}

bool App::toggle(const char* id, bool* value) {
    const float h = S(22), w = S(42);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    if (clicked)
        *value = !*value;
    handCursor();
    const bool hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + w, at.y + h);
    draw->AddRectFilled(at, end,
                        *value ? col(hovered ? kAccentHover : kAccent) : col(hovered ? kSurfaceHover : kSurfaceHigh),
                        h * 0.5f);
    if (!*value)
        draw->AddRect(at, end, col(hovered ? kFaint : kLine), h * 0.5f, 0, S(1));
    const float knob = *value ? end.x - h * 0.5f : at.x + h * 0.5f;
    draw->AddCircleFilled(ImVec2(knob, at.y + h * 0.5f), h * 0.5f - S(4), *value ? col(kPaper) : col(kPaperDim), 20);
    return clicked;
}

bool App::slider(const char* id, int* value, int lowest, int highest, const char* format) {
    // ImGui's own slider takes the mouse and the keyboard, unseen.
    const ImVec4 clear(0, 0, 0, 0);
    for (const ImGuiCol part : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_Border,
                                ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Text})
        ImGui::PushStyleColor(part, clear);
    const bool changed = ImGui::SliderInt(id, value, lowest, highest, format, ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopStyleColor(7);
    handCursor();
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const float share = std::clamp(static_cast<float>(*value - lowest) / static_cast<float>(highest - lowest), 0.f, 1.f);
    const float fill = min.x + std::max(S(6), (max.x - min.x) * share);
    draw->AddRectFilled(min, max, col(hovered || active ? kSurfaceHover : kSurfaceHigh));
    draw->AddRectFilled(min, ImVec2(fill, max.y), col(hovered || active ? kAccentHover : kAccent));
    draw->AddRect(min, max, col(hovered || active ? kFaint : kLine), 0, 0, S(1));
    char text[32];
    std::snprintf(text, sizeof(text), format, *value);
    const float size = ImGui::GetFontSize();
    const ImVec2 extent = measure(ImGui::GetFont(), size, text);
    draw->AddText(ImGui::GetFont(), size,
                  ImVec2(min.x + (max.x - min.x - extent.x) * 0.5f, min.y + (max.y - min.y - extent.y) * 0.5f),
                  col(kPaper), text);
    return changed;
}

bool App::link(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, vec(kCyan));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    const bool clicked = ImGui::IsItemClicked();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, col(kCyan), 1);
    }
    return clicked;
}

float App::captionBox(ImDrawList* draw, ImVec2 at, float width, const std::string& text, Caption paper,
                      const std::string& emphasis) {
    // The narrator's capitals beside a bar in the caption's colour: the print's caption box without its paper.
    const Rgb bar = paper == Caption::alert ? kRed : paper == Caption::fact ? kPaperDim : kYellow;
    const ImU32 accent = col(paper == Caption::alert ? kRedLift : kYellow);
    const float size = S(16.5f), indent = S(18);
    const float wrap = width - indent;
    const std::string words = upper(text), loud = upper(emphasis);
    const float wordsHeight = words.empty() ? 0 : measure(fonts_.narration, size, words.c_str(), wrap).y;
    const float loudHeight = loud.empty() ? 0 : measure(fonts_.narration, size, loud.c_str(), wrap).y;
    const float height = wordsHeight + loudHeight + S(4);
    if (!draw)
        return height;
    draw->AddRectFilled(ImVec2(at.x, at.y + S(1)), ImVec2(at.x + S(4), at.y + height - S(1)), col(bar));
    draw->AddText(fonts_.narration, size, ImVec2(at.x + indent, at.y + S(2)), col(kPaper), words.c_str(), nullptr, wrap);
    if (!loud.empty())
        draw->AddText(fonts_.narration, size, ImVec2(at.x + indent, at.y + S(2) + wordsHeight), accent, loud.c_str(),
                      nullptr, wrap);
    return height;
}

float App::stamp(ImDrawList* draw, ImVec2 at, const char* text, ImU32 fill, ImU32 lettering, ImU32 keyline,
                 float size, float degrees, ImU32 dot) {
    const std::string words = upper(text);
    const float spacing = 0.06f, dotWidth = dot ? size * 0.72f : 0;
    const ImVec2 pad(size * 0.6f, size * 0.24f);
    const float width = spacedWidth(fonts_.label, size, words.c_str(), spacing) + dotWidth + pad.x * 2;
    const ImVec2 min(at.x, at.y - size * 0.5f - pad.y), max(at.x + width, at.y + size * 0.5f + pad.y);
    const float rounding = S(4);
    const int start = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, max, fill, rounding);
    draw->AddRect(min, max, keyline, rounding, 0, S(1.5f));
    // On the beat of Spidy's videos (120 a minute): on for a beat, off for the next.
    if (dot && std::fmod(ImGui::GetTime(), 1.0) < 0.5)
        draw->AddCircleFilled(ImVec2(min.x + pad.x + size * 0.2f, at.y), size * 0.2f, dot, 12);
    spacedText(draw, fonts_.label, size, ImVec2(min.x + pad.x + dotWidth, at.y - size * 0.54f), lettering,
               words.c_str(), spacing);
    tilt(draw, start, ImVec2((min.x + max.x) * 0.5f, at.y), degrees);
    return width;
}

float App::wordmark(ImDrawList* draw, ImVec2 at, float size) {
    misprint(draw, fonts_.label, size, at, col(kPaper), "SPIDY", 0.1f);
    return spacedWidth(fonts_.label, size, "SPIDY", 0.1f);
}

// Spidy's mark: a red disc with a white web (the window icon is the same picture).
void App::badge(ImDrawList* draw, ImVec2 c, float r) {
    draw->AddCircleFilled(c, r, col(kRed), 48);
    const ImU32 line = col(kWhite);
    const float thickness = std::max(1.f, r * 0.055f);
    constexpr int spokes = 8;
    for (int i = 0; i < spokes; ++i) {
        const float a = i * 6.2831853f / spokes + 0.39f;
        draw->AddLine(c, ImVec2(c.x + std::cos(a) * r * 0.8f, c.y + std::sin(a) * r * 0.8f), line, thickness);
    }
    for (float ring : {0.3f, 0.52f, 0.74f})
        for (int i = 0; i < spokes; ++i) {
            const float a0 = i * 6.2831853f / spokes + 0.39f, a1 = (i + 1) * 6.2831853f / spokes + 0.39f;
            const ImVec2 p0(c.x + std::cos(a0) * r * ring, c.y + std::sin(a0) * r * ring);
            const ImVec2 p1(c.x + std::cos(a1) * r * ring, c.y + std::sin(a1) * r * ring);
            // Each thread sags a little toward the centre, as a real web does.
            const float mid = (a0 + a1) * 0.5f, sag = ring * 0.86f;
            draw->AddBezierQuadratic(p0, ImVec2(c.x + std::cos(mid) * r * sag, c.y + std::sin(mid) * r * sag), p1, line,
                                     thickness, 8);
        }
}

// --- Layout ----------------------------------------------------------------------

void App::frame(float scale) {
    scale_ = scale;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##spidy", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    header();
    tabs();
    const float top = kHeaderHeight + kTabsHeight + 18;
    const ImVec2 origin(viewport->Pos.x + S(22), viewport->Pos.y + S(top));
    const ImVec2 size(viewport->Size.x - S(44), viewport->Size.y - S(top + 22));
    if (size.x > 0 && size.y > 0) {
        switch (tab_) {
        case Tab::play: play(origin, size); break;
        case Tab::options: options(origin, size); break;
        case Tab::controls: controls(origin, size); break;
        case Tab::about: about(origin, size); break;
        }
    }
    modals();
    ImGui::End();
}

void App::header() {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth(), height = S(kHeaderHeight);
    // Faint dots rising into the top right corner, as on the cards.
    const ImVec2 corner(at.x + width, at.y);
    const float reach = S(300);
    halftone(draw, ImVec2(corner.x - reach * 1.8f, at.y), ImVec2(corner.x, at.y + height), S(9), col(kDotsStrong),
             [&](ImVec2 p) { return 0.9f - std::hypot((corner.x - p.x) / 1.8f, p.y - corner.y) / reach; });
    // Spidy's disc and its name in the misprint.
    const ImVec2 center(at.x + S(24 + 27), at.y + height * 0.5f);
    badge(draw, center, S(27));
    const float x = center.x + S(27 + 18);
    const float nameSize = S(46);
    const float nameWidth = wordmark(draw, ImVec2(x, at.y + S(9)), nameSize);
    // The release as an issue, and its price (the corner box of Spidy's videos).
    {
        const std::string number = format("NO. %s", kVersion);
        const float size = S(15.5f), spacing = 0.05f;
        const ImVec2 pad(S(9), S(4));
        const float numberWidth = spacedWidth(fonts_.label, size, number.c_str(), spacing) + pad.x * 2;
        const float freeWidth = spacedWidth(fonts_.label, size, "FREE", spacing) + pad.x * 2;
        const ImVec2 min(x + nameWidth + S(18), at.y + S(25));
        const ImVec2 max(min.x + numberWidth + freeWidth, min.y + size + pad.y * 2);
        const int start = draw->VtxBuffer.Size;
        draw->AddRectFilled(min, ImVec2(min.x + numberWidth, max.y), col(kPaper));
        draw->AddRectFilled(ImVec2(min.x + numberWidth, min.y), max, col(kYellow));
        draw->AddLine(ImVec2(min.x + numberWidth, min.y), ImVec2(min.x + numberWidth, max.y), col(kInk), S(1.5f));
        draw->AddRect(min, max, col(kInk), 0, 0, S(2));
        spacedText(draw, fonts_.label, size, ImVec2(min.x + pad.x, min.y + pad.y - S(1)), col(kInk), number.c_str(),
                   spacing);
        spacedText(draw, fonts_.label, size, ImVec2(min.x + numberWidth + pad.x, min.y + pad.y - S(1)), col(kInk),
                   "FREE", spacing);
        tilt(draw, start, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), -2);
    }
    const float kicker = S(13);
    const char* what = "VR MOD FOR MARVEL'S SPIDER-MAN REMASTERED";
    const ImVec2 line(x + S(2), at.y + S(61));
    spacedText(draw, fonts_.kicker, kicker, line, col(kPaperDim), what, 0.1f);
    const std::string by = "  \xC2\xB7  BY " + upper(kAuthor);
    spacedText(draw, fonts_.kicker, kicker, ImVec2(line.x + spacedWidth(fonts_.kicker, kicker, what, 0.1f), line.y),
               col(kPaper), by.c_str(), 0.1f);

    // Where players meet, quietly, and the tip jar in yellow: the README's buttons, as wide as their words.
    const float buttonHeight = S(50);
    const auto stickerWidth = [&](const char* label, const char* icon) {
        const float labelSize = buttonHeight * 0.46f, iconSize = labelSize * 0.62f;
        const float iconWidth = icon && fonts_.icons ? measure(fonts_.label, iconSize, icon).x + labelSize * 0.32f : 0;
        return std::max(S(178), measure(fonts_.display, labelSize, upper(label).c_str()).x + iconWidth + S(36));
    };
    const float kofiWidth = stickerWidth("Buy me a coffee", kHeart);
    float buttonX = at.x + width - S(26) - kofiWidth;
    const float buttonY = at.y + (height - buttonHeight) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(buttonX, buttonY));
    if (sticker("##kofi", kHeart, "Buy me a coffee", "on Ko-fi", ImVec2(kofiWidth, buttonHeight),
                {col(kSurfaceHigh), col(kSurfaceHover), col(kYellow), col(kYellow), 0}))
        openUrl(kKofiUrl);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        tip("Spidy is free; the AI tokens it is made with are not. A coffee on Ko-fi pays for its updates and the "
            "next full VR conversion.");
    const float discordWidth = stickerWidth("Join the Discord", nullptr);
    buttonX -= discordWidth + S(14);
    ImGui::SetCursorScreenPos(ImVec2(buttonX, buttonY));
    if (sticker("##discord", nullptr, "Join the Discord", "News \xC2\xB7 help \xC2\xB7 clips",
                ImVec2(discordWidth, buttonHeight), {col(kSurfaceHigh), col(kSurfaceHover), col(kPaper), col(kLine), 0}))
        openUrl(kDiscordUrl);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        tip("Where the players are: release news first, help when something goes wrong, and clips.");
}

void App::tabs() {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 window = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth(), y = window.y + S(kHeaderHeight), height = S(kTabsHeight);
    draw->AddLine(ImVec2(window.x, y + height - 1), ImVec2(window.x + width, y + height - 1), col(kLine), 1);
    float x = window.x + S(12);
    const std::pair<Tab, const char*> items[] = {
        {Tab::play, "Play"}, {Tab::options, "Options"}, {Tab::controls, "Controls"}, {Tab::about, "About"}};
    const float size = S(19), spacing = 0.06f;
    for (const auto& [tab, label] : items) {
        const std::string text = upper(label);
        const float itemWidth = spacedWidth(fonts_.label, size, text.c_str(), spacing) + S(40);
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (ImGui::InvisibleButton(label, ImVec2(itemWidth, height)))
            tab_ = tab;
        handCursor();
        const bool active = tab_ == tab, hovered = ImGui::IsItemHovered();
        // The tab you are on is underlined with a red swipe.
        if (active)
            slanted(draw, ImVec2(x + S(16), y + height - S(10)), ImVec2(x + itemWidth - S(20), y + height - S(6)),
                    col(kRed));
        spacedText(draw, fonts_.label, size, ImVec2(x + S(18), y + (height - size) * 0.5f - S(3)),
                   col(active || hovered ? kPaper : kPaperDim), text.c_str(), spacing);
        x += itemWidth;
    }
    // Right: VR running, as a stamp whose dot blinks on the beat.
    if (session_.running()) {
        const bool stopping = session_.stopRequested();
        const char* label = stopping ? "Stopping VR" : "VR running";
        const float stampSize = S(15);
        const float stampWidth =
            spacedWidth(fonts_.label, stampSize, upper(label).c_str(), 0.06f) + stampSize * (0.72f + 1.2f);
        const ImU32 tint = col(stopping ? kYellow : kRedLift);
        stamp(draw, ImVec2(window.x + width - S(28) - stampWidth, y + height * 0.5f - S(1)), label, col(kSurface),
              tint, tint, stampSize, 0, tint);
    }
}

void App::play(ImVec2 origin, ImVec2 size) {
    float top = origin.y;
    ImGui::SetCursorScreenPos(origin);
    if (const float height = updateBanner(size.x); height > 0) {
        top += height + S(18);
    } else if (!settings_.shortcutsAsked && scan_.done) {
        ImGui::SetCursorScreenPos(origin);
        shortcutBanner(size.x);
        top += S(58) + S(18);
    }
    const float gap = S(20);
    const float panelWidth = std::clamp(size.x * 0.4f, S(380), S(470));
    const float rightWidth = size.x - panelWidth - gap;
    const float bottom = origin.y + size.y;
    // Left: VR itself.
    ImGui::SetCursorScreenPos(ImVec2(origin.x, top));
    startPanel(ImVec2(panelWidth, bottom - top));
    // Right: this PC, then the session's messages.
    const float rightX = origin.x + panelWidth + gap;
    ImGui::SetCursorScreenPos(ImVec2(rightX, top));
    ImGui::BeginGroup();
    setupCard(rightWidth);
    ImGui::EndGroup();
    const float logTop = ImGui::GetItemRectMax().y + S(16);
    if (bottom - logTop > S(90)) {
        ImGui::SetCursorScreenPos(ImVec2(rightX, logTop));
        logCard(ImVec2(rightWidth, bottom - logTop));
    }
}

void App::startPanel(ImVec2 size) {
    const bool running = session_.running();
    const auto reasons = blockers();
    const auto seen = [&](const char* text) {
        return std::any_of(log_.begin(), log_.end(),
                           [&](const LogLine& l) { return l.text.find(text) != std::string::npos; });
    };
    // How far the session got, on show after it ended too: 1 the headset, 2 the game, 3 in VR.
    const int reached = seen("Rendering ") || seen("Render memory:") ? 3
                        : seen("Starting Spider-Man") || seen("Using the running game") || seen("Closing Spider-Man")
                            ? 2
                            : 1;
    enum class Step { pending, active, done, failed };
    Step steps[3] = {Step::pending, Step::pending, Step::pending};
    const auto progress = [&](Step state) {
        for (int i = 0; i < 3; ++i)
            steps[i] = i + 1 < reached ? Step::done : i + 1 == reached ? state : Step::pending;
    };
    // What is happening: a headline, the narrator's line (and a second, yellow one), and the details.
    std::string headline, narration, aside, detail;
    Rgb tint = kPaper;
    Caption paper = Caption::narrator;
    bool offerAdministrator = false, offerCloseGame = false, offerHelp = false, offerCoffee = false;
    if (running) {
        progress(Step::active);
        const auto elapsed = static_cast<int>(
            std::chrono::duration<double>(std::chrono::steady_clock::now() - session_.started()).count());
        if (session_.stopRequested()) {
            headline = "Stopping VR";
            narration = "Spidy puts the game back. And saves the session report.";
            tint = kYellow;
        } else if (reached == 3) {
            headline = format("In VR  \xC2\xB7  %d:%02d", elapsed / 60, elapsed % 60);
            narration = "Keep the game window in front. Behind other windows the game pauses.";
            tint = kCyan;
        } else if (seen("Starting Spider-Man") || seen("Using the running game")) {
            headline = seen("Using the running game") ? "Attaching to the game" : "Starting the game";
            narration = "Its menus show on a screen in the headset. Pick your save with the controllers.";
        } else if (seen("Closing Spider-Man")) {
            headline = "Closing the game";
            narration = "The game saves as it quits. Then Spidy starts it again.";
        } else {
            headline = "Starting VR";
            narration = "Put the headset on. Keep it connected.";
        }
    } else if (auto code = session_.exitCode()) {
        std::string error;
        for (auto it = log_.rbegin(); it != log_.rend() && error.empty(); ++it)
            if (it->kind == LineKind::error)
                error = it->text;
        if (error.rfind("Spidy VR unavailable: ", 0) == 0)
            error = error.substr(22);
        if (*code == 0) {
            progress(Step::done);
            headline = "Session ended";
            narration = "Its report is in the reports folder.";
            // After a session that got into VR, the tip jar.
            if (reached == 3) {
                aside = "Had fun? Buy me a coffee: it pays for the next update.";
                offerCoffee = true;
            }
        } else if (*code == 130) {
            progress(Step::pending);
            headline = "VR start cancelled";
            narration = "Press START VR when you are ready.";
            tint = kPaperDim;
        } else if (*code == spidy::launcher::kNeedsAdministratorExit) {
            progress(Step::failed);
            headline = "Needs administrator rights";
            narration = "Steam or the game runs as administrator. So Spidy has to as well.";
            paper = Caption::alert;
            detail = administratorError_;
            offerAdministrator = true;
        } else if (*code == spidy::launcher::kCloseGameExit) {
            progress(Step::failed);
            headline = "Close the game first";
            narration = "Spidy can close the game for you, then start it and VR again.";
            detail = error;
            offerCloseGame = true;
        } else {
            progress(Step::failed);
            headline = *code == 2 ? "VR could not start" : format("VR stopped (code %lu)", *code);
            narration = "The log says why. Copy it and ask on the Discord.";
            paper = Caption::alert;
            detail = error;
            offerHelp = true;
        }
    } else if (!startError_.empty()) {
        headline = "VR could not start";
        narration = "Copy what went wrong and ask on the Discord.";
        paper = Caption::alert;
        detail = startError_;
        offerHelp = true;
    } else if (!reasons.empty()) {
        // Checks still running are no fault: they only make VR wait.
        const bool waiting = std::all_of(reasons.begin(), reasons.end(),
                                         [](const std::string& r) { return r.rfind("Checking", 0) == 0; });
        headline = waiting ? "Checking this PC" : "Not ready yet";
        narration = waiting ? "Spidy looks for the game, your VR runtime and its own files."
                            : "Fix what This PC marks in red. Then press START VR.";
        if (!waiting)
            for (const auto& r : reasons)
                detail += (detail.empty() ? "" : " ") + r;
        tint = waiting ? kPaper : kYellow;
    } else {
        headline = "Ready";
        narration = "Connect your headset. Then press START VR.";
    }

    // A card as the others are, a little lighter.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vec(kSurface));
    ImGui::PushStyleColor(ImGuiCol_Border, vec(kLine));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(20)));
    const bool open = ImGui::BeginChild("##start", size, ImGuiChildFlags_Borders,
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    if (!open) {
        ImGui::EndChild();
        return;
    }
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos(), max = add(min, ImGui::GetWindowSize());
    const float padX = S(22), padY = S(20);
    // Words and buttons in front; the dots go behind them once the steps' place is known.
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    const float left = min.x + padX, innerWidth = max.x - min.x - padX * 2;
    float y = min.y + padY - S(6);
    // The headline, as large as fits.
    const std::string head = upper(headline);
    float headSize = S(56);
    while (headSize > S(26) && measure(fonts_.display, headSize, head.c_str()).x > innerWidth)
        headSize -= S(1);
    // Misprinted in white; a coloured headline is plain, as the misprint's ghosts shimmer on colour.
    if (tint.r == kPaper.r && tint.g == kPaper.g && tint.b == kPaper.b)
        misprint(draw, fonts_.display, headSize, ImVec2(left, y), col(tint), head.c_str());
    else
        draw->AddText(fonts_.display, headSize, ImVec2(left, y), col(tint), head.c_str());
    y += headSize * 0.92f + S(8);
    y += captionBox(draw, ImVec2(left, y), innerWidth - S(8), narration, paper, aside) + S(18);
    // The same for a game that already holds Spidy, after "Not now" (while nothing else keeps VR from
    // starting); and a stuck stop: after 20 s, the offer to end the session's process.
    offerCloseGame = offerCloseGame && reasons.empty();
    const bool offerForceStop = running && session_.stopRequested() &&
                                std::chrono::steady_clock::now() - session_.stopRequestedAt() > std::chrono::seconds(20);
    const bool offers = offerAdministrator || offerCloseGame || offerHelp || offerForceStop || offerCoffee;

    // At the bottom: which options VR starts with, and START VR over it. Between it and the caption: the details,
    // what can be done about it, then the session's three steps.
    const float lineHeight = S(20);
    const float lineY = max.y - padY - lineHeight;
    const float buttonHeight = S(70);
    const float buttonY = lineY - S(16) - buttonHeight;
    struct StepWords {
        const char* title;
        const char* text;
    };
    static constexpr StepWords words[] = {
        {"Headset", "Put it on and connect it to this PC. Quest 3: open Virtual Desktop and connect."},
        {"Game", "Spidy starts it. Its intro and menus show on a screen in the headset."},
        {"Play", "Pick your save with the controllers. VR takes over as soon as you play."},
    };
    const float badgeSize = S(26), titleSize = S(17), textSize = S(14);
    const float textX = left + badgeSize + S(14), textWidth = innerWidth - badgeSize - S(14);
    float heights[3], full = S(14) * 2;
    for (int i = 0; i < 3; ++i) {
        heights[i] = std::max(badgeSize, titleSize + S(2) + measure(fonts_.body, textSize, words[i].text, textWidth).y);
        full += heights[i];
    }
    // The details in as many whole lines as there is room for (up to five before the steps give way): first the
    // steps' words go, then the steps, as what went wrong may show nowhere else in a short window.
    const float offersHeight = offers ? ImGui::GetFrameHeight() + S(18) : 0;
    const float room = buttonY - S(24) - y - offersHeight, compact = badgeSize * 3 + S(10) * 2;
    const float detailSize = S(14.5f);
    const float detailFull = detail.empty() ? 0 : measure(fonts_.body, detailSize, detail.c_str(), innerWidth).y + S(16);
    const float detailNeed = std::min(detailFull, detailSize * 5 + S(16));
    const bool spoken = full + detailNeed <= room, shown = spoken || compact + detailNeed <= room;
    const float stepsHeight = spoken ? full : shown ? compact : 0;
    const float lines = std::floor((std::min(detailFull, room - stepsHeight) - S(16)) / detailSize);
    if (lines > 0) {
        draw->PushClipRect(ImVec2(left, y), ImVec2(left + innerWidth, y + lines * detailSize), true);
        draw->AddText(fonts_.body, detailSize, ImVec2(left, y), col(kPaper), detail.c_str(), nullptr, innerWidth);
        draw->PopClipRect();
    }
    if (lines > 0)
        y += lines * detailSize + S(16);
    // What can be done about it.
    if (offers) {
        ImGui::SetCursorScreenPos(ImVec2(left, y));
        bool first = true;
        const auto offer = [&](const char* label, Ink ink, const char* icon) {
            if (!first)
                ImGui::SameLine(0, S(10));
            first = false;
            const float iconWidth = icon && fonts_.icons ? measure(fonts_.label, S(15), icon).x + S(8) : 0;
            return button(label, spacedWidth(fonts_.label, S(15), upper(label).c_str(), 0.05f) + iconWidth + S(30),
                          ink, icon);
        };
        if (offerAdministrator) {
            if (offer("Restart as administrator", Ink::primary, nullptr))
                restartElevated();
            if (ImGui::IsItemHovered())
                tip("Windows asks you to allow it. Spidy then opens again; close the game and press START VR.");
        }
        if (offerCloseGame) {
            if (offer("Close the game and start VR", Ink::primary, nullptr))
                start(true);
            if (ImGui::IsItemHovered())
                tip("Spidy closes the game as its close button does (it saves as it quits), then starts it and VR "
                    "again.");
        }
        if (offerHelp) {
            if (offer("Copy the log", Ink::plain, nullptr))
                copyLog();
            if (offer("Ask on the Discord", Ink::plain, nullptr))
                openUrl(kDiscordUrl);
        }
        if (offerForceStop) {
            if (offer("Force stop", Ink::plain, nullptr))
                session_.forceStop();
            if (ImGui::IsItemHovered())
                tip("Ends the launcher's session at once. Restart the game before the next VR session.");
        }
        if (offerCoffee && offer("Buy me a coffee", Ink::support, kHeart))
            openUrl(kKofiUrl);
        y += offersHeight;
    }
    float stepY = y;
    for (int i = 0; i < (shown ? 3 : 0); ++i) {
        const Step state = steps[i];
        const float height = spoken ? heights[i] : badgeSize, gap = spoken ? S(14) : S(10);
        const ImVec2 b(left, stepY), bEnd(left + badgeSize, stepY + badgeSize);
        const ImVec2 c((b.x + bEnd.x) * 0.5f, (b.y + bEnd.y) * 0.5f);
        // The thread down to the next step.
        if (i < 2)
            draw->AddLine(ImVec2(c.x, bEnd.y), ImVec2(c.x, stepY + height + gap),
                          col(state == Step::done ? kCyan : kLine), S(2));
        // A square in the step's colour: drawn round what is to come, filled only for a fault.
        const Rgb edge = state == Step::done     ? kCyan
                         : state == Step::active ? kYellow
                         : state == Step::failed ? kRed
                                                 : kLine;
        draw->AddRectFilled(b, bEnd, col(state == Step::failed ? kRed : kSurfaceHigh));
        draw->AddRect(b, bEnd, col(edge), 0, 0, S(2));
        const float r = badgeSize * 0.5f, w = S(2.4f);
        if (state == Step::done) {
            draw->PathLineTo(ImVec2(c.x - r * 0.42f, c.y + r * 0.02f));
            draw->PathLineTo(ImVec2(c.x - r * 0.1f, c.y + r * 0.34f));
            draw->PathLineTo(ImVec2(c.x + r * 0.46f, c.y - r * 0.32f));
            draw->PathStroke(col(kCyan), 0, w);
        } else if (state == Step::failed) {
            draw->AddLine(ImVec2(c.x - r * 0.34f, c.y - r * 0.34f), ImVec2(c.x + r * 0.34f, c.y + r * 0.34f),
                          col(kWhite), w);
            draw->AddLine(ImVec2(c.x + r * 0.34f, c.y - r * 0.34f), ImVec2(c.x - r * 0.34f, c.y + r * 0.34f),
                          col(kWhite), w);
        } else {
            const char number[2] = {static_cast<char>('1' + i), 0};
            const float numberSize = S(17);
            const ImVec2 numberExtent = measure(fonts_.label, numberSize, number);
            draw->AddText(fonts_.label, numberSize, ImVec2(c.x - numberExtent.x * 0.5f, c.y - numberSize * 0.54f),
                          col(state == Step::active ? kYellow : kPaperDim), number);
        }
        const ImU32 titleColor = col(state == Step::pending ? kPaperDim : state == Step::active ? kYellow : kPaper);
        const float titleY = spoken ? stepY - S(2) : c.y - titleSize * 0.54f;
        spacedText(draw, fonts_.label, titleSize, ImVec2(textX, titleY), titleColor, upper(words[i].title).c_str(),
                   0.06f);
        if (spoken)
            draw->AddText(fonts_.body, textSize, ImVec2(textX, stepY + titleSize + S(1)), col(kPaperDim),
                          words[i].text, nullptr, textWidth);
        stepY += height + gap;
    }
    const float stepsBottom = stepY;

    // START VR, the one red thing, on a dark red drop; STOP VR keylined in red.
    ImGui::SetCursorScreenPos(ImVec2(left, buttonY));
    const float stickerDrop = std::round(std::max(S(4), buttonHeight * 0.07f));
    const ImVec2 buttonSize(innerWidth - stickerDrop, buttonHeight);
    const Paint stop{col(kSurfaceHigh), col(kSurfaceHover), col(kPaper), col(kRedLift), 0};
    if (!running) {
        if (sticker("##start", kPlay, "Start VR", nullptr, buttonSize,
                    {col(kRed), col(kRedLift), col(kWhite), col(kRedDrop), col(kRedDrop)}, reasons.empty()))
            start();
        if (!reasons.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            std::string why;
            for (const auto& r : reasons)
                why += (why.empty() ? "" : "\n") + r;
            tip(why.c_str());
        }
    } else if (!session_.stopRequested()) {
        if (sticker("##stop", kStop, "Stop VR", nullptr, buttonSize, stop))
            session_.requestStop();
    } else {
        sticker("##stopping", nullptr, "Stopping VR", nullptr, buttonSize, stop, false);
    }

    // Which options VR starts with, and where to change them.
    const ProfileState profile = profileState();
    const std::string name = profile.current   ? profile.current->name
                             : profile.changed ? profile.remembered->name + "*"
                                               : std::string("Your own");
    spacedText(draw, fonts_.kicker, S(13), ImVec2(left, lineY + S(3)), col(kPaperDim), "OPTIONS", 0.14f);
    const float nameX = left + spacedWidth(fonts_.kicker, S(13), "OPTIONS", 0.14f) + S(12);
    const float changeWidth = measure(fonts_.body, ImGui::GetFontSize(), "Change").x;
    const float nameRoom = left + innerWidth - changeWidth - S(14) - nameX;
    draw->PushClipRect(ImVec2(nameX, lineY), ImVec2(nameX + nameRoom, lineY + lineHeight), true);
    draw->AddText(fonts_.semibold, S(15), ImVec2(nameX, lineY), col(kPaper), name.c_str());
    draw->PopClipRect();
    ImGui::SetCursorScreenPos(
        ImVec2(nameX + std::min(nameRoom, measure(fonts_.semibold, S(15), name.c_str()).x) + S(14), lineY));
    if (link("Change"))
        tab_ = Tab::options;
    // Behind it all, faint dots rising into the lower right corner: from under the steps to over the line
    // under START VR, wider than high.
    draw->ChannelsSetCurrent(0);
    const ImVec2 corner(max.x, lineY - S(10));
    const float reach = std::clamp(corner.y - stepsBottom, S(60), S(400));
    halftone(draw, ImVec2(max.x - reach * 1.8f, corner.y - reach), ImVec2(max.x - S(2), corner.y), S(10),
             col(kDotsStrong), [&](ImVec2 p) {
                 return 0.95f - std::hypot((corner.x - p.x) / 1.8f, corner.y - p.y) / reach;
             });
    draw->ChannelsMerge();
    ImGui::EndChild();
}

void App::shortcutBanner(float width) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = S(58);
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), col(kSurface));
    draw->AddRect(at, ImVec2(at.x + width, at.y + height), col(kLine));
    slanted(draw, ImVec2(at.x + S(20), at.y + height * 0.5f - S(9)), ImVec2(at.x + S(25), at.y + height * 0.5f + S(9)),
            col(kYellow));
    const char* message = shortcutMessage_.empty() ? "Install Spidy on this PC: add Spidy VR to your desktop and Start menu."
                                                   : shortcutMessage_.c_str();
    const float buttonWidth = S(150), textX = at.x + S(40);
    const float textWidth = width - (textX - at.x) - buttonWidth * 2 - S(40);
    const ImVec2 textSize = measure(fonts_.semibold, S(15), message, textWidth);
    draw->AddText(fonts_.semibold, S(15), ImVec2(textX, at.y + (height - textSize.y) * 0.5f), col(kPaper), message,
                  nullptr, textWidth);
    ImGui::SetCursorScreenPos(
        ImVec2(at.x + width - S(16) - buttonWidth * 2 - S(10), at.y + (height - ImGui::GetFrameHeight()) * 0.5f));
    if (button("Add shortcuts", buttonWidth, Ink::primary)) {
        std::string error;
        if (createShortcuts(error)) {
            settings_.shortcutsAsked = true;
            save();
        } else {
            shortcutMessage_ = error;
        }
    }
    ImGui::SameLine(0, S(10));
    if (button("Not now", buttonWidth)) {
        settings_.shortcutsAsked = true;
        save();
    }
}

std::string App::updateBlocker() {
    if (installFolder_.empty())
        return "This launcher runs from a Spidy checkout, which git updates.";
    if (session_.running())
        return "Stop VR first.";
    if (!scan_.done || scanner_.busy() || headset_.state() == Outcome::running ||
        vcInstall_.state() == Outcome::running)
        return "Wait for the checks on this PC to finish.";
    if (!scan_.writable)
        return "Spidy can't write in its folder. Move the Spidy folder somewhere like Documents.";
    return {};
}

float App::updateBanner(float width) {
    struct Button {
        const char* label;
        bool primary, enabled;
        std::string tip;
        std::function<void()> action;
    };
    const auto& release = update_.release;
    const UpdateState state = update_.state;
    const bool working = state == UpdateState::downloading || state == UpdateState::installing ||
                         state == UpdateState::installed;
    const bool offer = !installFolder_.empty() && release && !updateLater_ &&
                       (state == UpdateState::available || state == UpdateState::installFailed);
    if (!working && !offer && updatedFrom_.empty())
        return 0;
    std::string message;
    std::vector<Button> buttons;
    const bool failed = offer && state == UpdateState::installFailed;
    if (working) {
        const char* version = release->version.c_str();
        if (state == UpdateState::downloading) {
            const double total = static_cast<double>(std::max<uint64_t>(update_.total, 1));
            message = format("Downloading Spidy %s...  %.0f%%  (%.1f of %.1f MB)", version,
                             100.0 * update_.received / total, update_.received / 1048576.0, total / 1048576.0);
            buttons.push_back({"Cancel", false, true, "", [&] { updater_.cancel(); }});
        } else {
            message = format(state == UpdateState::installing ? "Installing Spidy %s..."
                                                              : "Spidy %s is installed. Starting it...",
                             version);
        }
    } else if (offer) {
        std::string changes;
        for (size_t i = 0; i < release->changes.size() && i < 12; ++i)
            changes += (changes.empty() ? "\xE2\x80\xA2  " : "\n\xE2\x80\xA2  ") + release->changes[i];
        const std::string page = release->page.empty() ? kReleasesUrl : release->page;
        const std::string blocker = updateBlocker();
        message = failed ? format("Updating to Spidy %s failed: %s", release->version.c_str(), update_.message.c_str())
                         : format("Spidy %s is out: you have %s. Updating takes a few seconds; your settings and "
                                  "reports stay.",
                                  release->version.c_str(), kVersion);
        buttons.push_back({"What's new", false, true, changes, [page] { openUrl(page.c_str()); }});
        buttons.push_back({failed ? "Try again" : "Update now", true, blocker.empty(), blocker,
                           [&] { updater_.install(); }});
        buttons.push_back({"Later", false, true, "", [&] { updateLater_ = true; }});
    } else {
        message = format("Spidy is updated: %s to %s.", updatedFrom_.c_str(), kVersion);
        const std::string page = std::string(kReleasesUrl) + "/tag/v" + kVersion;
        buttons.push_back({"What's new", false, true, "", [page] { openUrl(page.c_str()); }});
        buttons.push_back({"OK", false, true, "", [&] { updatedFrom_.clear(); }});
    }

    // A strip of violet, the one big colour field, with the release as a new issue.
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const std::string issue = working ? "No. " + release->version : failed ? "Update failed" : offer ? "New issue" : "Updated";
    const float stampSize = S(14.5f);
    const float stampWidth = spacedWidth(fonts_.label, stampSize, upper(issue).c_str(), 0.06f) + stampSize * 1.2f;
    const float buttonWidth = S(124), gap = S(10);
    const float buttonsWidth = buttons.empty() ? 0 : static_cast<float>(buttons.size()) * (buttonWidth + gap) - gap;
    const float textX = at.x + S(18) + stampWidth + S(16);
    const float textWidth = at.x + width - S(18) - (buttons.empty() ? 0 : buttonsWidth + S(18)) - textX;
    const ImVec2 textSize = measure(fonts_.semibold, S(15), message.c_str(), textWidth);
    const float height = std::max(S(58), textSize.y + S(30));
    const ImVec2 end(at.x + width, at.y + height);
    draw->AddRectFilled(at, end, col(kSurface));
    halftone(draw, ImVec2(end.x - S(320), at.y + S(2)), ImVec2(end.x - S(2), end.y - S(2)), S(8), col(kDots),
             [&](ImVec2 p) { return 0.9f - std::hypot(end.x - p.x, end.y - p.y) / S(320); });
    draw->AddRect(at, end, col(kLine));
    // The release as a new issue, stamped: red to take, yellow when it failed, cyan once it is in.
    const Rgb tint = failed ? kYellow : offer ? kRedLift : working ? kPaperDim : kCyan;
    stamp(draw, ImVec2(at.x + S(18), at.y + height * 0.5f), issue.c_str(), col(kSurface), col(tint), col(tint),
          stampSize, -2);
    draw->AddText(fonts_.semibold, S(15), ImVec2(textX, at.y + (height - textSize.y) * 0.5f), col(kPaper),
                  message.c_str(), nullptr, textWidth);
    if (state == UpdateState::downloading && update_.total) {
        const float done =
            std::clamp(static_cast<float>(static_cast<double>(update_.received) / update_.total), 0.f, 1.f);
        const float left = textX, right = textX + textWidth, y = end.y - S(10);
        draw->AddRectFilled(ImVec2(left, y), ImVec2(right, y + S(4)), col(kInk));
        draw->AddRectFilled(ImVec2(left, y), ImVec2(left + (right - left) * done, y + S(4)), col(kYellow));
    }
    float x = end.x - S(18) - buttonsWidth;
    for (const auto& b : buttons) {
        ImGui::SetCursorScreenPos(ImVec2(x, at.y + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::PushID(b.label);
        ImGui::BeginDisabled(!b.enabled);
        const bool clicked = button(b.label, buttonWidth, b.primary ? Ink::primary : Ink::plain);
        ImGui::EndDisabled();
        if (!b.tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            tip(b.tip.c_str());
        ImGui::PopID();
        if (clicked)
            b.action();
        x += buttonWidth + gap;
    }
    return height;
}

void App::setupRow(Mark mark, const char* title, const std::string& detail, float actionWidth,
                   const std::function<void()>& action, bool last) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float textX = at.x + S(36);
    const float textWidth = width - S(36) - (actionWidth > 0 ? actionWidth + S(16) : 0);
    const std::string name = upper(title);
    const float titleSize = S(16.5f), detailSize = S(14);
    const ImVec2 detailExtent = measure(fonts_.body, detailSize, detail.c_str(), textWidth);
    const float height = std::max(S(54), S(10) + titleSize + S(2) + detailExtent.y + S(10));
    statusMark(draw, ImVec2(at.x + S(11), at.y + S(10) + titleSize * 0.5f), mark);
    spacedText(draw, fonts_.label, titleSize, ImVec2(textX, at.y + S(9)), col(kPaper), name.c_str(), 0.04f);
    draw->AddText(fonts_.body, detailSize, ImVec2(textX, at.y + S(10) + titleSize + S(2)), col(kPaperDim),
                  detail.c_str(), nullptr, textWidth);
    if (action) {
        ImGui::SetCursorScreenPos(ImVec2(at.x + width - actionWidth, at.y + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::PushID(title);
        ImGui::PushItemWidth(actionWidth);
        action();
        ImGui::PopItemWidth();
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + height));
    ImGui::Dummy(ImVec2(width, 0));
    if (!last)
        draw->AddLine(ImVec2(textX, at.y + height - S(0.5f)), ImVec2(at.x + width, at.y + height - S(0.5f)), col(kLine));
}

void App::setupCard(float width) {
    if (!beginCard("##setup", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY))
        return endCard();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), 0));
    cardTitle("This PC", S(116));
    ImGui::BeginDisabled(scanner_.busy() || session_.running());
    if (button("Re-check", S(116), Ink::plain, kRefresh))
        rescan();
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, S(4)));
    const bool running = session_.running();

    // The game.
    {
        Mark mark = Mark::busy;
        std::string detail = "Looking for the game...";
        const std::string path = narrow(scan_.gameExe);
        if (scan_.done && scan_.gameExe.empty()) {
            mark = Mark::error;
            detail = scan_.epicFound ? "The Epic Games Store has it installed, but not where. Show Spidy where "
                                       "Spider-Man.exe is."
                                     : "Not found in your Steam or Epic Games Store libraries. Show Spidy where "
                                       "Spider-Man.exe is.";
        } else if (scan_.done) {
            switch (scan_.build) {
            case Build::checking:
            case Build::unknown: detail = "Checking the game version...  " + path; break;
            case Build::supported:
                mark = Mark::ok;
                detail = (scan_.gameSource == "Steam" ? "Steam, supported version" : "Supported version, " + scan_.gameSource) +
                         (scan_.gameRunning ? ", running now" : "") + "\n" + path;
                break;
            case Build::sameVersion:
                mark = Mark::warning;
                detail = "Version " + scan_.gameVersion + ", but not Steam's file (" + scan_.gameSource +
                         "): Spidy tries it, and stops with a code if the game's code differs" +
                         (scan_.gameRunning ? ". Running now" : "") + "\n" + path;
                break;
            case Build::unsupported:
                mark = Mark::error;
                detail = scan_.gameSource == "Epic Games Store"
                             ? "Epic Games Store, version " + scan_.gameVersion + ". Spidy works with version " +
                                   scan_.expectedVersion + ".\n" + path
                             : spidy::launcher::unsupportedGame(scan_.gameVersion, scan_.expectedVersion,
                                                                scan_.steamApp) +
                                   "\n" + path;
                break;
            case Build::unreadable:
                mark = Mark::error;
                detail = "Spidy could not read this file.\n" + path;
                break;
            }
        }
        setupRow(mark, "Marvel's Spider-Man Remastered", detail, S(116), [&] {
            ImGui::BeginDisabled(running || !scan_.done);
            if (button(scan_.gameExe.empty() ? "Browse..." : "Change...", S(116))) {
                if (auto chosen = browseForGame(window_, scan_.gameExe)) {
                    settings_.gameExe = *chosen;
                    save();
                    rescan();
                }
            }
            ImGui::EndDisabled();
        });
    }
    // The VR runtime.
    {
        const Runtime* chosen = runtime();
        const bool automatic = settings_.runtime == kAutoRuntime;
        Mark mark = scan_.done ? Mark::ok : Mark::busy;
        std::string detail = "Looking for VR runtimes...";
        if (scan_.done && scan_.runtimes.empty()) {
            mark = Mark::error;
            detail = "No OpenXR runtime is installed. Quest 3 players: install Virtual Desktop (tested). SteamVR and the "
                     "Meta Quest Link app also provide one.";
        } else if (scan_.done && automatic) {
            detail = "Automatic: VR starts in the runtime your headset is connected to (Virtual Desktop, else "
                     "SteamVR or Meta Quest Link while running, else Windows' active one).";
        } else if (chosen) {
            mark = chosen->tested ? Mark::ok : Mark::info;
            detail = chosen->tested ? "Tested with Quest 3 over Virtual Desktop."
                     : chosen->name == "SteamVR"
                         ? "Played with Quest 3 over Steam Link; other SteamVR headsets should work."
                         : chosen->name + " has not been tested with Spidy yet. It should work; expect rough edges.";
            if (chosen->active)
                detail += " Windows' active VR runtime.";
        }
        setupRow(mark, "VR runtime", detail, S(210), [&] {
            if (scan_.runtimes.empty()) {
                if (button("Get Virtual Desktop", S(210), Ink::plain, kOpen))
                    openUrl("https://www.vrdesktop.net/");
                return;
            }
            ImGui::BeginDisabled(running);
            ImGui::PushFont(fonts_.semibold, 14);
            if (ImGui::BeginCombo("##runtime", automatic ? "Automatic" : chosen ? chosen->name.c_str() : "Choose...")) {
                if (ImGui::Selectable("Automatic  (recommended)", automatic)) {
                    settings_.runtime = kAutoRuntime;
                    headset_.reset();
                    save();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    tip("Each session asks the runtimes for your headset and uses the one that has it.");
                for (const auto& r : scan_.runtimes) {
                    const std::string label = r.name + (r.tested ? "  (tested)" : "") + "##" + narrow(r.manifest);
                    if (ImGui::Selectable(label.c_str(), chosen == &r)) {
                        settings_.runtime = r.manifest;
                        headset_.reset();
                        save();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        tip(narrow(r.manifest).c_str());
                }
                ImGui::EndCombo();
            }
            ImGui::PopFont();
            ImGui::EndDisabled();
        });
    }
    // The headset.
    {
        const Outcome state = headset_.state();
        const Runtime* chosen = runtime();
        const bool automatic = settings_.runtime == kAutoRuntime;
        Mark mark = Mark::info;
        std::string detail = "Connect it to this PC, then check it here (optional: Start VR checks it too).";
        if (chosen && chosen->name == "SteamVR" && state == Outcome::idle)
            detail += " Checking starts SteamVR.";
        else if (automatic && state == Outcome::idle)
            detail += " Checking shows which runtime has it.";
        if (state == Outcome::running) {
            mark = Mark::busy;
            detail = headset_.summary();
        } else if (state == Outcome::ok) {
            mark = Mark::ok;
            detail = headset_.summary();
        } else if (state == Outcome::error) {
            mark = Mark::warning;
            detail = headset_.summary();
        }
        setupRow(mark, "Headset", detail, S(116), [&] {
            ImGui::BeginDisabled(running || (!chosen && !automatic) || (automatic && scan_.python.empty()) ||
                                 state == Outcome::running || scan_.root.empty() || !scan_.missing.empty());
            if (button("Check", S(116)))
                headset_.start(scan_.root, automatic ? std::wstring(kAutoRuntime) : chosen->manifest, scan_.python);
            ImGui::EndDisabled();
        });
    }
    const Outcome install = vcInstall_.state();
    const bool filesOk = !scan_.root.empty() && scan_.missing.empty() && !scan_.python.empty() && scan_.writable;
    if (scan_.done && scan_.vcCurrent && filesOk && install != Outcome::running && install != Outcome::warning) {
        const std::string detail =
            format("Visual C++ runtime %s  \xC2\xB7  Python %s", scan_.vcVersion.c_str(),
                   scan_.pythonVersion.empty() ? "found" : scan_.pythonVersion.c_str());
        setupRow(Mark::ok, "Windows and Spidy's files", detail, S(116), [&] {
            if (button("Open folder", S(116), Ink::plain, kFolder))
                openPath(scan_.root);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                tip(narrow(scan_.root).c_str());
        }, true);
        ImGui::PopStyleVar();
        endCard();
        return;
    }
    // Visual C++ runtime.
    {
        Mark mark = scan_.done ? Mark::ok : Mark::busy;
        std::string detail = "Checking...";
        bool offer = false;
        if (install == Outcome::running) {
            mark = Mark::busy;
            detail = vcInstall_.message();
        } else if (scan_.done && scan_.vcCurrent) {
            detail = "Microsoft Visual C++ runtime " + scan_.vcVersion + ".";
            if (install == Outcome::warning) {
                mark = Mark::warning;
                detail = vcInstall_.message();
            }
        } else if (scan_.done) {
            mark = Mark::error;
            offer = true;
            detail = scan_.vcInstalled ? "Version " + scan_.vcVersion + " is too old for Spidy's modules (14.40 or newer)."
                                       : "Missing. Spidy's modules need Microsoft's Visual C++ runtime.";
            if (install == Outcome::error)
                detail += " " + vcInstall_.message();
        }
        setupRow(mark, "Visual C++ runtime", detail, offer ? S(116) : 0, offer ? std::function<void()>([&] {
            ImGui::BeginDisabled(running);
            if (button(scan_.vcInstalled ? "Update" : "Install", S(116), Ink::primary))
                vcInstall_.start();
            ImGui::EndDisabled();
        }) : std::function<void()>());
    }
    // Spidy's own files.
    {
        Mark mark = Mark::busy;
        std::string detail = "Checking...";
        if (scan_.done) {
            mark = Mark::error;
            if (scan_.root.empty()) {
                detail = "The tools folder is missing. Extract the whole Spidy zip and start the launcher from that folder.";
            } else if (!scan_.missing.empty()) {
                detail = "Missing:";
                for (const auto& name : scan_.missing)
                    detail += " " + name;
                detail += ". Extract the zip again; some antivirus programs remove these files.";
            } else if (scan_.python.empty()) {
                detail = "Python is missing (python\\python.exe in the Spidy folder).";
            } else if (!scan_.writable) {
                detail = "Spidy can't write in its folder. Move the Spidy folder somewhere like Documents.";
            } else {
                mark = Mark::ok;
                detail = "All modules present; Python " +
                         (scan_.pythonVersion.empty() ? std::string("found") : scan_.pythonVersion) + ".";
            }
        }
        setupRow(mark, "Spidy files", detail, S(116), [&] {
            if (button("Open folder", S(116), Ink::plain, kFolder))
                openPath(scan_.root.empty() ? std::filesystem::current_path().wstring() : scan_.root);
        }, true);
    }
    ImGui::PopStyleVar();
    endCard();
}

void App::logCard(ImVec2 size) {
    if (!beginCard("##log", size)) {
        endCard();
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8), 0));
    const float copyWidth = S(96), reportsWidth = S(116), summaryWidth = S(200);
    cardTitle("Session log", copyWidth + S(8) + reportsWidth + S(8) + summaryWidth);
    ImGui::BeginDisabled(log_.empty());
    if (button("Copy", copyWidth, Ink::plain, kCopy))
        copyLog();
    ImGui::EndDisabled();
    ImGui::SameLine(0, S(8));
    if (button("Reports", reportsWidth, Ink::plain, kFolder) && !scan_.root.empty())
        openPath((std::filesystem::path(scan_.root) / L"reports").wstring());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        tip("Every session leaves a report there: send it with a bug report.");
    ImGui::SameLine(0, S(8));
    // For a help post: what a helper asks first, ready to paste. The button says so for two seconds.
    const bool copied = ImGui::GetTime() - summaryCopied_ < 2;
    if (button(copied ? "Copied###summary" : "Copy for Discord###summary", summaryWidth, Ink::plain, kCopy)) {
        ImGui::SetClipboardText(summary().c_str());
        summaryCopied_ = ImGui::GetTime();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        tip("Copies your Spidy version, headset, PC and last session in a few lines: paste them into your post "
            "on the Discord.");
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vec(kWell));
    ImGui::PushStyleColor(ImGuiCol_Border, vec(kLine));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(12)));
    ImGui::BeginChild("##lines", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (log_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, vec(kPaperDim));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(8)));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("What the session does shows here, line by line, as soon as VR starts.");
        ImGui::TextUnformatted("Something went wrong? Copy puts these lines on the clipboard: post them on the "
                               "Discord with what happened.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    } else {
        ImGui::PushFont(fonts_.mono, 13);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(3)));
        const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - S(4);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(log_.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& line = log_[i];
                const Rgb tint = line.kind == LineKind::error     ? kRedLift
                                 : line.kind == LineKind::warning ? kYellow
                                 : line.kind == LineKind::good    ? kCyan
                                                                  : kLogText;
                ImGui::PushStyleColor(ImGuiCol_Text, vec(tint));
                ImGui::TextUnformatted(line.text.c_str());
                ImGui::PopStyleColor();
            }
        if (scrollLog_ && atBottom)
            ImGui::SetScrollHereY(1.0f);
        scrollLog_ = false;
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    endCard();
}

void App::option(const char* title, const char* help, float controlWidth, const std::function<void()>& control,
                 bool warning) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    // A rule between rows, none above the card's first.
    if (!firstOption_)
        draw->AddLine(start, ImVec2(start.x + width, start.y), col(kLine));
    const ImVec2 at(start.x, start.y + (firstOption_ ? S(4) : S(12)));
    firstOption_ = false;
    const float textWidth = width - controlWidth - S(16);
    const ImVec2 titleSize = measure(fonts_.semibold, S(15.5f), title, textWidth);
    const ImVec2 helpSize = measure(fonts_.body, S(13.5f), help, textWidth);
    draw->AddText(fonts_.semibold, S(15.5f), at, col(kPaper), title, nullptr, textWidth);
    draw->AddText(fonts_.body, S(13.5f), ImVec2(at.x, at.y + titleSize.y + S(2)), col(warning ? kYellow : kPaperDim),
                  help, nullptr, textWidth);
    const float height = std::max(titleSize.y + S(2) + helpSize.y, ImGui::GetFrameHeight());
    ImGui::SetCursorScreenPos(ImVec2(at.x + width - controlWidth,
                                     at.y + std::max(0.f, (titleSize.y - ImGui::GetFrameHeight()) * 0.5f) - S(1)));
    ImGui::PushID(title);
    ImGui::PushItemWidth(controlWidth);
    control();
    ImGui::PopItemWidth();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(start.x, at.y + height + S(12)));
    ImGui::Dummy(ImVec2(width, 0));
}

void App::options(ImVec2 origin, ImVec2 size) {
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginChild("##optionsScroll", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - S(6);
    const float gap = S(18);
    float top = base.y;
    const bool running = session_.running();
    if (running) {
        top += captionBox(ImGui::GetWindowDrawList(), ImVec2(base.x, top + S(4)), width - S(8),
                          "These options apply when VR starts. To change them now, pause in the headset: Settings, "
                          "then SPIDY VR.",
                          Caption::narrator) +
               S(4) + gap;
    }
    ImGui::BeginDisabled(running);
    ImGui::SetCursorScreenPos(ImVec2(base.x, top));
    profileCard(width);
    top = ImGui::GetItemRectMax().y + gap;

    auto& o = settings_.options;
    bool changed = false;
    // A value the list does not have shows as the nearest one.
    const auto stepCombo = [&](const char* id, const char* const* labels, const int* values, int count, int* value) {
        int index = 0;
        for (int i = 1; i < count; ++i)
            if (std::abs(values[i] - *value) < std::abs(values[index] - *value))
                index = i;
        ImGui::PushFont(fonts_.semibold, 14);
        if (ImGui::Combo(id, &index, labels, count)) {
            *value = values[index];
            changed = true;
        }
        ImGui::PopFont();
    };
    // The settings no launcher option carries, which the game module keeps in spidy_vr.ini (tabOnly_): the
    // tab's own choices, so the two always offer the same steps.
    using spidy::vr_settings::Item;
    bool tabChanged = false;
    const auto tabCombo = [&](const char* id, Item item) {
        std::vector<std::string> labels;
        for (const auto& row : spidy::vr_settings::rows())
            if (row.item == item)
                for (const char* choice : row.choices)
                    labels.push_back(launcherCase(choice));
        std::vector<const char*> texts;
        for (const auto& label : labels)
            texts.push_back(label.c_str());
        int index = spidy::vr_settings::choice(item, tabOnly_);
        ImGui::PushFont(fonts_.semibold, 14);
        if (ImGui::Combo(id, &index, texts.data(), static_cast<int>(texts.size())))
            tabChanged |= spidy::vr_settings::choose(item, index, tabOnly_);
        ImGui::PopFont();
    };
    const auto tabToggle = [&](const char* id, Item item) {
        bool on = spidy::vr_settings::choice(item, tabOnly_) != 0;
        if (toggle(id, &on))
            tabChanged |= spidy::vr_settings::choose(item, on ? 1 : 0, tabOnly_);
    };
    const float columnWidth = (width - gap) * 0.5f;
    float columnY[2] = {top, top};
    // A card of options in a column: webs, moving and walls on the left; you, the picture, the screens and
    // troubleshooting on the right.
    const auto group = [&](int column, const char* title, const std::function<void()>& rows) {
        ImGui::SetCursorScreenPos(ImVec2(base.x + column * (columnWidth + gap), columnY[column]));
        if (beginCard(title, ImVec2(columnWidth, 0), ImGuiChildFlags_AutoResizeY)) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), 0));
            cardTitle(title);
            ImGui::Dummy(ImVec2(0, S(8)));
            rows();
            ImGui::PopStyleVar();
        }
        endCard();
        columnY[column] = ImGui::GetItemRectMax().y + gap;
    };
    group(0, "Webs", [&] {
        option("Web style", "Arcade: webs pass lamp posts, signs and cars, and swings stay above the street.", S(150),
               [&] {
                   static constexpr int styles[] = {0, 1};
                   int arcade = o.arcadeWebs ? 1 : 0;
                   stepCombo("##webStyle", kWebStyles, styles, 2, &arcade);
                   o.arcadeWebs = arcade != 0;
               });
        option("Web button", "Which button shoots and holds a web; the other reels in and shoots web balls.", S(150),
               [&] {
                   static constexpr int buttons[] = {0, 1};
                   int trigger = o.triggerWebs ? 1 : 0;
                   stepCombo("##webButton", kWebButtons, buttons, 2, &trigger);
                   o.triggerWebs = trigger != 0;
               });
        option("Webs hold in open air", "With nothing in reach, a web still holds 100 m out; off, it misses.", S(42),
               [&] { changed |= toggle("##air", &o.airWebs); });
        option("Smart anchor",
               "With webs holding in open air off, a web that just misses still catches the real surface "
               "nearest it.",
               S(42), [&] { tabToggle("##smartanchor", Item::smartAnchor); });
        option("Aim markers", "Show where each hand's web would land.", S(42),
               [&] { changed |= toggle("##aim", &o.aimMarkers); });
        option("Eye aim",
               "Web balls and zips go where your eyes look. Only where the headset and its VR runtime track "
               "the eyes (Quest Pro, Varjo, Pimax Crystal...); elsewhere hands and head aim as before.",
               S(150), [&] { tabCombo("##eyeaim", Item::eyeAim); });
    });
    group(0, "Moving", [&] {
        option("Swing speed limit", "How fast a swing can carry you.", S(150), [&] {
            ImGui::PushFont(fonts_.semibold, 14);
            changed |= slider("##speed", &o.swingSpeed, 10, 65, "%d m/s");
            ImGui::PopFont();
        });
        option("Weight", "How heavy you are while swinging and after letting go. 100% is real gravity.", S(150), [&] {
            stepCombo("##weight", kWeights, kWeightValues, static_cast<int>(std::size(kWeightValues)), &o.weight);
        });
        option("Elastic web", "Webs stretch under load and catch you more softly; higher is softer.", S(150),
               [&] { tabCombo("##elasticweb", Item::elasticWeb); });
        option("Web slingshot", "No auto jump. Draw two fixed webs, hold for vibration, release both to launch.",
               S(42), [&] { tabToggle("##webslingshot", Item::webSlingshot); });
        option("Better climbing", "Take one web in both hands, then pull hand over hand to keep climbing it.",
               S(42), [&] { tabToggle("##betterclimbing", Item::betterClimbing); });
        option("Snap turn", "How far a flick of the right stick turns you.", S(150), [&] {
            stepCombo("##snap", kSnapTurns, kSnapValues, static_cast<int>(std::size(kSnapValues)), &o.snapTurn);
        });
        option("Smooth turn", "Turn steadily while you hold the right stick; off, it snap turns.", S(150), [&] {
            stepCombo("##smooth", kSmoothTurns, kSmoothValues, static_cast<int>(std::size(kSmoothValues)),
                      &o.smoothTurn);
        });
    });
    group(0, "Walls", [&] {
        option("Hand wall climbing (experimental)",
               "On a wall with the left stick at rest, squeeze a grip near the wall to hold on and pull to "
               "climb; both grips held roll your view along it.",
               S(42), [&] { tabToggle("##handclimb", Item::handClimb); });
        option("Wall Run Camera Tilt", "Gently bank with the body on horizontal momentum wall runs.", S(42),
               [&] { tabToggle("##wallruncameratilt", Item::wallRunCameraTilt); });
    });
    group(0, "Flips", [&] {
        option("Flips (experimental)",
               "Right stick flips; up zips/aims. Release holds the angle. A returns upright.",
               S(42), [&] { changed |= toggle("##flips", &o.flips); });
        option("Controlled Flips",
               "In air: A enables left-stick flips. Release holds the angle. A again resets and exits. "
               "Overrides basic Flips; Realistic Flips keeps its controls.",
               S(42), [&] { tabToggle("##controlledflips", Item::controlledFlips); });
        option("Flip speed", "How fast the steering stick turns you at full tilt.", S(150), [&] {
            stepCombo("##flipspeed", kFlipSpeeds, kFlipSpeedValues, static_cast<int>(std::size(kFlipSpeedValues)),
                      &o.flipSpeed);
        });
        option("Realistic flips (experimental)",
               "Webs and arm swings spin you in the air, and landing brings you upright. While on, it takes "
               "the place of Flips' turns, and with Flips on too the left stick steers.",
               S(42), [&] { tabToggle("##realistic", Item::realisticFlips); });
        // Its tuning, under it while it is on.
        if (tabOnly_.realisticFlips) {
            ImGui::Indent(S(16));
            option("Rotation strength",
                   "On a web, how strongly your body turns toward its pull. Where you face stays yours.", S(150),
                   [&] { tabCombo("##strength", Item::automaticStrength); });
            option("Max rotation speed", "The fastest it turns you, the spin after letting go too.", S(150),
                   [&] { tabCombo("##maxspeed", Item::automaticSpeed); });
            option("Minimum flight speed", "A web turns you only while you travel faster than this.", S(150),
                   [&] { tabCombo("##flightspeed", Item::automaticMinimumSpeed); });
            option("Swing momentum", "Letting go of the last web keeps the swing's spin.", S(42),
                   [&] { tabToggle("##momentum", Item::automaticReleaseFlip); });
            option("Rotation response", "How soon a turn gets up to speed and slows down. Longer is gentler.",
                   S(150), [&] { tabCombo("##response", Item::automaticResponse); });
            option("Hand momentum",
                   "How much swinging your arms in the air turns you; swing them back to slow down.", S(150),
                   [&] { tabCombo("##handmomentum", Item::automaticHandStrength); });
            option("Angular drag", "How quickly a spin dies away without more webs or arm swings.", S(150),
                   [&] { tabCombo("##drag", Item::automaticFalloff); });
            option("Minimum hand speed", "Arm swings slower than this do not turn you.", S(150),
                   [&] { tabCombo("##handspeed", Item::automaticHandThreshold); });
            option("Air rotation brake", "A in the air stops the spin and brings you upright.", S(42),
                   [&] { tabToggle("##brake", Item::automaticAirBrake); });
            option("Flips in combat",
                   "Off: while enemies are near, and 3 s after, arms and webs stop turning you and you come "
                   "upright.",
                   S(42), [&] { tabToggle("##combat", Item::automaticCombat); });
            ImGui::Unindent(S(16));
        }
    });
    group(1, "Body and haptics", [&] {
        // The T-pose calibration made in the headset; Redo forgets it, and the next session asks for it again.
        const bool calibrated = spidy::launcher::calibrated(o);
        const std::string calibration =
            calibrated ? format("Height %.2f m (eyes at %.2f m), arm %.2f m, from your T-pose in the headset. Redo "
                                "asks again when VR starts.",
                                spidy::body_calibration::height(o.eyeHeightMm / 1000.f), o.eyeHeightMm / 1000.0,
                                o.armLengthMm / 1000.0)
            : o.calibrationPrompt
                ? std::string("When VR starts, stand in a T-pose and hold both triggers: Spider-Man takes your height "
                              "and arms.")
                : std::string("Skipped: your height comes from the headset. Ask again to get the T-pose when VR "
                              "starts.");
        option("Body calibration", calibration.c_str(), S(116), [&] {
            if ((calibrated || !o.calibrationPrompt) && button(calibrated ? "Redo" : "Ask again", S(116))) {
                o.eyeHeightMm = o.armLengthMm = 0;
                o.calibrationPrompt = true;
                changed = true;
            }
        });
        // HEIGHT and SEATED (seated.hpp), kept with the tab's own in spidy_vr.ini.
        option("Height", "Yours: Spider-Man's size, and how high seated play raises you. Auto: from the "
                         "calibration or the headset.",
               S(150), [&] { tabCombo("##height", Item::height); });
        option("Seated", "Play sitting down: your view and body rise to your standing height. Recenter the "
                         "headset after you sit down.",
               S(42), [&] { tabToggle("##seated", Item::seated); });
        option("Controller vibration", "How strongly webs and punches buzz.", S(150), [&] {
            stepCombo("##haptics", kHaptics, kHapticValues, static_cast<int>(std::size(kHapticValues)), &o.haptics);
        });
        option("Body haptics", "A bHaptics vest, sleeves and visor feel webs, blows, wind and Spider-Sense; the "
                               "bHaptics Player has to be running.",
               S(150), [&] {
                   stepCombo("##bodyhaptics", kHaptics, kHapticValues, static_cast<int>(std::size(kHapticValues)),
                             &o.bodyHaptics);
               });
        // Its groups, under it while it plays.
        if (o.bodyHaptics > 0) {
            ImGui::Indent(S(16));
            option("Web haptics", "Shots, holds, the swing's pull, zips and grabs.", S(42),
                   [&] { tabToggle("##hapticwebs", Item::hapticWebs); });
            option("Wind haptics", "The wind when you fly or fall free of webs and walls.", S(42),
                   [&] { tabToggle("##hapticwind", Item::hapticWind); });
            option("Combat haptics", "Punches, hits, Spider-Sense, dodges, heals and slow motion.", S(42),
                   [&] { tabToggle("##hapticcombat", Item::hapticCombat); });
            option("Movement haptics", "Landings, walls, wall climbing and charged leaps.", S(42),
                   [&] { tabToggle("##hapticmovement", Item::hapticMovement); });
            ImGui::Unindent(S(16));
        }
    });
    group(1, "Picture", [&] {
        // Percent of the headset's own size, so no choice looks sharper than it is; with the headset checked,
        // the pixels it makes.
        std::string resolutionHelp = "Of the headset's own, per side. Higher is sharper; lower is faster.";
        if (const auto eye = headset_.eye()) {
            const auto rendered = eyeSize();
            resolutionHelp = format("%u x %u per eye; the headset asks for %u x %u. Higher is sharper; lower is faster.",
                                    rendered[0], rendered[1], (*eye)[0], (*eye)[1]);
        }
        if (o.renderScale > 100)
            resolutionHelp += " Costs frame rate and memory.";
        option("Render resolution", resolutionHelp.c_str(), S(150), [&] {
            ImGui::PushFont(fonts_.semibold, 14);
            if (slider("##resolution", &o.renderScale, static_cast<int>(spidy::minimumRenderScale),
                       static_cast<int>(spidy::maximumRenderScale), "%d%%")) {
                o.renderScale = (o.renderScale + 2) / 5 * 5;
                changed = true;
            }
            ImGui::PopFont();
        });
        // What the scene is rendered at before the game's anti-aliasing brings it to the eye's size; with the
        // headset checked, in pixels.
        std::string upscaleHelp = "The scene is rendered smaller and brought to the eye's size, anti-aliased. Lower "
                                  "is faster and softer.";
        if (headset_.eye()) {
            const auto rendered = eyeSize();
            const auto scene = spidy::eyeRenderSize(rendered[0], rendered[1], static_cast<uint32_t>(o.upscale));
            upscaleHelp = o.upscale >= 100
                              ? format("The scene is rendered at the eye's %u x %u, anti-aliased. Lower is faster.",
                                       rendered[0], rendered[1])
                              : format("The scene is rendered at %u x %u and brought to %u x %u, anti-aliased. Lower "
                                       "is faster and softer.",
                                       scene[0], scene[1], rendered[0], rendered[1]);
        }
        option("Upscaling", upscaleHelp.c_str(), S(150), [&] {
            stepCombo("##upscale", kUpscales, kUpscaleValues, static_cast<int>(std::size(kUpscaleValues)), &o.upscale);
        });
        option("Game's depth of field",
               "Off: VR turns the game's Depth of Field off while it runs (its blur is not where you look). On: your "
               "game setting stays.",
               S(42), [&] { changed |= toggle("##dof", &o.gameDepthOfField); });
        option("Game's sharpening",
               "Off: VR sets the game's Sharpness to its lowest while it runs. On: your game setting stays.", S(42),
               [&] { changed |= toggle("##sharpen", &o.gameSharpening); });
        option("Near clip",
               "How near your eyes things still show. Nearer keeps hands held up before your face in view.", S(150),
               [&] { tabCombo("##nearclip", Item::nearClip); });
    });
    group(1, "Game screen and HUD", [&] {
        option("Game screen size", "The headset's screen for menus, cutscenes and flat mode.", S(150), [&] {
            static constexpr int sizes[] = {0, 1, 2};
            stepCombo("##screen", kScreenSizes, sizes, 3, &o.screenSize);
        });
        option("Game screen 3D",
               "Cutscenes have depth on that screen; higher brings the scene nearer. Off, they are flat.", S(150), [&] {
                   static constexpr int depths[] = {0, 1, 2, 3};
                   stepCombo("##depth", kScreenDepths, depths, 4, &o.screenDepth);
               });
        option("HUD", "How large the game's HUD shows in front of you; off, there is none in VR.", S(150), [&] {
            static constexpr int sizes[] = {0, 1, 2, 3};
            stepCombo("##hud", kHudSizes, sizes, 4, &o.hud);
        });
        option("UI style",
               "Spidy's own marks, gadget wheel and meters: Comic, printed like Spidy's videos, or Tech, like the "
               "game's HUD.",
               S(150), [&] { tabCombo("##uistyle", Item::uiStyle); });
        option("Combat visuals",
               "Show punch arrows, target rings, hit effects and KO text. Spider-Sense stays on.",
               S(42), [&] { tabToggle("##combatvisuals", Item::combatVisuals); });
    });
    group(1, "Monitor", [&] {
        option("Small game window", "Saves GPU time while you play in VR.", S(42),
               [&] { changed |= toggle("##small", &o.smallWindow); });
        option("Monitor frame rate",
               "How often the monitor gets your left eye in VR. Each picture costs the graphics card time the "
               "headset could have.",
               S(150), [&] {
                   stepCombo("##monitor", kMonitorRates, kMonitorRateValues,
                             static_cast<int>(std::size(kMonitorRateValues)), &o.monitorRate);
               });
        option("Recording FOV",
               "A wider view on the monitor to record, at least this wide across, instead of your left "
               "eye. Costs frame rate; Spidy's marks stay in the headset.",
               S(150), [&] { tabCombo("##recordingfov", Item::recordingFov); });
    });
    // A way round a display driver error as VR starts (PLAYERS.md), apart from the monitor's options since
    // October 25: beside them it read as one of them, and Recording FOV does nothing while it is on.
    group(1, "Troubleshooting", [&] {
        option("Normal camera on the monitor",
               "Only for a display driver error as VR starts: the game keeps its own camera, and the "
               "monitor shows it. Costs frame rate; shadows and far detail follow that camera, and "
               "Recording FOV does nothing.",
               S(42), [&] { changed |= toggle("##stock", &o.stockMonitorView); });
    });
    ImGui::EndDisabled();
    if (changed)
        save();
    if (tabChanged && !saveTabOnly(tabOnly_))
        profileStatus("Spidy could not save that setting in %APPDATA%\\Spidy\\spidy_vr.ini.", true);
    ImGui::SetCursorScreenPos(ImVec2(base.x, std::max(columnY[0], columnY[1]) - gap));
    ImGui::Dummy(ImVec2(width, S(6)));
    ImGui::EndChild();
}

// --- Profiles ----------------------------------------------------------------------

void App::refreshProfiles() {
    profiles_ = savedProfiles();
    tabOnly_ = loadTabOnly();
    profilesStale_ = false;
}

App::ProfileState App::profileState() {
    namespace text = spidy::launcher;
    if (profilesStale_)
        refreshProfiles();
    const text::PlaySettings now = playSettings();
    // The profile these settings are: the one last used or saved while they still are its, else the first
    // that holds them all.
    ProfileState state;
    for (const auto& saved : profiles_)
        if (saved.name == settings_.profile)
            state.remembered = &saved;
    if (state.remembered && text::profileMatches(state.remembered->profile, now))
        state.current = state.remembered;
    for (const auto& saved : profiles_)
        if (!state.current && saved.profile.complete() && text::profileMatches(saved.profile, now))
            state.current = &saved;
    state.changed = !state.current && state.remembered;
    return state;
}

void App::profileStatus(std::string message, bool warning) {
    profileStatus_ = std::move(message);
    profileWarning_ = warning;
    profileStatusUntil_ = ImGui::GetTime() + 10;
}

bool App::keepProfile(const Profile& profile) {
    profilesStale_ = true;
    if (saveProfile(profile.name, spidy::launcher::profileText(profile, kVersion)))
        return true;
    profileStatus("Spidy could not save the profile in " + narrow(profilesFolder()) + ".", true);
    return false;
}

void App::useProfile(const Profile& profile, const std::string& name) {
    namespace text = spidy::launcher;
    text::PlaySettings settings = playSettings();
    // Previous settings themselves: back to what was used before, and what is used now takes their place,
    // to switch back with.
    const bool previous = name == kPreviousProfile;
    std::string message = previous ? "Back to your previous settings." : "Using " + name + ".";
    const bool held = std::any_of(profiles_.begin(), profiles_.end(), [&](const SavedProfile& saved) {
        return saved.profile.complete() && text::profileMatches(saved.profile, settings);
    });
    if (!held && !text::profileMatches(profile, settings)) {
        if (!keepProfile(text::profileOf(settings, kPreviousProfile)))
            return;
        message += previous ? " What you had is Previous settings now."
                            : std::string(" What you had is kept as ") + kPreviousProfile + ".";
    }
    text::applyProfile(profile, settings);
    settings_.options = settings.options;
    // Previous settings are no profile to save changes to.
    settings_.profile = previous ? std::string() : name;
    save();
    if (!(settings.tab == tabOnly_)) {
        if (!saveTabOnly(settings.tab)) {
            profileStatus("Spidy could not save the SPIDY VR tab's own settings of " + name + ".", true);
            return;
        }
        tabOnly_ = settings.tab;
    }
    profileStatus(message);
}

void App::openImport(std::string_view text, const std::string& fallbackName, const std::string& source) {
    auto profile = spidy::launcher::readProfile(text);
    tab_ = Tab::options;
    if (!profile) {
        profileStatus(source + " holds no Spidy profile.", true);
        return;
    }
    // Shared while it was kept as the settings before another profile: the next profile used would
    // replace it.
    std::string name = spidy::launcher::profileName(profile->name.empty() ? fallbackName : profile->name);
    if (spidy::launcher::profileFileName(name).empty() || _stricmp(name.c_str(), kPreviousProfile) == 0)
        name = "Shared profile";
    std::snprintf(profileName_.data(), profileName_.size(), "%s", name.c_str());
    importing_ = std::move(profile);
    wantImport_ = true;
}

void App::importFile(const std::wstring& path) {
    const std::filesystem::path file(path);
    openImport(readTextFile(path), narrow(file.stem().wstring()), narrow(file.filename().wstring()));
}

void App::profileCard(float width) {
    namespace text = spidy::launcher;
    const ProfileState state = profileState();
    const SavedProfile* current = state.current;
    const SavedProfile* remembered = state.remembered;
    const bool changed = state.changed;
    const text::PlaySettings now = playSettings();
    const bool status = ImGui::GetTime() < profileStatusUntil_;
    const std::string help =
        status    ? profileStatus_
        : changed ? "Changed since you used " + remembered->name + ". Save changes keeps them in it."
                  : std::string("Save, switch and share sets of these options: copy yours into a Discord message, "
                                "paste someone else's, or drop a .spidy file on this window.");
    const std::string preview = current   ? current->name
                                : changed ? remembered->name + "*"
                                          : std::string("None");
    enum class Action { none, use, saveChanges, saveAs, copy, exportFile, paste, importFile, remove, folder };
    Action action = Action::none;
    std::optional<SavedProfile> chosen;
    const auto section = [&](const char* name) {
        ImGui::PushFont(fonts_.kicker, 12.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, vec(kPaperDim));
        ImGui::SeparatorText(upper(name).c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    };
    if (beginCard("##profile", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY)) {
        const float resetWidth = S(170);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), 0));
        cardTitle("Profile", resetWidth);
        if (button("Reset options", resetWidth)) {
            // Your body's measurements are not options: they stay.
            auto& o = settings_.options;
            const SessionOptions kept = o;
            o = SessionOptions{};
            o.eyeHeightMm = kept.eyeHeightMm;
            o.armLengthMm = kept.armLengthMm;
            o.calibrationPrompt = kept.calibrationPrompt;
            save();
            // And those spidy_vr.ini keeps.
            if (!(tabOnly_ == spidy::vr_settings::Values{})) {
                if (saveTabOnly({}))
                    tabOnly_ = {};
                else
                    profileStatus("Spidy could not reset the settings in %APPDATA%\\Spidy\\spidy_vr.ini.",
                                  true);
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            tip("Every option here back to Spidy's own; your body calibration stays.");
        ImGui::Dummy(ImVec2(0, S(10)));
        ImGui::PopStyleVar();
        ImGui::SetNextItemWidth(S(280));
        ImGui::PushFont(fonts_.semibold, 15);
        if (ImGui::BeginCombo("##profile", preview.c_str(), ImGuiComboFlags_HeightLargest)) {
            // Profiles put in the folder meanwhile show.
            if (ImGui::IsWindowAppearing())
                profilesStale_ = true;
            // The card has no space between its rows; a menu does.
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(7)));
            section("Your profiles");
            if (profiles_.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, vec(kPaperDim));
                ImGui::TextUnformatted("None yet. Save these options as one.");
                ImGui::PopStyleColor();
            }
            for (const auto& saved : profiles_) {
                ImGui::PushID(saved.name.c_str());
                if (ImGui::Selectable(saved.name.c_str(), &saved == current)) {
                    action = Action::use;
                    chosen = saved;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    const auto changes = text::profileChanges(saved.profile, now);
                    std::string lines = changes.empty() ? "These options." : "Using it changes:";
                    for (size_t i = 0; i < changes.size() && i < 12; ++i)
                        lines += "\n" + changes[i].setting + "   " + changes[i].from + "  \xE2\x86\x92  " +
                                 changes[i].to;
                    if (changes.size() > 12)
                        lines += format("\nand %d more.", static_cast<int>(changes.size() - 12));
                    tip(lines.c_str());
                }
                ImGui::PopID();
            }
            if (changed && ImGui::Selectable(("Save changes to " + remembered->name).c_str()))
                action = Action::saveChanges;
            if (ImGui::Selectable("Save as a profile..."))
                action = Action::saveAs;
            section("Share yours");
            if (ImGui::Selectable("Copy to share"))
                action = Action::copy;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                tip("Copies these options as text: paste it into a Discord message, say.");
            if (ImGui::Selectable("Export a file..."))
                action = Action::exportFile;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                tip("Saves these options as a .spidy file to send.");
            section("Use someone else's");
            if (ImGui::Selectable("Paste a profile"))
                action = Action::paste;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                tip("One copied from a Discord message, say. You see what it changes before it does.");
            if (ImGui::Selectable("Import a file..."))
                action = Action::importFile;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                tip("A .spidy file. Dropping one on this window imports it too.");
            ImGui::Separator();
            if (ImGui::BeginMenu("Delete a profile", !profiles_.empty())) {
                for (const auto& saved : profiles_)
                    if (ImGui::MenuItem(saved.name.c_str())) {
                        action = Action::remove;
                        chosen = saved;
                    }
                ImGui::EndMenu();
            }
            if (ImGui::Selectable("Open the profiles folder"))
                action = Action::folder;
            ImGui::PopStyleVar();
            ImGui::EndCombo();
        }
        ImGui::PopFont();
        ImGui::SameLine(0, S(18));
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, vec(status && profileWarning_ ? kYellow : kPaperDim));
        ImGui::PushTextWrapPos(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(help.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, S(2)));
    }
    endCard();
    // Shared under the name of the profile they are, else none: whoever imports them names them.
    const std::string name = current ? current->name : std::string();
    switch (action) {
    case Action::none: break;
    case Action::use: useProfile(chosen->profile, chosen->name); break;
    case Action::saveChanges:
        if (keepProfile(text::profileOf(now, remembered->name)))
            profileStatus("Saved " + remembered->name + ".");
        break;
    case Action::saveAs: {
        const std::string suggested = current ? current->name : remembered ? remembered->name : "My settings";
        std::snprintf(profileName_.data(), profileName_.size(), "%s", suggested.c_str());
        wantSaveProfile_ = true;
        break;
    }
    case Action::copy:
        ImGui::SetClipboardText(text::profileMessage(text::profileOf(now, name), kVersion).c_str());
        profileStatus("Copied. Paste it into a Discord message; others take it with Paste a profile.");
        break;
    case Action::exportFile: {
        const std::string file = text::profileFileName(name.empty() ? "My Spidy settings" : name);
        if (const auto path = browseProfileDestination(window_, widen(file) + L".spidy")) {
            const Profile exported = text::profileOf(now, name.empty() ? file : name);
            if (writeTextFile(*path, text::profileText(exported, kVersion)))
                profileStatus("Exported " + narrow(std::filesystem::path(*path).filename().wstring()) + ".");
            else
                profileStatus("Spidy could not write " + narrow(*path) + ".", true);
        }
        break;
    }
    case Action::paste: {
        const char* clipboard = ImGui::GetClipboardText();
        openImport(clipboard ? clipboard : "", "", "The clipboard");
        break;
    }
    case Action::importFile:
        if (const auto path = browseForProfile(window_))
            importFile(*path);
        break;
    case Action::remove:
        if (deleteProfile(chosen->name)) {
            if (settings_.profile == chosen->name) {
                settings_.profile.clear();
                save();
            }
            profileStatus("Deleted " + chosen->name + ": it is in the Recycle Bin.");
        } else {
            profileStatus("Spidy could not delete " + chosen->name + ".", true);
        }
        profilesStale_ = true;
        break;
    case Action::folder: openPath(profilesFolder()); break;
    }
}

void App::profileModals() {
    namespace text = spidy::launcher;
    if (wantSaveProfile_) {
        ImGui::OpenPopup("Save as a profile");
        wantSaveProfile_ = false;
    }
    if (wantImport_) {
        ImGui::OpenPopup("Import a profile");
        wantImport_ = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;
    const auto heading = [&](const char* title, const std::string& message) {
        misprint(ImGui::GetWindowDrawList(), fonts_.display, S(30), ImGui::GetCursorScreenPos(), col(kPaper),
                 upper(title).c_str());
        ImGui::Dummy(ImVec2(0, S(32)));
        ImGui::PushStyleColor(ImGuiCol_Text, vec(kPaperDim));
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, S(6)));
    };
    // The name the profile is saved under (its file's), and whether Enter was pressed in it.
    const auto nameField = [&]() -> std::pair<std::string, bool> {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::PushFont(fonts_.semibold, 15);
        const bool enter =
            ImGui::InputTextWithHint("##name", "Its name", profileName_.data(), profileName_.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopFont();
        const std::string file = text::profileFileName(profileName_.data());
        if (!file.empty() && profileExists(file)) {
            ImGui::PushStyleColor(ImGuiCol_Text, vec(kYellow));
            ImGui::TextWrapped("You have a profile of that name: saving replaces it.");
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0, S(8)));
        return {file, enter};
    };
    const auto primary = [&](const char* label, float width, bool enabled) {
        ImGui::BeginDisabled(!enabled);
        const bool clicked = button(label, width, Ink::primary);
        ImGui::EndDisabled();
        return clicked;
    };

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(480), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(22)));
    if (ImGui::BeginPopupModal("Save as a profile", nullptr, flags)) {
        heading("Save as a profile",
                "These options under a name: the SPIDY VR tab's settings, its own ones too, and wall "
                "running. Your graphics options and body calibration stay out of it.");
        const auto [file, enter] = nameField();
        const float width = (ImGui::GetContentRegionAvail().x - S(12)) * 0.5f;
        const bool saving = primary("Save", width, !file.empty()) || (enter && !file.empty());
        ImGui::SameLine(0, S(12));
        if (saving) {
            if (keepProfile(text::profileOf(playSettings(), file))) {
                settings_.profile = file;
                save();
                profileStatus("Saved " + file + ".");
            }
            ImGui::CloseCurrentPopup();
        } else if (button("Cancel", width) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(580), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(22)));
    if (ImGui::BeginPopupModal("Import a profile", nullptr, flags)) {
        bool done = !importing_;
        if (importing_) {
            const Profile& profile = *importing_;
            heading("Import a profile", profile.madeWith.empty()
                                            ? std::string("A Spidy settings profile. Name it, then save it.")
                                            : "A Spidy settings profile, saved by Spidy " + profile.madeWith +
                                                  ". Name it, then save it.");
            const auto [file, enter] = nameField();
            const auto changes = text::profileChanges(profile, playSettings());
            std::string summary = "Your options already are its settings.";
            if (changes.size() == 1)
                summary = "Using it changes one of your settings:";
            else if (changes.size() > 1)
                summary = format("Using it changes %d of your settings:", static_cast<int>(changes.size()));
            ImGui::PushStyleColor(ImGuiCol_Text, vec(kPaperDim));
            ImGui::TextUnformatted(summary.c_str());
            ImGui::PopStyleColor();
            if (!changes.empty()) {
                const float line = ImGui::GetTextLineHeightWithSpacing();
                const float height = std::min(static_cast<float>(changes.size()), 10.f) * line + S(18);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, vec(kWell));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12), S(9)));
                ImGui::BeginChild("##changes", ImVec2(0, height),
                                  ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
                for (const auto& change : changes) {
                    ImGui::PushFont(fonts_.semibold, 14);
                    ImGui::TextUnformatted(change.setting.c_str());
                    ImGui::PopFont();
                    ImGui::SameLine(S(220));
                    ImGui::PushStyleColor(ImGuiCol_Text, vec(kPaperDim));
                    ImGui::TextUnformatted((change.from + "  \xE2\x86\x92").c_str());
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, vec(kYellow));
                    ImGui::TextUnformatted(change.to.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
            }
            if (!profile.complete()) {
                ImGui::PushStyleColor(ImGuiCol_Text, vec(kPaperDim));
                const int held = static_cast<int>(profile.count());
                const int all = static_cast<int>(profile.values.size());
                ImGui::TextWrapped("It has %d of the %d settings a profile holds; the rest stay as they are.",
                                   held, all);
                ImGui::PopStyleColor();
            }
            ImGui::Dummy(ImVec2(0, S(8)));
            const float width = (ImGui::GetContentRegionAvail().x - S(24)) / 3;
            const bool running = session_.running();
            const bool use = primary("Save and use", width, !file.empty() && !running) ||
                             (enter && !file.empty() && !running);
            if (running && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                tip("Stop VR first: a session keeps the options it started with.");
            ImGui::SameLine(0, S(12));
            ImGui::BeginDisabled(file.empty());
            const bool keep = button("Save only", width);
            ImGui::EndDisabled();
            ImGui::SameLine(0, S(12));
            const bool cancel = button("Cancel", width) || ImGui::IsKeyPressed(ImGuiKey_Escape);
            Profile named = profile;
            named.name = file;
            if (use && keepProfile(named))
                useProfile(named, file);
            else if (keep && keepProfile(named))
                profileStatus("Saved " + file + ": it is in your profiles now.");
            done = use || keep || cancel;
        }
        if (done) {
            importing_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

void App::controls(ImVec2 origin, ImVec2 size) {
    struct Entry {
        const char* control;
        const char* action;
    };
    struct Group {
        const char* title;
        std::vector<Entry> entries;
    };
    const Group groups[] = {
        {"SWINGING",
         {{"Squeeze a grip", "Shoot that hand's web where the controller points; hold to swing"},
          {"Release the grip", "Let go"},
          {"Trigger", "Reel in while the web is attached"},
          {"Pull the hand sharply", "Zip toward the web's anchor"},
          {"Pull the hand in", "The web is a rope in your hand: you go where you pull it"},
          {"Other hand on the web, grip", "It takes hold of the same web. The upper hand carries you: pull with "
                                          "it, or climb hand over hand"},
          {"A corner in the way", "The web bends round it and holds; swing back and it lies straight again"}}},
        {"GRABBING",
         {{"Grip, aimed at a prop or thug", "Catch it with your web"},
          {"Trigger", "Reel it in"},
          {"Pull the hand sharply", "Yank it to your hand"},
          {"Release the grip", "Throw it: the harder you swing, the faster it flies"}}},
        {"FIGHTING",
         {{"Punch a thug", "A fist that meets him fast lands a blow, as hard as it moved. You feel it, hear it and see "
                           "it, and time stops for a moment"},
          {"Punch at a thug across the room", "Web strike: you fly to him and the blow lands as you arrive. Up to "
                                              "10 m, from the ground, a jump or a swing"},
          {"Punch along your web at the thug it holds", "The same: you go to him. A sharp pull still yanks him to you"},
          {"Uppercut", "Lifts him into the air. Strike up after him: you hang beside him while your blows land"},
          {"Punch down at a thug in the air", "Slams him into the ground"},
          {"Blows in a row", "Count up (X2, X3...): each hits harder, and the fifth is a finisher that sends him "
                             "flying, if you throw it the way its arrow points"},
          {"A ring over a thug", "Your next punch goes to him and to nobody else: the one you look at, up to 10 m "
                                 "away. Hit him and the ring locks on (four ticks) until he is down"},
          {"Change target", "Look squarely at another enemy for a third of a second"},
          {"KO!", "A thug your blow knocked out: the ring is free, look at the next one"},
          {"An arrow into the ring", "A called blow, from your second blow on. It comes in from the side your fist "
                                     "comes from: swing a hook from that side, an uppercut from below or a blow "
                                     "down from above, at arm's reach or at the marked thug across the room. It hits "
                                     "harder and throws him that way; the yellow arrow into a jagged red ring calls "
                                     "the finisher"},
          {"Spider-Sense lines over a thug", "He is about to hit you, as the game itself warns you. The ring closes "
                                             "on the blow; the yellow burst is now. Out of view his mark shows near "
                                             "the middle of your view, its arrowhead pointing to him"},
          {"Blue lines: throw a fist at him", "The counter: his attack breaks, he is your target, and you fly to "
                                              "him if he is out of reach. The blow sends him flying"},
          {"Red lines / yellow lines", "Dodge (B) / jump (A): no counter breaks this attack"},
          {"Lines while you swing", "A web carrying you dodges red and blue lines' blows by itself: the game's "
                                    "own dodge on the move, with its Focus"},
          {"Yank a thug while you are in the air", "He flies to you and floats in front of you; time slows as he "
                                                   "arrives and you hang there. Hit him"},
          {"Slow motion by itself", "Time slows for a moment at a finisher, a called blow, a thug who arrives in "
                                    "front of you in the air, and a perfect dodge"}}},
        {"MOVING",
         {{"Left stick", "Walk and run"},
          {"A", "Jump. Hold it on the ground to charge a leap and let go: the longer you hold, the higher; with "
                "the left stick, forward too"},
          {"B", "Dodge, the way the left stick points: the game's B"},
          {"Y", "Interact with what you look at: the game's Y (backpacks, doors, prompts); web strike in a "
                "fight, while you swing too (a car in a chase). Hold it and you step up to the thing, until "
                "the game takes it"},
          {"X where the game shows LB + RB", "The game's LB + RB: throw a prop, pull a door or a grate; hold "
                                             "it as long as the game wants the two held. Its prompt and marker "
                                             "show X. A prop goes where your other hand points"},
          {"Right stick down", "Heal: the game's D-pad down. Push it right down and hold it a moment"},
          {"Right stick up", "The game's LT + RT: zip to a point, enter a vent, where the game shows the "
                             "two; while you swing too. No point? Keep holding (time slows as the game "
                             "aims), look at a point and let go to zip there"},
          {"Hand to your temple, trigger", "Scan your surroundings: the game's R3"},
          {"Both hands at your chest, both triggers", "The game's suit power: its L3 + R3, when its meter is "
                                                      "full"},
          {"Menu button", "Pause; there SWITCH TO GAME SCREEN plays the game as it is, on a 3D screen, and VR "
                          "settings are in Settings > SPIDY VR"},
          {"Both sticks held in", "Pause too: under SteamVR a Quest's menu button opens SteamVR's dashboard"},
          {"Grip behind your hip, bring it up", "Your phone. MENU opens the game menu (map, suits, skills), CAMERA "
                                                "the game's camera, MUSIC your songs. The stick picks, A opens, B "
                                                "puts it away"},
          {"Hold X", "The gadget wheel: move the hand toward a gadget and let go to equip it (the web shooter "
                     "alone until the game gives you one). While the game asks for its X, X is that X"},
          {"A button the game shows", "Press the one of that name: A, B, X and Y as they are, X its LB + RB, "
                                      "your left trigger its LT or LB, your right trigger its RT or RB, the "
                                      "stick clicks its L3 and R3, the right stick pushed its D-pad. While it "
                                      "asks for X, a punch is that X"}}},
        {"MENUS AND CUTSCENES",
         {{"Left stick", "Move through menus (they show on a screen in the headset)"},
          {"A  /  B", "Select  /  Back"},
          {"Grips", "Switch menu tabs"},
          {"Menu button  /  both sticks held in", "Start"}}},
        {"STEAM FRAME CONTROLLERS",
         {{"A, B, X, Y", "All four are on the right controller, each the button of that name above: jump, dodge, "
                         "gadget wheel, interact"},
          {"Menu  /  View", "Pause  /  Game menu, as the phone's MENU"},
          {"D-pad", "The game's own D-pad: down heals. The right stick's down and up work as above"},
          {"Bumpers", "Not used: the triggers and grips are your webs"}}},
    };
    const float gap = S(18);
    float columnY[2] = {origin.y, origin.y};
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginChild("##controlsScroll", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), 0));
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float contentWidth = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - S(6);
    const float columnWidth = (contentWidth - gap) * 0.5f;
    columnY[0] = columnY[1] = base.y;
    // The README's pictures of them first, one under another, each as many pixels wide as it is drawn (never wider
    // than it is), then each control in words.
    for (const Picture& picture : controlsPictures_) {
        if (!picture.texture || picture.size.x <= 0)
            continue;
        const float width = std::floor(std::min(contentWidth, picture.size.x));
        const float height = std::round(width * picture.size.y / picture.size.x);
        if (const ImTextureID texture = picture.texture(static_cast<int>(width)); texture != ImTextureID_Invalid) {
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 min(std::round(base.x), std::round(columnY[0])), max(min.x + width, min.y + height);
            draw->AddImage(ImTextureRef(texture), min, max);
            draw->AddRect(min, max, col(kLine));
            ImGui::SetCursorScreenPos(min);
            ImGui::Dummy(ImVec2(width, height));
            columnY[0] = columnY[1] = max.y + gap;
        }
    }
    for (int g = 0; g < static_cast<int>(std::size(groups)); ++g) {
        const int column = g % 2;
        ImGui::SetCursorScreenPos(ImVec2(base.x + column * (columnWidth + gap), columnY[column]));
        ImGui::PushID(g);
        if (beginCard("##group", ImVec2(columnWidth, 0), ImGuiChildFlags_AutoResizeY)) {
            cardTitle(groups[g].title);
            ImGui::Dummy(ImVec2(0, S(8)));
            auto* draw = ImGui::GetWindowDrawList();
            const float keyWidth = S(180), keySize = S(14.5f), actionSize = S(14.5f);
            for (const auto& entry : groups[g].entries) {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float width = ImGui::GetContentRegionAvail().x;
                // The control as a key cap in capitals, what it does beside it.
                const std::string key = upper(entry.control);
                const ImVec2 keyExtent = measure(fonts_.label, keySize, key.c_str(), keyWidth - S(16));
                const ImVec2 actionExtent = measure(fonts_.body, actionSize, entry.action, width - keyWidth - S(14));
                const float height = std::max(keyExtent.y + S(10), actionExtent.y) + S(12);
                const ImVec2 capEnd(at.x + std::min(keyExtent.x + S(16), keyWidth), at.y + keyExtent.y + S(10));
                draw->AddRectFilled(at, capEnd, col(kSurfaceHigh), S(4));
                draw->AddRect(at, capEnd, col(kLine), S(4), 0, S(1));
                draw->AddText(fonts_.label, keySize, ImVec2(at.x + S(8), at.y + S(5)), col(kPaper), key.c_str(), nullptr,
                              keyWidth - S(16));
                draw->AddText(fonts_.body, actionSize, ImVec2(at.x + keyWidth + S(14), at.y + S(4)), col(kLogText),
                              entry.action, nullptr, width - keyWidth - S(14));
                ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + height));
                ImGui::Dummy(ImVec2(width, 0));
            }
        }
        endCard();
        ImGui::PopID();
        columnY[column] = ImGui::GetItemRectMax().y + gap;
    }
    const float tipsY = std::max(columnY[0], columnY[1]);
    ImGui::SetCursorScreenPos(ImVec2(base.x, tipsY));
    if (beginCard("##tips", ImVec2(contentWidth, 0), ImGuiChildFlags_AutoResizeY)) {
        cardTitle("Good to know");
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::PushStyleColor(ImGuiCol_Text, vec(kLogText));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("Start the game from Spidy, with the game closed: Spidy then gives it the larger render "
                               "memory VR needs. Attached to a game that is already running, VR works but may break up.");
        ImGui::Dummy(ImVec2(0, S(10)));
        ImGui::TextUnformatted("The game pauses while another window is in front of it on the desktop. If the headset "
                               "shows a still picture, click the game window once.");
        ImGui::Dummy(ImVec2(0, S(10)));
        ImGui::TextUnformatted("The keyboard does not move the player once the VR controllers have pressed a button; a "
                               "real gamepad keeps working.");
        ImGui::Dummy(ImVec2(0, S(10)));
        ImGui::TextUnformatted("Aim markers, blue for the left hand and orange for the right: a ring where a web would "
                               "hold, a faint dashed ring where it would hold in open air, a red cross where it would "
                               "miss, and a turning ring of three arcs around a prop or thug it would catch. The marker "
                               "tightens as you squeeze the grip. AIM MARKERS in Settings > SPIDY VR, or in "
                               "the launcher's options, hides them.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, S(4)));
    }
    endCard();
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void App::about(ImVec2 origin, ImVec2 size) {
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginChild("##aboutScroll", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - S(6);
    const float gap = S(18);
    const float rightWidth = std::clamp(width * 0.42f, S(360), S(480));
    const float leftWidth = width - gap - rightWidth;
    ImGui::SetCursorScreenPos(base);
    if (beginCard("##about", ImVec2(leftWidth, 0), ImGuiChildFlags_AutoResizeY)) {
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        badge(draw, ImVec2(at.x + S(34), at.y + S(36)), S(34));
        wordmark(draw, ImVec2(at.x + S(86), at.y - S(2)), S(52));
        const float kicker = S(13.5f);
        spacedText(draw, fonts_.kicker, kicker, ImVec2(at.x + S(88), at.y + S(52)), col(kPaperDim), "CREATED BY",
                   0.14f);
        spacedText(draw, fonts_.kicker, kicker,
                   ImVec2(at.x + S(88) + spacedWidth(fonts_.kicker, kicker, "CREATED BY ", 0.14f), at.y + S(52)),
                   col(kPaper), upper(kAuthor).c_str(), 0.14f);
        ImGui::Dummy(ImVec2(0, S(78)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(10)));
        ImGui::PushStyleColor(ImGuiCol_Text, vec(kLogText));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("Spidy puts you inside Marvel's Spider-Man Remastered: your head is the camera, each hand "
                               "shoots its own web, and you swing, zip and throw with your own arms.");
        ImGui::TextUnformatted("Spidy changes nothing in the game's folder. It attaches to the game while it runs and "
                               "puts back what it changed when VR stops.");
        ImGui::TextUnformatted("Sharing Spidy? Share the original zip with this credit intact, so players get "
                               "working files and know where to find updates.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, S(2)));
        ImGui::PushStyleColor(ImGuiCol_Text, vec(kFaint));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("A free fan project, not affiliated with or endorsed by Insomniac Games, Sony Interactive "
                               "Entertainment, Marvel or Valve. You need your own copy of the game on Steam.");
        ImGui::Text("Spidy %s. Built with the OpenXR loader (Apache 2.0), MinHook (BSD), Dear ImGui (MIT), Python (PSF) "
                    "and the Barlow and Comic Neue fonts (SIL OFL).",
                    kVersion);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (link("Third-party notices") && !scan_.root.empty())
            openPath((std::filesystem::path(scan_.root) / L"THIRD_PARTY_NOTICES.md").wstring());
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, S(2)));
    }
    endCard();
    ImGui::SetCursorScreenPos(ImVec2(base.x, ImGui::GetItemRectMax().y + gap));
    if (beginCard("##pc", ImVec2(leftWidth, 0), ImGuiChildFlags_AutoResizeY)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8), S(8)));
        cardTitle("On this PC");
        const float buttonWidth = ImGui::GetContentRegionAvail().x;
        if (button("Add desktop and Start menu shortcuts", buttonWidth, Ink::plain, kShortcut)) {
            std::string error;
            shortcutMessage_ = createShortcuts(error) ? "Shortcuts added: look for Spidy VR." : error;
            settings_.shortcutsAsked = true;
            save();
        }
        if (button("Open the reports folder", buttonWidth, Ink::plain, kFolder) && !scan_.root.empty())
            openPath((std::filesystem::path(scan_.root) / L"reports").wstring());
        if (button("Open the Spidy folder", buttonWidth, Ink::plain, kFolderOpen) && !scan_.root.empty())
            openPath(scan_.root);
        ImGui::PopStyleVar();
        if (!shortcutMessage_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, vec(kPaperDim));
            ImGui::TextWrapped("%s", shortcutMessage_.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0, S(2)));
    }
    endCard();
    ImGui::SetCursorScreenPos(ImVec2(base.x, ImGui::GetItemRectMax().y + gap));
    updatesCard(leftWidth);
    // Right: Ko-fi and the Discord, as the README has them.
    ImGui::SetCursorScreenPos(ImVec2(base.x + leftWidth + gap, base.y));
    ImGui::BeginGroup();
    supportCards(rightWidth);
    ImGui::EndGroup();
    ImGui::EndChild();
}

void App::supportCards(float width) {
    struct Card {
        const char* id;
        Rgb accent, dots;
        const char* kicker;
        const char* lines[2];
        Caption paper;
        const char* words;
        const char* loud;
        const char* address;
        const char* url;
        bool tipJar; // keylined in its yellow, its address a button with a heart
    };
    // The README's two cards, dark as the other cards; the tip jar in yellow, as in the header (the two reasons
    // for a tip are Ilya's).
    const Card cards[] = {
        {"##kofiCard", kYellow, kWarmDots, "Spidy is free. The AI tokens are not.", {"Buy me", "a coffee"},
         Caption::narrator, "Your tip pays for updates.", "And the next full VR conversion.",
         "ko-fi.com/ilyamezerowsky", kKofiUrl, true},
        {"##discordCard", kAccentHover, kDotsStrong, "Where the players are", {"Join the", "Discord"},
         Caption::narrator, "Release news, help and clips.", "Come say hi.", "discord.gg/x27ZXDpdNt", kDiscordUrl,
         false},
    };
    for (const auto& card : cards) {
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float pad = S(22), kickerSize = S(13.5f), headSize = S(42), addressSize = S(16);
        const float captionWidth = width - pad * 2;
        // Measured first, drawn after: the card is as tall as what it holds.
        const float captionHeight = captionBox(nullptr, at, captionWidth, card.words, card.paper, card.loud);
        const float height = pad + kickerSize + S(12) + headSize * 1.7f + S(14) + captionHeight + S(18) +
                             addressSize + S(16) + pad;
        const ImVec2 end(at.x + width, at.y + height);
        const bool clicked = ImGui::InvisibleButton(card.id, ImVec2(width, height));
        handCursor();
        const bool hovered = ImGui::IsItemHovered();
        draw->AddRectFilled(at, end, col(hovered ? kSurfaceHigh : kSurface));
        const float reach = width * 0.7f;
        halftone(draw, ImVec2(end.x - reach, at.y + S(2)), ImVec2(end.x - S(2), end.y - S(2)), S(10), col(card.dots),
                 [&](ImVec2 p) { return 0.9f - std::hypot(end.x - p.x, end.y - p.y) / reach; });
        if (card.tipJar)
            draw->AddRect(at, end, col(kYellow), 0, 0, S(1.5f));
        else
            draw->AddRect(at, end, col(hovered ? kFaint : kLine));
        float y = at.y + pad;
        // The kicker after a slash in the card's colour, the headline's second line in it.
        slanted(draw, ImVec2(at.x + pad, y + kickerSize * 0.18f), ImVec2(at.x + pad + S(4), y + kickerSize * 0.86f),
                col(card.accent));
        spacedText(draw, fonts_.kicker, kickerSize, ImVec2(at.x + pad + S(14), y), col(kPaperDim),
                   upper(card.kicker).c_str(), 0.12f);
        y += kickerSize + S(12);
        // The misprint on the white line only: on a coloured one its ghosts shimmer.
        misprint(draw, fonts_.display, headSize, ImVec2(at.x + pad, y), col(kPaper), upper(card.lines[0]).c_str());
        draw->AddText(fonts_.display, headSize, ImVec2(at.x + pad, y + headSize * 0.82f), col(card.accent),
                      upper(card.lines[1]).c_str());
        y += headSize * 1.7f + S(14);
        captionBox(draw, ImVec2(at.x + pad, y), captionWidth, card.words, card.paper, card.loud);
        y += captionHeight + S(18);
        // Its address as a button, at the right.
        const char* heart = card.tipJar && fonts_.icons ? kHeart : nullptr;
        const float heartWidth = heart ? measure(fonts_.label, addressSize, heart).x + S(8) : 0;
        const float addressWidth = measure(fonts_.label, addressSize, card.address).x + heartWidth + S(32);
        const ImVec2 pill(end.x - pad - addressWidth, y), pillEnd(end.x - pad, y + addressSize + S(16));
        const ImU32 lettering = col(card.tipJar ? kYellow : kPaper);
        draw->AddRectFilled(pill, pillEnd, col(hovered ? kSurfaceHover : kSurfaceHigh), S(4));
        draw->AddRect(pill, pillEnd, card.tipJar ? col(kYellow) : col(hovered ? kFaint : kLine), S(4), 0,
                      S(card.tipJar ? 1.5f : 1));
        if (heart)
            draw->AddText(fonts_.label, addressSize, ImVec2(pill.x + S(16), pill.y + S(7)), lettering, heart);
        draw->AddText(fonts_.label, addressSize, ImVec2(pill.x + S(16) + heartWidth, pill.y + S(8)), lettering,
                      card.address);
        if (clicked)
            openUrl(card.url);
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + height + S(18)));
    }
    ImGui::Dummy(ImVec2(width, 0));
}

void App::updatesCard(float width) {
    if (!beginCard("##updates", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY))
        return endCard();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(8)));
    cardTitle("Updates");
    ImGui::PopStyleVar();
    const auto& release = update_.release;
    const bool available = release && (update_.state == UpdateState::available ||
                                       update_.state == UpdateState::installFailed);
    std::string status;
    switch (update_.state) {
    case UpdateState::idle:
        status = installFolder_.empty() ? "This launcher runs from a Spidy checkout, which git updates."
                                        : "Not checked yet.";
        break;
    case UpdateState::checking: status = "Asking GitHub for the newest Spidy..."; break;
    case UpdateState::current:
        status = format("Spidy %s is the newest version (checked at %s).", kVersion, update_.checkedAt.c_str());
        break;
    case UpdateState::available: status = format("Spidy %s is out: you have %s.", release->version.c_str(), kVersion); break;
    case UpdateState::checkFailed: status = "The check failed. " + update_.message; break;
    case UpdateState::downloading:
    case UpdateState::installing:
    case UpdateState::installed: status = format("Updating to Spidy %s.", release->version.c_str()); break;
    case UpdateState::installFailed: status = "The update failed. " + update_.message; break;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, vec(kLogText));
    ImGui::TextWrapped("%s", status.c_str());
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), 0));
    option("Check when Spidy starts", "Asks GitHub for a newer Spidy. Updating waits for your click.", S(42), [&] {
        if (toggle("##updateCheck", &settings_.updateCheck))
            save();
    });
    ImGui::PopStyleVar();
    const float half = (ImGui::GetContentRegionAvail().x - S(10)) * 0.5f;
    if (available && !installFolder_.empty()) {
        // The Play tab's banner shows how it goes.
        const std::string blocker = updateBlocker();
        ImGui::BeginDisabled(!blocker.empty());
        if (button("Update now", half, Ink::primary)) {
            updateLater_ = false;
            tab_ = Tab::play;
            updater_.install();
        }
        ImGui::EndDisabled();
        if (!blocker.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            tip(blocker.c_str());
    } else {
        ImGui::BeginDisabled(updater_.busy() || update_.state == UpdateState::installed);
        if (button("Check now", half, Ink::plain, kRefresh)) {
            updateLater_ = false;
            updater_.check();
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine(0, S(10));
    if (button("All releases", half, Ink::plain, kOpen))
        openUrl(kReleasesUrl);
    ImGui::Dummy(ImVec2(0, S(4)));
    endCard();
}

void App::modals() {
    if (wantClose_) {
        ImGui::OpenPopup("VR is running");
        wantClose_ = false;
    }
    if (wantAdministrator_) {
        ImGui::OpenPopup("Administrator rights needed");
        wantAdministrator_ = false;
    }
    if (wantCloseGame_) {
        ImGui::OpenPopup("Close the game?");
        wantCloseGame_ = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const auto dialog = [&](const char* name, const std::string& message, const char* yes, const char* no) -> int {
        int answer = -1;
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(S(480), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(22)));
        if (ImGui::BeginPopupModal(name, nullptr,
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
            misprint(ImGui::GetWindowDrawList(), fonts_.display, S(30), ImGui::GetCursorScreenPos(), col(kPaper),
                     upper(name).c_str());
            ImGui::Dummy(ImVec2(0, S(32)));
            ImGui::PushStyleColor(ImGuiCol_Text, vec(kPaperDim));
            ImGui::TextWrapped("%s", message.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, S(10)));
            const float width = (ImGui::GetContentRegionAvail().x - S(12)) * 0.5f;
            if (button(yes, width, Ink::primary))
                answer = 1;
            ImGui::SameLine(0, S(12));
            if (button(no, width) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                answer = 0;
            if (answer >= 0)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        return answer;
    };
    if (dialog("VR is running", "Closing Spidy stops VR first: it restores the game and saves the session report. The "
                                "game keeps running.",
               "Stop VR and close", "Keep playing") == 1) {
        session_.requestStop();
        closeAfterStop_ = true;
    }
    if (ImGui::IsPopupOpen("Administrator rights needed")) {
        // The session's own words for what runs as administrator (Steam, or a game that is already running).
        std::string reason;
        for (auto it = log_.rbegin(); it != log_.rend() && reason.empty(); ++it)
            if (it->text.rfind("Spidy VR unavailable: ", 0) == 0)
                reason = it->text.substr(22) + "\n\n";
        if (dialog("Administrator rights needed",
                   reason + "Spidy can start again as administrator now; Windows asks you to allow it.",
                   "Restart as administrator", "Not now") == 1)
            restartElevated();
    }
    if (ImGui::IsPopupOpen("Close the game?")) {
        // The session's own words: VR already ran in that game, or another Spidy build is in it.
        std::string reason;
        for (auto it = log_.rbegin(); it != log_.rend() && reason.empty(); ++it)
            if (it->text.rfind("Spidy VR unavailable: ", 0) == 0)
                reason = it->text.substr(22) + "\n\n";
        if (dialog("Close the game?",
                   reason + "Spidy can close it now, as its close button does (the game saves as it quits), "
                            "then start the game and VR again.",
                   "Close and start VR", "Not now") == 1)
            start(true);
    }
    profileModals();
}

} // namespace launcher
