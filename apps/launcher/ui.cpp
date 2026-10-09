#include "ui.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <filesystem>

namespace launcher {
namespace {

// Segoe MDL2 Assets / Segoe Fluent Icons glyphs (UTF-8; the sources build with /utf-8).
constexpr const char* kHeart = "";
constexpr const char* kPlay = "";
constexpr const char* kStop = "";
constexpr const char* kFolder = "";
constexpr const char* kRefresh = "";
constexpr const char* kCopy = "";
constexpr const char* kOpen = "";
constexpr const char* kDownload = "";

struct Rgb {
    int r, g, b;
};
constexpr Rgb kBackground{13, 15, 20}, kPanel{21, 25, 34}, kPanelHigh{30, 35, 48}, kBorder{38, 44, 59},
    kText{233, 236, 242}, kMuted{139, 147, 167}, kFaint{90, 98, 117}, kRed{227, 38, 47}, kRedHover{242, 59, 68},
    kBlue{59, 130, 246}, kGreen{52, 211, 153}, kAmber{251, 191, 36}, kError{248, 113, 113}, kKofi{255, 94, 91},
    kKofiHover{255, 120, 116}, kWhite{255, 255, 255};

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

// The steps the game's Settings offer too (SPIDY VR, vr_settings.hpp).
constexpr const char* kSnapTurns[] = {"Off", "15\xC2\xB0", "30\xC2\xB0", "45\xC2\xB0", "60\xC2\xB0", "90\xC2\xB0"};
constexpr int kSnapValues[] = {0, 15, 30, 45, 60, 90};
constexpr const char* kSmoothTurns[] = {"Off",          "60\xC2\xB0/s",  "90\xC2\xB0/s",
                                        "120\xC2\xB0/s", "180\xC2\xB0/s", "240\xC2\xB0/s"};
constexpr int kSmoothValues[] = {0, 60, 90, 120, 180, 240};
constexpr const char* kHaptics[] = {"Off", "25%", "50%", "75%", "100%"};
constexpr int kHapticValues[] = {0, 25, 50, 75, 100};
constexpr const char* kScreenSizes[] = {"Small", "Medium", "Large"};
// The button that shoots and holds webs (SessionOptions::triggerWebs: the second).
constexpr const char* kWebButtons[] = {"Grip", "Trigger"};
constexpr const char* kWeights[] = {"40%", "60%", "80%", "100%", "125%", "150%", "200%", "250%", "300%"};
constexpr int kWeightValues[] = {40, 60, 80, 100, 125, 150, 200, 250, 300};

} // namespace

void theme(ImGuiStyle& style) {
    style.WindowPadding = ImVec2(16, 16);
    style.FramePadding = ImVec2(10, 7);
    style.ItemSpacing = ImVec2(10, 10);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.WindowRounding = 0;
    style.ChildRounding = 12;
    style.FrameRounding = 8;
    style.PopupRounding = 10;
    style.GrabRounding = 8;
    style.ScrollbarRounding = 8;
    style.ScrollbarSize = 10;
    style.GrabMinSize = 14;
    style.WindowBorderSize = 0;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;
    style.DisabledAlpha = 0.45f;
    style.FontSizeBase = 15;
    auto* c = style.Colors;
    c[ImGuiCol_Text] = vec(kText);
    c[ImGuiCol_TextDisabled] = vec(kFaint);
    c[ImGuiCol_WindowBg] = vec(kBackground);
    c[ImGuiCol_ChildBg] = vec(kPanel);
    c[ImGuiCol_PopupBg] = vec(kPanel);
    c[ImGuiCol_Border] = vec(kBorder);
    c[ImGuiCol_FrameBg] = vec(kPanelHigh);
    c[ImGuiCol_FrameBgHovered] = vec({40, 46, 62});
    c[ImGuiCol_FrameBgActive] = vec({46, 53, 71});
    c[ImGuiCol_Button] = vec(kPanelHigh);
    c[ImGuiCol_ButtonHovered] = vec({42, 48, 65});
    c[ImGuiCol_ButtonActive] = vec({50, 57, 77});
    c[ImGuiCol_Header] = vec({42, 48, 65});
    c[ImGuiCol_HeaderHovered] = vec({48, 55, 74});
    c[ImGuiCol_HeaderActive] = vec({55, 63, 84});
    c[ImGuiCol_SliderGrab] = vec(kBlue);
    c[ImGuiCol_SliderGrabActive] = vec({96, 155, 255});
    c[ImGuiCol_CheckMark] = vec(kBlue);
    c[ImGuiCol_ScrollbarBg] = vec(kPanel, 0);
    c[ImGuiCol_ScrollbarGrab] = vec({50, 57, 75});
    c[ImGuiCol_ScrollbarGrabHovered] = vec({62, 70, 92});
    c[ImGuiCol_ScrollbarGrabActive] = vec({74, 84, 110});
    c[ImGuiCol_Separator] = vec(kBorder);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.04f, 0.72f);
    c[ImGuiCol_NavCursor] = vec(kBlue);
}

App::App(HWND window, Fonts fonts, std::string updatedFrom)
    : window_(window), fonts_(fonts), settings_(loadSettings()), installFolder_(installFolder()),
      updatedFrom_(std::move(updatedFrom)) {
    scanner_.start(settings_);
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
    const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0;
    // Memory and the game process change while the launcher is open.
    if (scan_.done && latest.done && now - lastRefresh_ > 2) {
        MEMORYSTATUSEX memory{sizeof(memory)};
        if (GlobalMemoryStatusEx(&memory))
            scan_.freeCommitGb = static_cast<double>(memory.ullAvailPageFile) / (1ull << 30);
        lastRefresh_ = now;
    }
    const double freshCommit = scan_.freeCommitGb;
    scan_ = latest;
    if (scan_.done && freshCommit > 0 && !firstResult)
        scan_.freeCommitGb = freshCommit;
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

double App::neededCommitGb() {
    return spidy::launcher::vrCommitGb(scan_.neededCommitGb * 1024, scan_.eyeCommitBytes, eyeSize());
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
    else if (scan_.build != Build::supported)
        reasons.push_back("This game version is not supported.");
    if (scan_.runtimes.empty())
        reasons.push_back("No VR runtime is installed.");
    if (!scan_.vcCurrent)
        reasons.push_back("The Visual C++ runtime is needed.");
    return reasons;
}

void App::start(bool memoryConfirmed) {
    startError_.clear();
    if (!memoryConfirmed && scan_.freeCommitGb > 0 && scan_.freeCommitGb < neededCommitGb()) {
        wantLowMemory_ = true;
        return;
    }
    const auto report = newReportPath(scan_.root);
    const auto* chosen = runtime();
    const auto args = spidy::launcher::sessionArguments(settings_.options, report, chosen ? chosen->manifest : L"",
                                                        newStopEventName());
    log_.clear();
    logCount_ = 0;
    if (!session_.start(scan_.python, scan_.root, args, report, startError_))
        return;
    save();
}

// --- Widgets -----------------------------------------------------------------

bool App::beginCard(const char* id, ImVec2 size, int childFlags) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vec(kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, vec(kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(14)));
    const bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | childFlags, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return open;
}

void App::endCard() { ImGui::EndChild(); }

void App::cardTitle(const char* title, float rightWidth) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    float x = at.x;
    const float size = S(12);
    for (const char* c = title; *c; ++c) {
        draw->AddText(fonts_.bold, size, ImVec2(x, at.y + S(4)), col(kMuted), c, c + 1);
        x += measure(fonts_.bold, size, std::string(1, *c).c_str()).x + S(1.4f);
    }
    // The caller may put buttons on the right of the title line.
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - rightWidth, S(22)));
    if (rightWidth > 0)
        ImGui::SameLine(0, 0);
}

void App::statusMark(ImDrawList* draw, ImVec2 c, Mark mark) {
    const float r = S(10.5f);
    const float t = static_cast<float>(ImGui::GetTime());
    switch (mark) {
    case Mark::ok:
        draw->AddCircleFilled(c, r, col(kGreen, 0.16f));
        draw->PathLineTo(ImVec2(c.x - r * 0.42f, c.y + r * 0.02f));
        draw->PathLineTo(ImVec2(c.x - r * 0.1f, c.y + r * 0.34f));
        draw->PathLineTo(ImVec2(c.x + r * 0.46f, c.y - r * 0.32f));
        draw->PathStroke(col(kGreen), 0, S(2.1f));
        break;
    case Mark::error:
        draw->AddCircleFilled(c, r, col(kError, 0.16f));
        draw->AddLine(ImVec2(c.x - r * 0.33f, c.y - r * 0.33f), ImVec2(c.x + r * 0.33f, c.y + r * 0.33f), col(kError), S(2.1f));
        draw->AddLine(ImVec2(c.x + r * 0.33f, c.y - r * 0.33f), ImVec2(c.x - r * 0.33f, c.y + r * 0.33f), col(kError), S(2.1f));
        break;
    case Mark::warning:
        draw->AddCircleFilled(c, r, col(kAmber, 0.16f));
        draw->AddLine(ImVec2(c.x, c.y - r * 0.45f), ImVec2(c.x, c.y + r * 0.12f), col(kAmber), S(2.1f));
        draw->AddCircleFilled(ImVec2(c.x, c.y + r * 0.42f), S(1.4f), col(kAmber));
        break;
    case Mark::info:
        draw->AddCircleFilled(c, r, col(kMuted, 0.14f));
        draw->AddCircleFilled(ImVec2(c.x, c.y - r * 0.4f), S(1.4f), col(kMuted));
        draw->AddLine(ImVec2(c.x, c.y - r * 0.08f), ImVec2(c.x, c.y + r * 0.45f), col(kMuted), S(2.1f));
        break;
    case Mark::busy:
        draw->AddCircle(c, r * 0.7f, col(kBlue, 0.2f), 0, S(2.2f));
        draw->PathArcTo(c, r * 0.7f, t * 6.f, t * 6.f + 1.9f, 16);
        draw->PathStroke(col(kBlue), 0, S(2.2f));
        break;
    }
}

bool App::pillButton(const char* id, const char* icon, const char* label, ImU32 fill, ImU32 hover, ImU32 text,
                     ImVec2 size, ImFont* font, float fontSize) {
    font = font ? font : fonts_.semibold;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    handCursor();
    auto* draw = ImGui::GetWindowDrawList();
    const bool hovered = ImGui::IsItemHovered();
    draw->AddRectFilled(at, add(at, size), hovered ? hover : fill, size.y * 0.5f);
    const std::string content = fonts_.icons && icon ? std::string(icon) + "  " + label : std::string(label);
    const ImVec2 textSize = measure(font, S(fontSize), content.c_str());
    draw->AddText(font, S(fontSize), ImVec2(at.x + (size.x - textSize.x) * 0.5f, at.y + (size.y - textSize.y) * 0.5f),
                  text, content.c_str());
    return clicked;
}

bool App::bigButton(const char* id, const char* icon, const char* label, ImVec2 size, bool primary, bool enabled) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImGui::EndDisabled();
    if (enabled)
        handCursor();
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end = add(at, size);
    const float rounding = S(14);
    if (!enabled) {
        draw->AddRectFilled(at, end, col(kPanelHigh), rounding);
    } else if (primary) {
        if (hovered)
            draw->AddRectFilled(ImVec2(at.x - S(3), at.y - S(3)), ImVec2(end.x + S(3), end.y + S(3)), col(kRed, 0.22f),
                                rounding + S(3));
        draw->AddRectFilled(at, end, col(held ? Rgb{196, 28, 36} : hovered ? kRedHover : kRed), rounding);
        // A soft light along the top edge.
        draw->AddLine(ImVec2(at.x + rounding, at.y + S(1)), ImVec2(end.x - rounding, at.y + S(1)), col(kWhite, 0.28f), S(1));
    } else {
        draw->AddRectFilled(at, end, col(hovered ? Rgb{44, 24, 30} : Rgb{32, 20, 26}), rounding);
        draw->AddRect(at, end, col(kRed, hovered ? 1.f : 0.7f), rounding, 0, S(1.5f));
    }
    const ImU32 text = enabled ? col(kWhite) : col(kMuted);
    const std::string content = fonts_.icons && icon ? std::string(icon) + "   " + label : std::string(label);
    const ImVec2 textSize = measure(fonts_.bold, S(18), content.c_str());
    draw->AddText(fonts_.bold, S(18), ImVec2(at.x + (size.x - textSize.x) * 0.5f, at.y + (size.y - textSize.y) * 0.5f),
                  text, content.c_str());
    return clicked;
}

bool App::secondaryButton(const char* label, float width) {
    ImGui::PushFont(fonts_.semibold, 13.5f);
    const bool clicked = ImGui::Button(label, ImVec2(width, 0));
    ImGui::PopFont();
    handCursor();
    return clicked;
}

bool App::toggle(const char* id, bool* value) {
    const float h = S(22), w = S(40);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    if (clicked)
        *value = !*value;
    handCursor();
    const bool hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + w, at.y + h),
                        *value ? col(kBlue) : col(hovered ? Rgb{58, 66, 86} : Rgb{44, 50, 66}), h * 0.5f);
    const float knob = *value ? at.x + w - h * 0.5f : at.x + h * 0.5f;
    draw->AddCircleFilled(ImVec2(knob, at.y + h * 0.5f), h * 0.5f - S(3), col(kWhite), 24);
    return clicked;
}

bool App::link(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, vec({125, 168, 255}));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    const bool clicked = ImGui::IsItemClicked();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, col({125, 168, 255}), 1);
    }
    return clicked;
}

// Spidy's mark: a red disc with a white web (the window icon is the same picture).
void App::badge(ImDrawList* draw, ImVec2 c, float r) {
    draw->AddCircleFilled(ImVec2(c.x, c.y + S(2)), r + S(2), IM_COL32(0, 0, 0, 90), 48);
    draw->AddCircleFilled(c, r, col(kRed), 48);
    draw->AddCircle(c, r - S(0.5f), col({255, 120, 120}, 0.35f), 48, S(1));
    const ImU32 line = col(kWhite, 0.92f);
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

// A faint web spun from a corner of the header.
void App::web(ImDrawList* draw, ImVec2 corner, float radius) {
    const ImU32 line = col(kWhite, 0.055f);
    constexpr int spokes = 7;
    for (int i = 0; i < spokes; ++i) {
        const float a = 1.5707963f * i / (spokes - 1);
        draw->AddLine(corner, ImVec2(corner.x + std::cos(a) * radius, corner.y + std::sin(a) * radius), line, S(1));
    }
    for (int ring = 1; ring <= 6; ++ring) {
        const float r = radius * ring / 6.5f;
        for (int i = 0; i + 1 < spokes; ++i) {
            const float a0 = 1.5707963f * i / (spokes - 1), a1 = 1.5707963f * (i + 1) / (spokes - 1);
            const float mid = (a0 + a1) * 0.5f;
            draw->AddBezierQuadratic(ImVec2(corner.x + std::cos(a0) * r, corner.y + std::sin(a0) * r),
                                     ImVec2(corner.x + std::cos(mid) * r * 0.9f, corner.y + std::sin(mid) * r * 0.9f),
                                     ImVec2(corner.x + std::cos(a1) * r, corner.y + std::sin(a1) * r), line, S(1), 10);
        }
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
    const ImVec2 origin(viewport->Pos.x + S(24), viewport->Pos.y + S(96 + 46 + 18));
    const ImVec2 size(viewport->Size.x - S(48), viewport->Size.y - S(96 + 46 + 18 + 22));
    if (size.x > 0 && size.y > 0) {
        switch (tab_) {
        case Tab::play: play(origin, size); break;
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
    const float width = ImGui::GetWindowWidth(), height = S(96);
    draw->AddRectFilledMultiColor(at, ImVec2(at.x + width, at.y + height), col({46, 13, 21}), col(kBackground),
                                  col(kBackground), col({30, 11, 17}));
    web(draw, at, S(260));
    const ImVec2 center(at.x + S(24 + 27), at.y + height * 0.5f);
    badge(draw, center, S(27));
    const float x = center.x + S(27 + 16);
    draw->AddText(fonts_.title, S(34), ImVec2(x, at.y + S(13)), col(kText), "SPIDY");
    const float titleWidth = measure(fonts_.title, S(34), "SPIDY").x;
    const ImVec2 tag(x + titleWidth + S(10), at.y + S(26));
    const ImVec2 tagSize = measure(fonts_.bold, S(11.5f), "VR");
    draw->AddRect(tag, ImVec2(tag.x + tagSize.x + S(14), tag.y + S(20)), col(kRed), S(6), 0, S(1.5f));
    draw->AddText(fonts_.bold, S(11.5f), ImVec2(tag.x + S(7), tag.y + (S(20) - tagSize.y) * 0.5f), col({255, 120, 120}), "VR");
    const char* subtitle = "VR mod for Marvel's Spider-Man Remastered";
    const float lineY = at.y + S(58);
    draw->AddText(fonts_.body, S(14.5f), ImVec2(x, lineY), col(kMuted), subtitle);
    float bylineX = x + measure(fonts_.body, S(14.5f), subtitle).x;
    draw->AddText(fonts_.body, S(14.5f), ImVec2(bylineX, lineY), col(kFaint), "   \xC2\xB7   by ");
    bylineX += measure(fonts_.body, S(14.5f), "   \xC2\xB7   by ").x;
    draw->AddText(fonts_.semibold, S(14.5f), ImVec2(bylineX, lineY), col(kText), kAuthor);

    const ImVec2 buttonSize(S(196), S(40));
    ImGui::SetCursorScreenPos(ImVec2(at.x + width - S(24) - buttonSize.x, at.y + (height - buttonSize.y) * 0.5f));
    if (pillButton("##kofi", kHeart, "Support on Ko-fi", col(kKofi), col(kKofiHover), col(kWhite), buttonSize))
        openUrl(kKofiUrl);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Spidy is free. If you enjoy it, a tip helps pay for its development.");
}

void App::tabs() {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 window = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth(), y = window.y + S(96), height = S(46);
    draw->AddLine(ImVec2(window.x, y + height), ImVec2(window.x + width, y + height), col(kBorder), 1);
    float x = window.x + S(16);
    const std::pair<Tab, const char*> items[] = {{Tab::play, "Play"}, {Tab::controls, "Controls"}, {Tab::about, "About"}};
    for (const auto& [tab, label] : items) {
        const ImVec2 size = measure(fonts_.semibold, S(15), label);
        const float itemWidth = size.x + S(24);
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (ImGui::InvisibleButton(label, ImVec2(itemWidth, height)))
            tab_ = tab;
        handCursor();
        const bool active = tab_ == tab, hovered = ImGui::IsItemHovered();
        draw->AddText(fonts_.semibold, S(15), ImVec2(x + S(12), y + (height - size.y) * 0.5f),
                      col(active || hovered ? kText : kMuted), label);
        if (active)
            draw->AddRectFilled(ImVec2(x + S(10), y + height - S(3)), ImVec2(x + itemWidth - S(10), y + height), col(kRed),
                                S(2));
        x += itemWidth + S(4);
    }
    // Right side: whether VR runs now, and the version.
    const std::string version = format("v%s", kVersion);
    const ImVec2 versionSize = measure(fonts_.body, S(13), version.c_str());
    float right = window.x + width - S(24) - versionSize.x;
    draw->AddText(fonts_.body, S(13), ImVec2(right, y + (height - versionSize.y) * 0.5f), col(kFaint), version.c_str());
    if (session_.running()) {
        const char* label = session_.stopRequested() ? "Stopping VR" : "VR running";
        const ImVec2 labelSize = measure(fonts_.semibold, S(12.5f), label);
        const float pillWidth = labelSize.x + S(30);
        right -= pillWidth + S(14);
        const ImVec2 pill(right, y + (height - S(24)) * 0.5f);
        const Rgb tint = session_.stopRequested() ? kAmber : kGreen;
        draw->AddRectFilled(pill, ImVec2(pill.x + pillWidth, pill.y + S(24)), col(tint, 0.14f), S(12));
        const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.f);
        draw->AddCircleFilled(ImVec2(pill.x + S(13), pill.y + S(12)), S(3.5f), col(tint, pulse));
        draw->AddText(fonts_.semibold, S(12.5f), ImVec2(pill.x + S(22), pill.y + (S(24) - labelSize.y) * 0.5f), col(tint), label);
    }
}

void App::play(ImVec2 origin, ImVec2 size) {
    float top = origin.y;
    ImGui::SetCursorScreenPos(origin);
    if (const float height = updateBanner(size.x); height > 0) {
        top += height + S(14);
    } else if (!settings_.shortcutsAsked && scan_.done) {
        ImGui::SetCursorScreenPos(origin);
        shortcutBanner(size.x);
        top += S(48) + S(14);
    }
    const float gap = S(16);
    const float rightWidth = std::clamp(size.x * 0.38f, S(350), S(440));
    const float leftWidth = size.x - rightWidth - gap;
    const float bottom = origin.y + size.y;
    // Left: what this PC has, then the session's messages.
    ImGui::SetCursorScreenPos(ImVec2(origin.x, top));
    ImGui::BeginGroup();
    setupCard(leftWidth);
    ImGui::EndGroup();
    const float logTop = ImGui::GetItemRectMax().y + gap;
    if (bottom - logTop > S(80)) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, logTop));
        logCard(ImVec2(leftWidth, bottom - logTop));
    }
    // Right: options, then the start button.
    const float startHeight = S(136);
    const float rightX = origin.x + leftWidth + gap;
    ImGui::SetCursorScreenPos(ImVec2(rightX, top));
    optionsCard(ImVec2(rightWidth, bottom - top - startHeight - gap));
    ImGui::SetCursorScreenPos(ImVec2(rightX, bottom - startHeight));
    startBlock(ImVec2(rightWidth, startHeight));
}

void App::shortcutBanner(float width) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = S(48);
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), col({24, 36, 64}), S(12));
    draw->AddRect(at, ImVec2(at.x + width, at.y + height), col(kBlue, 0.35f), S(12));
    const char* message = shortcutMessage_.empty() ? "Install Spidy on this PC: add Spidy VR to your desktop and Start menu."
                                                   : shortcutMessage_.c_str();
    const ImVec2 textSize = measure(fonts_.semibold, S(14.5f), message);
    draw->AddText(fonts_.semibold, S(14.5f), ImVec2(at.x + S(18), at.y + (height - textSize.y) * 0.5f), col(kText), message);
    const float buttonWidth = S(130);
    ImGui::SetCursorScreenPos(ImVec2(at.x + width - S(12) - buttonWidth * 2 - S(8), at.y + (height - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, vec(kBlue));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec({96, 155, 255}));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec({45, 110, 220}));
    if (secondaryButton("Add shortcuts", buttonWidth)) {
        std::string error;
        if (createShortcuts(error)) {
            settings_.shortcutsAsked = true;
            save();
        } else {
            shortcutMessage_ = error;
        }
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, S(8));
    if (secondaryButton("Not now", buttonWidth)) {
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

    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float buttonWidth = S(112), gap = S(8);
    const float buttonsWidth = buttons.empty() ? 0 : static_cast<float>(buttons.size()) * (buttonWidth + gap) - gap;
    const float textWidth = width - S(36) - (buttons.empty() ? 0 : buttonsWidth + S(4));
    const ImVec2 textSize = measure(fonts_.semibold, S(14.5f), message.c_str(), textWidth);
    const float height = std::max(S(48), textSize.y + S(28));
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), failed ? col({48, 36, 18}) : col({24, 36, 64}), S(12));
    draw->AddRect(at, ImVec2(at.x + width, at.y + height), col(failed ? kAmber : kBlue, 0.35f), S(12));
    draw->AddText(fonts_.semibold, S(14.5f), ImVec2(at.x + S(18), at.y + (height - textSize.y) * 0.5f), col(kText),
                  message.c_str(), nullptr, textWidth);
    if (state == UpdateState::downloading && update_.total) {
        const float done =
            std::clamp(static_cast<float>(static_cast<double>(update_.received) / update_.total), 0.f, 1.f);
        const float left = at.x + S(18), right = at.x + width - S(18), y = at.y + height - S(8);
        draw->AddRectFilled(ImVec2(left, y), ImVec2(right, y + S(3)), col(kWhite, 0.1f), S(2));
        draw->AddRectFilled(ImVec2(left, y), ImVec2(left + (right - left) * done, y + S(3)), col(kBlue), S(2));
    }
    float x = at.x + width - S(12) - buttonsWidth;
    for (const auto& button : buttons) {
        ImGui::SetCursorScreenPos(ImVec2(x, at.y + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::PushID(button.label);
        if (button.primary) {
            ImGui::PushStyleColor(ImGuiCol_Button, vec(kBlue));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec({96, 155, 255}));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec({45, 110, 220}));
        }
        ImGui::BeginDisabled(!button.enabled);
        const bool clicked = secondaryButton(button.label, buttonWidth);
        ImGui::EndDisabled();
        if (button.primary)
            ImGui::PopStyleColor(3);
        if (!button.tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", button.tip.c_str());
        ImGui::PopID();
        if (clicked)
            button.action();
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
    const ImVec2 titleSize = measure(fonts_.semibold, S(15), title);
    const ImVec2 detailSize = measure(fonts_.body, S(13.5f), detail.c_str(), textWidth);
    const float height = std::max(S(50), S(8) + titleSize.y + S(1) + detailSize.y + S(9));
    statusMark(draw, ImVec2(at.x + S(11), at.y + S(8) + titleSize.y * 0.5f), mark);
    draw->AddText(fonts_.semibold, S(15), ImVec2(textX, at.y + S(8)), col(kText), title);
    draw->AddText(fonts_.body, S(13.5f), ImVec2(textX, at.y + S(8) + titleSize.y + S(1)), col(kMuted), detail.c_str(),
                  nullptr, textWidth);
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
        draw->AddLine(ImVec2(textX, at.y + height - S(0.5f)), ImVec2(at.x + width, at.y + height - S(0.5f)), col(kBorder));
}

void App::setupCard(float width) {
    if (!beginCard("##setup", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY))
        return endCard();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), 0));
    cardTitle("THIS PC", S(98));
    ImGui::BeginDisabled(scanner_.busy() || session_.running());
    if (secondaryButton(fonts_.icons ? "  Re-check" : "Re-check", S(98)))
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
            detail = scan_.epicFound ? "Only the Epic Games Store version was found. Spidy works with the Steam version."
                                     : "Not found in your Steam libraries. Install it on Steam, or show Spidy where "
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
            case Build::unsupported:
                mark = Mark::error;
                detail = scan_.gameSource == "Epic Games Store"
                             ? "Epic Games Store version. Spidy works with the Steam version only.\n" + path
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
        setupRow(mark, "Marvel's Spider-Man Remastered", detail, S(98), [&] {
            ImGui::BeginDisabled(running || !scan_.done);
            if (secondaryButton(scan_.gameExe.empty() ? "Browse..." : "Change...", S(98))) {
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
                if (secondaryButton("Get Virtual Desktop", S(210)))
                    openUrl("https://www.vrdesktop.net/");
                return;
            }
            ImGui::BeginDisabled(running);
            ImGui::PushFont(fonts_.semibold, 13.5f);
            if (ImGui::BeginCombo("##runtime", automatic ? "Automatic" : chosen ? chosen->name.c_str() : "Choose...")) {
                if (ImGui::Selectable("Automatic  (recommended)", automatic)) {
                    settings_.runtime = kAutoRuntime;
                    headset_.reset();
                    save();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Each session asks the runtimes for your headset and uses the one that has it.");
                for (const auto& r : scan_.runtimes) {
                    const std::string label = r.name + (r.tested ? "  (tested)" : "") + "##" + narrow(r.manifest);
                    if (ImGui::Selectable(label.c_str(), chosen == &r)) {
                        settings_.runtime = r.manifest;
                        headset_.reset();
                        save();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", narrow(r.manifest).c_str());
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
        setupRow(mark, "Headset", detail, S(98), [&] {
            ImGui::BeginDisabled(running || (!chosen && !automatic) || (automatic && scan_.python.empty()) ||
                                 state == Outcome::running || scan_.root.empty() || !scan_.missing.empty());
            if (secondaryButton("Check", S(98)))
                headset_.start(scan_.root, automatic ? std::wstring(kAutoRuntime) : chosen->manifest, scan_.python);
            ImGui::EndDisabled();
        });
    }
    const Outcome install = vcInstall_.state();
    const double neededGb = neededCommitGb();
    const bool memoryOk = scan_.freeCommitGb >= neededGb;
    const bool filesOk = !scan_.root.empty() && scan_.missing.empty() && !scan_.python.empty() && scan_.writable;
    if (scan_.done && scan_.vcCurrent && memoryOk && filesOk && install != Outcome::running && install != Outcome::warning) {
        const std::string detail =
            format("Visual C++ runtime %s  \xC2\xB7  %.0f GB free for programs (VR takes about %.0f)  \xC2\xB7  Python %s",
                   scan_.vcVersion.c_str(), scan_.freeCommitGb, neededGb,
                   scan_.pythonVersion.empty() ? "found" : scan_.pythonVersion.c_str());
        setupRow(Mark::ok, "Windows, memory and Spidy's files", detail, S(98), [&] {
            if (secondaryButton("Open folder", S(98)))
                openPath(scan_.root);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", narrow(scan_.root).c_str());
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
        setupRow(mark, "Visual C++ runtime", detail, offer ? S(98) : 0, offer ? std::function<void()>([&] {
            ImGui::BeginDisabled(running);
            if (secondaryButton(scan_.vcInstalled ? "Update" : "Install", S(98)))
                vcInstall_.start();
            ImGui::EndDisabled();
        }) : std::function<void()>());
    }
    // Memory Windows can still promise.
    {
        Mark mark = Mark::busy;
        std::string detail = "Checking...";
        if (scan_.done) {
            mark = memoryOk ? Mark::ok : Mark::warning;
            detail = memoryOk ? format("%.0f GB available for programs; the game in VR takes about %.0f GB.",
                                       scan_.freeCommitGb, neededGb)
                              : format("Only %.1f GB available for programs; the game in VR takes about %.0f GB. Close "
                                       "browsers, chat apps and other launchers, or enlarge the Windows page file.",
                                       scan_.freeCommitGb, neededGb);
        }
        setupRow(mark, "Memory", detail, 0, {});
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
        setupRow(mark, "Spidy files", detail, S(98), [&] {
            if (secondaryButton("Open folder", S(98)))
                openPath(scan_.root.empty() ? std::filesystem::current_path().wstring() : scan_.root);
        }, true);
    }
    ImGui::PopStyleVar();
    endCard();
}

void App::option(const char* title, const char* help, float controlWidth, const std::function<void()>& control) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float textWidth = width - controlWidth - S(16);
    const ImVec2 titleSize = measure(fonts_.semibold, S(14.5f), title, textWidth);
    const ImVec2 helpSize = measure(fonts_.body, S(12.5f), help, textWidth);
    draw->AddText(fonts_.semibold, S(14.5f), at, col(kText), title, nullptr, textWidth);
    draw->AddText(fonts_.body, S(12.5f), ImVec2(at.x, at.y + titleSize.y + S(2)), col(kMuted), help, nullptr, textWidth);
    const float height = std::max(titleSize.y + S(2) + helpSize.y, ImGui::GetFrameHeight());
    ImGui::SetCursorScreenPos(ImVec2(at.x + width - controlWidth, at.y + std::max(0.f, (titleSize.y - ImGui::GetFrameHeight()) * 0.5f) - S(1)));
    ImGui::PushID(title);
    ImGui::PushItemWidth(controlWidth);
    control();
    ImGui::PopItemWidth();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + height + S(13)));
    ImGui::Dummy(ImVec2(width, 0));
}

void App::optionsCard(ImVec2 size) {
    if (!beginCard("##options", size)) {
        endCard();
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), 0));
    cardTitle("OPTIONS");
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::BeginChild("##optionsList", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    const bool running = session_.running();
    ImGui::BeginDisabled(running);
    auto& o = settings_.options;
    bool changed = false;
    // The T-pose calibration made in the headset; Redo forgets it, and the next session asks for it again.
    const bool calibrated = spidy::launcher::calibrated(o);
    const std::string calibration =
        calibrated ? format("Eye height %.2f m, arm %.2f m, from your T-pose in the headset. Redo asks again when VR "
                            "starts.",
                            o.eyeHeightMm / 1000.0, o.armLengthMm / 1000.0)
        : o.calibrationPrompt
            ? std::string("When VR starts, stand in a T-pose and hold both triggers: Spider-Man takes your height and "
                          "arms.")
            : std::string("Skipped: your height comes from the headset. Ask again to get the T-pose when VR starts.");
    option("Body calibration", calibration.c_str(), S(98), [&] {
        if ((calibrated || !o.calibrationPrompt) && secondaryButton(calibrated ? "Redo" : "Ask again", S(98))) {
            o.eyeHeightMm = o.armLengthMm = 0;
            o.calibrationPrompt = true;
            changed = true;
        }
    });
    // A value the list does not have shows as the nearest one.
    const auto stepCombo = [&](const char* id, const char* const* labels, const int* values, int count, int* value) {
        int index = 0;
        for (int i = 1; i < count; ++i)
            if (std::abs(values[i] - *value) < std::abs(values[index] - *value))
                index = i;
        ImGui::PushFont(fonts_.semibold, 13.5f);
        if (ImGui::Combo(id, &index, labels, count)) {
            *value = values[index];
            changed = true;
        }
        ImGui::PopFont();
    };
    option("Web button", "Which button shoots and holds a web; the other reels in and shoots web balls.",
           S(150), [&] {
               static constexpr int buttons[] = {0, 1};
               int trigger = o.triggerWebs ? 1 : 0;
               stepCombo("##webButton", kWebButtons, buttons, 2, &trigger);
               o.triggerWebs = trigger != 0;
           });
    option("Webs hold in open air", "With nothing in reach, a web still holds 100 m out; off, it misses.", S(40),
           [&] { changed |= toggle("##air", &o.airWebs); });
    option("Aim markers", "Show where each hand's web would land; X switches them in VR.", S(40),
           [&] { changed |= toggle("##aim", &o.aimMarkers); });
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
        ImGui::PushFont(fonts_.semibold, 13.5f);
        if (ImGui::SliderInt("##resolution", &o.renderScale, static_cast<int>(spidy::minimumRenderScale),
                             static_cast<int>(spidy::maximumRenderScale), "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            o.renderScale = (o.renderScale + 2) / 5 * 5;
            changed = true;
        }
        ImGui::PopFont();
    });
    option("Swing speed limit", "How fast a swing can carry you.", S(150), [&] {
        ImGui::PushFont(fonts_.semibold, 13.5f);
        changed |= ImGui::SliderInt("##speed", &o.swingSpeed, 10, 65, "%d m/s");
        ImGui::PopFont();
    });
    option("Weight", "How heavy you are while swinging and after letting go. 100% is real gravity.", S(150), [&] {
        stepCombo("##weight", kWeights, kWeightValues, static_cast<int>(std::size(kWeightValues)), &o.weight);
    });
    option("Snap turn", "How far a flick of the right stick turns you.", S(150), [&] {
        stepCombo("##snap", kSnapTurns, kSnapValues, static_cast<int>(std::size(kSnapValues)), &o.snapTurn);
    });
    option("Smooth turn", "Turn steadily while you hold the right stick; off, it snap turns.", S(150), [&] {
        stepCombo("##smooth", kSmoothTurns, kSmoothValues, static_cast<int>(std::size(kSmoothValues)),
                  &o.smoothTurn);
    });
    option("Controller vibration", "How strongly webs and punches buzz.", S(150), [&] {
        stepCombo("##haptics", kHaptics, kHapticValues, static_cast<int>(std::size(kHapticValues)), &o.haptics);
    });
    option("Game screen size", "The headset's screen for menus, cutscenes and flat mode.", S(150), [&] {
        static constexpr int sizes[] = {0, 1, 2};
        stepCombo("##screen", kScreenSizes, sizes, 3, &o.screenSize);
    });
    option("Flips (experimental)", "Tap A in the air to flip; hold A there and the left stick turns you over.",
           S(40), [&] { changed |= toggle("##flips", &o.flips); });
    option("Small game window", "Saves GPU time while you play in VR.", S(40),
           [&] { changed |= toggle("##small", &o.smallWindow); });
    option("Normal camera on the monitor", "Off: the monitor shows your head's view.", S(40),
           [&] { changed |= toggle("##stock", &o.stockMonitorView); });
    if (link("Reset options")) {
        // Your body's measurements are not options: they stay.
        const SessionOptions kept = o;
        o = SessionOptions{};
        o.eyeHeightMm = kept.eyeHeightMm;
        o.armLengthMm = kept.armLengthMm;
        o.calibrationPrompt = kept.calibrationPrompt;
        changed = true;
    }
    ImGui::EndDisabled();
    if (running) {
        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::PushStyleColor(ImGuiCol_Text, vec(kFaint));
        ImGui::TextWrapped("Options apply when VR starts. In the headset, pause and open Settings, then "
                           "SPIDY VR, to change them during play; the next session starts with them.");
        ImGui::PopStyleColor();
    }
    if (changed)
        save();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    endCard();
}

void App::startBlock(ImVec2 size) {
    const bool running = session_.running();
    const auto reasons = blockers();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 buttonSize(size.x, S(62));
    if (!running) {
        if (bigButton("##start", kPlay, "START VR", buttonSize, true, reasons.empty()))
            start(false);
    } else if (!session_.stopRequested()) {
        if (bigButton("##stop", kStop, "STOP VR", buttonSize, false))
            session_.requestStop();
    } else {
        bigButton("##stopping", nullptr, "STOPPING...", buttonSize, false, false);
    }
    // What is happening, in two lines.
    std::string title, detail;
    Rgb tint = kText;
    const auto lastLine = [&](auto&& match) -> std::string {
        for (auto it = log_.rbegin(); it != log_.rend(); ++it)
            if (match(*it))
                return it->text;
        return {};
    };
    const auto seen = [&](const char* text) {
        return std::any_of(log_.begin(), log_.end(), [&](const LogLine& l) { return l.text.find(text) != std::string::npos; });
    };
    if (running) {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - session_.started()).count();
        if (session_.stopRequested()) {
            title = "Stopping VR";
            detail = "Restoring the game and saving the session report...";
            tint = kAmber;
        } else if (seen("Rendering ") || seen("Render memory:")) {
            title = format("In VR  \xC2\xB7  %d:%02d", static_cast<int>(elapsed) / 60, static_cast<int>(elapsed) % 60);
            detail = "Keep the game window in front: the game pauses behind other windows, this one too.";
            tint = kGreen;
        } else if (seen("Starting Spider-Man") || seen("Using the running game")) {
            title = seen("Using the running game") ? "Attaching to the running game" : "Starting the game";
            detail = "Its menus appear on a screen in the headset. Pick your save with the controllers.";
        } else {
            title = "Checking the headset";
            detail = "Put the headset on and keep it connected.";
        }
    } else if (auto code = session_.exitCode()) {
        const std::string error = lastLine([](const LogLine& l) { return l.kind == LineKind::error; });
        if (*code == 0) {
            title = "Session ended";
            detail = "Its report is in the reports folder.";
            tint = kGreen;
        } else if (*code == 130) {
            title = "VR start cancelled";
            tint = kMuted;
        } else {
            title = *code == 2 ? "VR could not start" : format("VR stopped (code %lu)", *code);
            detail = error.rfind("Spidy VR unavailable: ", 0) == 0 ? error.substr(22) : error;
            tint = kError;
        }
    } else if (!startError_.empty()) {
        title = "VR could not start";
        detail = startError_;
        tint = kError;
    } else if (!reasons.empty()) {
        title = scan_.done ? "Not ready yet" : "Checking this PC";
        for (const auto& r : reasons)
            detail += (detail.empty() ? "" : " ") + r;
        tint = scan_.done ? kAmber : kMuted;
    } else {
        title = "Ready";
        detail = "Starts Steam and the game. Connect your headset first.";
        tint = kGreen;
    }
    auto* draw = ImGui::GetWindowDrawList();
    const float y = at.y + buttonSize.y + S(14);
    draw->AddText(fonts_.semibold, S(15), ImVec2(at.x + S(4), y), col(tint), title.c_str());
    draw->AddText(fonts_.body, S(13), ImVec2(at.x + S(4), y + S(22)), col(kMuted), detail.c_str(), nullptr, size.x - S(8));
    // A stuck stop: after 20 s offer to end the session's process.
    if (running && session_.stopRequested() &&
        std::chrono::steady_clock::now() - session_.stopRequestedAt() > std::chrono::seconds(20)) {
        ImGui::SetCursorScreenPos(ImVec2(at.x + size.x - S(84), y));
        if (link("Force stop"))
            session_.forceStop();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ends the launcher's session at once. Restart the game before the next VR session.");
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + size.y));
    ImGui::Dummy(ImVec2(size.x, 0));
}

void App::logCard(ImVec2 size) {
    if (!beginCard("##log", size)) {
        endCard();
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8), 0));
    const float buttons = S(84) + S(8) + S(92);
    cardTitle("SESSION LOG", buttons);
    ImGui::BeginDisabled(log_.empty());
    if (secondaryButton(fonts_.icons ? "  Copy" : "Copy", S(84))) {
        std::string all;
        for (const auto& line : log_)
            all += line.text + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, S(8));
    if (secondaryButton(fonts_.icons ? "  Reports" : "Reports", S(92)) && !scan_.root.empty())
        openPath((std::filesystem::path(scan_.root) / L"reports").wstring());
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, vec({15, 18, 25}));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12), S(10)));
    ImGui::BeginChild("##lines", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
    if (log_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, vec(kMuted));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("1.  Put on your headset and connect it to this PC (Quest 3: open Virtual Desktop and connect).");
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::TextUnformatted("2.  Press START VR. Steam starts the game; its intro and menus show on a screen in the headset.");
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::TextUnformatted("3.  Pick your save with the controllers. VR takes over as soon as you play.");
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::TextUnformatted("The game pauses while another window, this one too, is in front of it on the desktop.");
        ImGui::PopTextWrapPos();
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
                const Rgb tint = line.kind == LineKind::error     ? kError
                                 : line.kind == LineKind::warning ? kAmber
                                 : line.kind == LineKind::good    ? kGreen
                                                                  : Rgb{196, 202, 214};
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
    ImGui::PopStyleColor();
    endCard();
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
          {"X", "Show or hide the aim markers: where each hand's web would land"}}},
        {"GRABBING",
         {{"Grip, aimed at a prop or thug", "Catch it with your web"},
          {"Trigger", "Reel it in"},
          {"Pull the hand sharply", "Yank it to your hand"},
          {"Release the grip", "Throw it: the harder you swing, the faster it flies"}}},
        {"MOVING",
         {{"Left stick", "Walk and run"},
          {"A", "Jump"},
          {"B", "Interact: the game's Y (backpacks, doors, prompts); web strike in a fight"},
          {"Menu button", "Pause; VR settings are in Settings > SPIDY VR"},
          {"Y", "Game menu: map, suits, skills"},
          {"Click both sticks", "Switch between VR and a flat game screen"}}},
        {"MENUS AND CUTSCENES",
         {{"Left stick", "Move through menus (they show on a screen in the headset)"},
          {"A  /  B", "Select  /  Back"},
          {"Grips", "Switch menu tabs"},
          {"Menu button", "Start"}}},
    };
    const float gap = S(16);
    float columnY[2] = {origin.y, origin.y};
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginChild("##controlsScroll", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), 0));
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float contentWidth = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - S(4);
    const float columnWidth = (contentWidth - gap) * 0.5f;
    columnY[0] = columnY[1] = base.y;
    for (int g = 0; g < 4; ++g) {
        const int column = g % 2;
        ImGui::SetCursorScreenPos(ImVec2(base.x + column * (columnWidth + gap), columnY[column]));
        ImGui::PushID(g);
        if (beginCard("##group", ImVec2(columnWidth, 0), ImGuiChildFlags_AutoResizeY)) {
            cardTitle(groups[g].title);
            ImGui::Dummy(ImVec2(0, S(8)));
            auto* draw = ImGui::GetWindowDrawList();
            const float keyWidth = S(176);
            for (const auto& entry : groups[g].entries) {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float width = ImGui::GetContentRegionAvail().x;
                const ImVec2 keySize = measure(fonts_.semibold, S(13), entry.control, keyWidth - S(16));
                const ImVec2 actionSize = measure(fonts_.body, S(14), entry.action, width - keyWidth - S(12));
                const float height = std::max(keySize.y + S(10), actionSize.y) + S(10);
                draw->AddRectFilled(at, ImVec2(at.x + std::min(keySize.x + S(16), keyWidth), at.y + keySize.y + S(10)),
                                    col(kPanelHigh), S(6));
                draw->AddText(fonts_.semibold, S(13), ImVec2(at.x + S(8), at.y + S(5)), col(kText), entry.control, nullptr,
                              keyWidth - S(16));
                draw->AddText(fonts_.body, S(14), ImVec2(at.x + keyWidth + S(12), at.y + S(3)), col({200, 205, 216}),
                              entry.action, nullptr, width - keyWidth - S(12));
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
        cardTitle("GOOD TO KNOW");
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::PushStyleColor(ImGuiCol_Text, vec({200, 205, 216}));
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
                               "tightens as you squeeze the grip.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    endCard();
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void App::about(ImVec2 origin, ImVec2 size) {
    const float gap = S(16);
    const float leftWidth = (size.x - gap) * 0.56f, rightWidth = size.x - gap - leftWidth;
    ImGui::SetCursorScreenPos(origin);
    if (beginCard("##about", ImVec2(leftWidth, 0), ImGuiChildFlags_AutoResizeY)) {
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        badge(draw, ImVec2(at.x + S(34), at.y + S(36)), S(32));
        draw->AddText(fonts_.title, S(30), ImVec2(at.x + S(84), at.y + S(6)), col(kText), "Spidy");
        draw->AddText(fonts_.body, S(15), ImVec2(at.x + S(84), at.y + S(46)), col(kMuted), "Created by");
        draw->AddText(fonts_.semibold, S(15), ImVec2(at.x + S(84) + measure(fonts_.body, S(15), "Created by ").x, at.y + S(46)),
                      col(kText), kAuthor);
        ImGui::Dummy(ImVec2(0, S(86)));
        ImGui::PushStyleColor(ImGuiCol_Text, vec({200, 205, 216}));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("Spidy puts you inside Marvel's Spider-Man Remastered: your head is the camera, each hand "
                               "shoots its own web, and you swing, zip and throw with your own arms.");
        ImGui::Dummy(ImVec2(0, S(8)));
        ImGui::TextUnformatted("Spidy changes nothing in the game's folder. It attaches to the game while it runs and "
                               "puts back what it changed when VR stops.");
        ImGui::Dummy(ImVec2(0, S(8)));
        ImGui::TextUnformatted("Sharing Spidy? Share the original zip with this credit intact, so players get "
                               "working files and know where to find updates.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, S(10)));
        ImGui::PushStyleColor(ImGuiCol_Text, vec(kFaint));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("A free fan project, not affiliated with or endorsed by Insomniac Games, Sony Interactive "
                               "Entertainment, Marvel or Valve. You need your own copy of the game on Steam.");
        ImGui::Dummy(ImVec2(0, S(8)));
        ImGui::Text("Spidy %s. Built with the OpenXR loader (Apache 2.0), MinHook (BSD), Dear ImGui (MIT) and Python (PSF).",
                    kVersion);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, S(6)));
        if (link("Third-party notices") && !scan_.root.empty())
            openPath((std::filesystem::path(scan_.root) / L"THIRD_PARTY_NOTICES.md").wstring());
        ImGui::Dummy(ImVec2(0, S(4)));
    }
    endCard();

    const float x = origin.x + leftWidth + gap;
    ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
    if (beginCard("##support", ImVec2(rightWidth, 0), ImGuiChildFlags_AutoResizeY)) {
        cardTitle("SUPPORT SPIDY");
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::PushStyleColor(ImGuiCol_Text, vec({200, 205, 216}));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("Spidy is free. It is built with a lot of AI help, and those tokens aren't free. If you "
                               "enjoy swinging through the city in VR, a tip on Ko-fi keeps the updates coming.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, S(10)));
        if (pillButton("##kofiAbout", kHeart, "Support Ilya on Ko-fi", col(kKofi), col(kKofiHover), col(kWhite),
                       ImVec2(ImGui::GetContentRegionAvail().x, S(46)), fonts_.bold, 16))
            openUrl(kKofiUrl);
        ImGui::Dummy(ImVec2(0, S(2)));
        ImGui::PushStyleColor(ImGuiCol_Text, vec(kFaint));
        ImGui::TextUnformatted("ko-fi.com/ilyamezerowsky");
        ImGui::PopStyleColor();
    }
    endCard();
    const float nextY = ImGui::GetItemRectMax().y + gap;
    ImGui::SetCursorScreenPos(ImVec2(x, nextY));
    if (beginCard("##pc", ImVec2(rightWidth, 0), ImGuiChildFlags_AutoResizeY)) {
        cardTitle("ON THIS PC");
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8), S(8)));
        const float width = ImGui::GetContentRegionAvail().x;
        if (secondaryButton(fonts_.icons ? "  Add desktop and Start menu shortcuts" : "Add desktop and Start menu shortcuts",
                            width)) {
            std::string error;
            shortcutMessage_ = createShortcuts(error) ? "Shortcuts added: look for Spidy VR." : error;
            settings_.shortcutsAsked = true;
            save();
        }
        if (secondaryButton(fonts_.icons ? "  Open the reports folder" : "Open the reports folder", width) &&
            !scan_.root.empty())
            openPath((std::filesystem::path(scan_.root) / L"reports").wstring());
        if (secondaryButton(fonts_.icons ? "  Open the Spidy folder" : "Open the Spidy folder", width) &&
            !scan_.root.empty())
            openPath(scan_.root);
        ImGui::PopStyleVar();
        if (!shortcutMessage_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, vec(kMuted));
            ImGui::TextWrapped("%s", shortcutMessage_.c_str());
            ImGui::PopStyleColor();
        }
    }
    endCard();
    ImGui::SetCursorScreenPos(ImVec2(x, ImGui::GetItemRectMax().y + gap));
    updatesCard(rightWidth);
}

void App::updatesCard(float width) {
    if (!beginCard("##updates", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY))
        return endCard();
    cardTitle("UPDATES");
    ImGui::Dummy(ImVec2(0, S(4)));
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
    ImGui::PushStyleColor(ImGuiCol_Text, vec({200, 205, 216}));
    ImGui::TextWrapped("%s", status.c_str());
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, S(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), 0));
    option("Check when Spidy starts", "Asks GitHub for a newer Spidy. Updating waits for your click.", S(40), [&] {
        if (toggle("##updateCheck", &settings_.updateCheck))
            save();
    });
    ImGui::PopStyleVar();
    const float half = (ImGui::GetContentRegionAvail().x - S(8)) * 0.5f;
    if (available && !installFolder_.empty()) {
        // The Play tab's banner shows how it goes.
        const std::string blocker = updateBlocker();
        ImGui::BeginDisabled(!blocker.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, vec(kBlue));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec({96, 155, 255}));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec({45, 110, 220}));
        if (secondaryButton("Update now", half)) {
            updateLater_ = false;
            tab_ = Tab::play;
            updater_.install();
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        if (!blocker.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", blocker.c_str());
    } else {
        ImGui::BeginDisabled(updater_.busy() || update_.state == UpdateState::installed);
        if (secondaryButton(fonts_.icons ? (std::string(kRefresh) + "  Check now").c_str() : "Check now", half)) {
            updateLater_ = false;
            updater_.check();
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine(0, S(8));
    if (secondaryButton(fonts_.icons ? (std::string(kOpen) + "  All releases").c_str() : "All releases", half))
        openUrl(kReleasesUrl);
    ImGui::Dummy(ImVec2(0, S(2)));
    endCard();
}

void App::modals() {
    if (wantLowMemory_) {
        ImGui::OpenPopup("Low on memory");
        wantLowMemory_ = false;
    }
    if (wantClose_) {
        ImGui::OpenPopup("VR is running");
        wantClose_ = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const auto dialog = [&](const char* name, const std::string& message, const char* yes, const char* no) -> int {
        int answer = -1;
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(S(460), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(20)));
        if (ImGui::BeginPopupModal(name, nullptr,
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushFont(fonts_.bold, 18);
            ImGui::TextUnformatted(name);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, S(2)));
            ImGui::PushStyleColor(ImGuiCol_Text, vec({200, 205, 216}));
            ImGui::TextWrapped("%s", message.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, S(8)));
            const float width = (ImGui::GetContentRegionAvail().x - S(10)) * 0.5f;
            ImGui::PushStyleColor(ImGuiCol_Button, vec(kRed));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec(kRedHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec({200, 30, 38}));
            if (secondaryButton(yes, width))
                answer = 1;
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, S(10));
            if (secondaryButton(no, width) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                answer = 0;
            if (answer >= 0)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        return answer;
    };
    if (dialog("Low on memory",
               format("Windows can promise programs only %.1f GB more memory, and the game in VR takes about %.0f GB. "
                      "Close browsers, chat apps and other launchers, or enlarge the Windows page file; otherwise the "
                      "game may stall or crash.",
                      scan_.freeCommitGb, neededCommitGb()),
               "Start anyway", "Cancel") == 1)
        start(true);
    if (dialog("VR is running", "Closing Spidy stops VR first: it restores the game and saves the session report. The "
                                "game keeps running.",
               "Stop VR and close", "Keep playing") == 1) {
        session_.requestStop();
        closeAfterStop_ = true;
    }
}

} // namespace launcher
