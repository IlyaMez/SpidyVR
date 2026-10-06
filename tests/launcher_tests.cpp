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
        const std::vector<std::wstring> defaults{L"--auto-launch", L"--seconds", L"0", L"--size", L"0", L"--swing-speed",
                                                 L"32", L"--output", L"r.json", L"--xr-runtime", L"vd.json",
                                                 L"--stop-event", L"Local\\Stop"};
        check(args == defaults, "defaults");
        options = {false, true, false, true, 2048, 90, false, false};
        args = sessionArguments(options, L"r.json", L"", L"");
        const std::vector<std::wstring> changed{L"--auto-launch", L"--seconds", L"0", L"--size", L"2048",
                                                L"--swing-speed", L"65", L"--output", L"r.json", L"--overlay-webs",
                                                L"--no-web-grab", L"--full-desktop-view", L"--stock-monitor-view",
                                                L"--no-body", L"--no-punch"};
        check(args == changed, "every flag, speed capped at 65");
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
