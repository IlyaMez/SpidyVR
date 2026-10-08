#include "spidy/launcher_text.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace spidy::launcher;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    int total = 0, failed = 0;
    auto test = [&](const char* name, const std::function<void()>& f) {
        ++total;
        try {
            f();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    };
    test("Steam library folders: every path, escapes decoded, nested blocks skipped", [] {
        const auto values = vdfValues(R"("libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"label"		""
		"apps"
		{
			"1817070"		"80213345236"
		}
	}
	"1"
	{
		"PATH"		"D:\\SteamLibrary"   // a comment "path" "E:\\not"
	}
})", "path");
        check(values.size() == 2, "two libraries");
        check(values[0] == R"(C:\Program Files (x86)\Steam)", "backslashes decoded");
        check(values[1] == R"(D:\SteamLibrary)", "keys match without case; comments skipped");
    });
    test("app manifest gives the install folder, apostrophe included", [] {
        const auto folders = vdfValues(R"("AppState" { "appid" "1817070" "installdir" "Marvel's Spider-Man Remastered" })",
                                       "installdir");
        check(folders.size() == 1 && folders[0] == "Marvel's Spider-Man Remastered", "installdir");
        check(vdfValues(R"("a" { "installdir" })", "installdir").empty(), "a key without a value");
    });
    test("Epic manifests are JSON, but read the same way", [] {
        const auto folders =
            vdfValues(R"({"DisplayName": "Marvel's Spider-Man Remastered", "InstallLocation": "D:\\Epic\\MarvelsSpiderMan"})",
                      "InstallLocation");
        check(folders.size() == 1 && folders[0] == R"(D:\Epic\MarvelsSpiderMan)", "install location");
    });
    test("constants come from the Python tools", [] {
        const std::string source = "\"\"\"doc EXPECTED_SHA256 = 'no'\"\"\"\nimport x\nEXPECTED_SHA256 = \"e297d4\"\n"
                                   "VR_COMMIT_MB = 19000\n# VR_COMMIT_MB = 1\nOTHER_VR_COMMIT_MB = 5\n";
        check(pythonConstant(source, "EXPECTED_SHA256") == std::optional<std::string>("e297d4"), "string at a line start");
        check(pythonConstant(source, "VR_COMMIT_MB") == std::optional<std::string>("19000"), "number, not a comment");
        check(!pythonConstant(source, "MISSING"), "missing");
    });
    test("runtime names match tools/xr_runtime.py", [] {
        check(runtimeLabel(R"(C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json)") ==
                  "Virtual Desktop", "Virtual Desktop");
        check(runtimeLabel(R"(C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json)") == "SteamVR",
              "SteamVR");
        check(runtimeLabel(R"(C:\Program Files\Oculus\Support\oculus-runtime\oculus_openxr_64.json)") == "Meta Quest Link",
              "Oculus");
        check(runtimeLabel(R"(C:\elsewhere\runtime.json)").empty(), "unknown");
    });
    test("the headset check names the headset and the runtime it was found in", [] {
        const char* probe = "Headset available: Oculus Quest3; position tracking=1; orientation tracking=1. No session "
                            "was started.\r\nRecommended eye 0: 3072x3264\r\nRecommended eye 1: 3072x3264\r\n";
        check(headsetSummary(probe, true) == "Connected: Oculus Quest3 - 3072x3264 per eye", "one runtime's probe");
        const std::string detect = std::string("Looking for the headset in Virtual Desktop...\nLooking for the headset "
                                               "in SteamVR...\nVR runtime: SteamVR (C:\\SteamVR\\steamxr_win64.json), "
                                               "found automatically.\n") + probe;
        check(headsetSummary(detect, true) == "Connected: Oculus Quest3 via SteamVR - 3072x3264 per eye", "detected");
        check(headsetSummary("Headset unavailable: XR_ERROR_FORM_FACTOR_UNAVAILABLE (-35).\n", false) ==
                  "Not found. Put the headset on and connect it (Headset unavailable: XR_ERROR_FORM_FACTOR_UNAVAILABLE "
                  "(-35).).",
              "the probe's reason");
        check(headsetSummary("Looking for the headset in Virtual Desktop...\nNo headset found (Virtual Desktop: x).\n",
                             false) == "Not found. Put the headset on and connect it (No headset found (Virtual Desktop: "
                                       "x).).",
              "detection's reason, not its progress");
        check(headsetSummary("", false) == "Not found. Put the headset on and connect it.", "nothing said");
        check(headsetSummary("", true) == "Headset available.", "found, nothing said");
    });
    test("arguments survive Windows command-line quoting", [] {
        check(quoteArgument(L"plain") == L"plain", "plain");
        check(quoteArgument(L"") == L"\"\"", "empty");
        check(quoteArgument(LR"(C:\Program Files\Spidy\run.py)") == LR"("C:\Program Files\Spidy\run.py")", "spaces");
        check(quoteArgument(LR"(C:\Spidy Folder\)") == LR"("C:\Spidy Folder\\")", "trailing backslash doubled");
        check(quoteArgument(LR"(say "hi")") == LR"("say \"hi\"")", "quotes escaped");
    });
    test("session arguments carry every option", [] {
        SessionOptions options;
        auto args = sessionArguments(options, L"r.json", L"vd.json", L"Local\\Stop");
        const std::vector<std::wstring> defaults{L"--auto-launch", L"--seconds", L"0", L"--swing-speed", L"32",
                                                 L"--output", L"r.json", L"--xr-runtime", L"vd.json",
                                                 L"--stop-event", L"Local\\Stop"};
        check(args == defaults, "defaults");
        options = {false, true, 2048, 90, false, false};
        args = sessionArguments(options, L"r.json", L"", L"");
        const std::vector<std::wstring> changed{L"--auto-launch", L"--seconds", L"0", L"--swing-speed", L"65",
                                                L"--output", L"r.json", L"--full-desktop-view",
                                                L"--stock-monitor-view", L"--no-aim-markers", L"--no-air-webs",
                                                L"--render-scale", L"200"};
        check(args == changed, "every flag, speed capped at 65 and the render scale at 200%");
        options = {};
        options.renderScale = 125;
        options.snapTurn = 45;
        options.smoothTurn = 120;
        options.haptics = 0;
        options.screenSize = 7;
        options.weight = 150;
        args = sessionArguments(options, L"r.json", L"", L"");
        const std::vector<std::wstring> settings{L"--auto-launch", L"--seconds", L"0", L"--swing-speed", L"32",
                                                 L"--output", L"r.json", L"--render-scale", L"125", L"--snap-turn",
                                                 L"45", L"--smooth-turn", L"120", L"--haptics", L"0",
                                                 L"--screen-size", L"2", L"--weight", L"150"};
        check(args == settings, "the VR settings, the screen size capped at large");
        options = {};
        options.renderScale = 10;
        args = sessionArguments(options, L"r.json", L"", L"");
        check(args.size() == 9 && args[7] == L"--render-scale" && args[8] == L"50", "the render scale at least 50%");
        options = {};
        options.weight = 999;
        args = sessionArguments(options, L"r.json", L"", L"");
        check(args.size() == 9 && args[7] == L"--weight" && args[8] == L"300", "the weight at most 300%");
        options = {};
        check(!options.flips, "the experimental flips are on in a new launcher");
        options.flips = true;
        args = sessionArguments(options, L"r.json", L"", L"");
        check(args.size() == 8 && args[7] == L"--flips", "the experimental flips switched on");
    });
    test("the headset check's eye size and the memory larger eyes take", [] {
        const char* probe = "Headset available: Oculus Quest3; position tracking=1; orientation tracking=1. No session "
                            "was started.\r\nRecommended eye 0: 2496x2688\r\nRecommended eye 1: 2496x2688\r\n";
        const auto eye = recommendedEye(probe);
        check(eye && (*eye)[0] == 2496 && (*eye)[1] == 2688, "the probe's first eye");
        check(!recommendedEye("Headset available: x.\n") && !recommendedEye("Recommended eye 0: 0x2688\n") &&
                  !recommendedEye("Recommended eye 0: 2496 x 2688\n") &&
                  !recommendedEye("Not Recommended eye 0: 2496x2688\n"),
              "no size, an invalid one, another format, another line");
        check(vrCommitGb(19000, 150, {3072, 3264}) == 19000.0 / 1024, "VR_COMMIT_MB at its own eye size");
        check(vrCommitGb(19000, 150, {2496, 2688}) == 19000.0 / 1024, "never less for smaller eyes");
        const double larger = vrCommitGb(19000, 150, {4608, 4896});
        check(larger > 22.0 && larger < 22.1, "150% of 3072 x 3264: 2 x 12.5 megapixels more at 150 bytes");
    });
    test("what the VR settings were left at in the headset becomes the next session's options", [] {
        SessionOptions options;
        check(headsetSettings("VR settings from the headset: aim_markers=0 web_grab=1 air_webs=0 web_shooter=0 "
                              "punch=0 body=1 swing_speed=48 weight=150 snap_turn=45 smooth_turn=90 haptics=50 "
                              "flips=1 screen_size=2\r",
                              options),
              "the line changed nothing");
        check(!options.aimMarkers && !options.airWebs && options.swingSpeed == 48 && options.weight == 150 &&
                  options.snapTurn == 45 && options.smoothTurn == 90 && options.haptics == 50 && options.flips &&
                  options.screenSize == 2,
              "every value, the last one before a carriage return");
        const auto kept = options;
        check(!headsetSettings("VR settings from the headset: aim_markers=0 air_webs=0", options) && options == kept,
              "the same values again");
        check(!headsetSettings("VR settings from the headset: web_grab=0 web_shooter=0 punch=0 body=0", options) &&
                  options == kept,
              "web grab, the web shooter, punching and the body are no options");
        check(!headsetSettings("Game closed. Session report: r.json", options) && options == kept, "another line");
        check(headsetSettings("VR settings from the headset: swing_speed=99 snap_turn=x smooth_turn=999 haptics=-4 "
                              "weight=7 colour=3",
                              options) &&
                  options.swingSpeed == 65 && options.snapTurn == 45 && options.smoothTurn == 360 &&
                  options.haptics == 0 && options.weight == 40,
              "values outside their ranges, unreadable ones and unknown keys");
    });
    test("a T-pose calibration in the headset sizes every next session, until Redo", [] {
        SessionOptions options;
        check(!calibrated(options) && options.calibrationPrompt, "a new launcher has one");
        // Calibrated in the headset: eyes 1.63 m high, arms 0.59 m.
        check(headsetSettings("VR settings from the headset: aim_markers=1 air_webs=1 swing_speed=32 weight=60 "
                              "snap_turn=30 smooth_turn=0 haptics=100 screen_size=1 eye_height_mm=1630 "
                              "arm_length_mm=590 calibration_prompt=1",
                              options) &&
                  calibrated(options) && options.eyeHeightMm == 1630 && options.armLengthMm == 590,
              "the calibration did not come back from the headset");
        auto args = sessionArguments(options, L"r.json", L"", L"");
        const std::vector<std::wstring> sized{L"--auto-launch", L"--seconds", L"0",    L"--swing-speed",
                                              L"32",            L"--output",  L"r.json", L"--eye-height",
                                              L"1630",          L"--arm-length", L"590"};
        check(args == sized, "the next session is not sized");
        // Half a calibration, or one out of range, keeps the one there was.
        const auto kept = options;
        check(!headsetSettings("VR settings from the headset: eye_height_mm=1700 arm_length_mm=0", options) &&
                  options == kept,
              "half a calibration replaced it");
        check(!headsetSettings("VR settings from the headset: eye_height_mm=99999 arm_length_mm=590", options) &&
                  options == kept,
              "an eye height out of range replaced it");
        options.armLengthMm = 1300;
        check(!calibrated(options), "an arm out of range counted");
        // Skipped in the headset: no calibration, and none asked for at the next sessions' first gameplay.
        options = {};
        check(headsetSettings("VR settings from the headset: eye_height_mm=0 arm_length_mm=0 calibration_prompt=0",
                              options) &&
                  !calibrated(options) && !options.calibrationPrompt,
              "skipping it did not come back");
        args = sessionArguments(options, L"r.json", L"", L"");
        check(args.size() == 8 && args[7] == L"--no-calibration-prompt", "the next session still asks");
        // A calibration made from the SPIDY VR tab after a skip counts; the skip says nothing then.
        options.eyeHeightMm = 1500;
        options.armLengthMm = 520;
        args = sessionArguments(options, L"r.json", L"", L"");
        check(args.size() == 11 && args[7] == L"--eye-height" && args[8] == L"1500", "the later calibration");
    });
    test("log lines are coloured by what they say", [] {
        check(classifyLine("WARNING: render memory module unavailable") == LineKind::warning, "warning");
        check(classifyLine("Spidy VR unavailable: no headset") == LineKind::error, "error");
        check(classifyLine("Headset available: Oculus Quest3") == LineKind::good, "good");
        check(classifyLine("Starting Spider-Man.") == LineKind::normal, "normal");
    });
    test("runtime versions compare field by field", [] {
        check(parseVersion("14.44.35211.0") >= std::array<int, 4>{14, 40, 0, 0}, "newer");
        check(parseVersion("14.38.33135") < std::array<int, 4>{14, 40, 0, 0}, "older");
        check(parseVersion("v14.40") == std::array<int, 4>{14, 40, 0, 0}, "prefix");
    });
    std::cout << (total - failed) << '/' << total << " launcher checks passed\n";
    return failed ? 1 : 0;
}
