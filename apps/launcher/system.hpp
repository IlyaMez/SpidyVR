#pragma once
// What the launcher finds on this PC, what it remembers, and the VR session it
// runs (tools/run_game_vr.py). Long work runs on worker threads; the window
// reads snapshots.
#include <spidy/launcher_text.hpp>
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace launcher {
using spidy::launcher::LineKind;
using spidy::launcher::SessionOptions;

inline constexpr const char* kVersion = SPIDY_VERSION;
inline constexpr const char* kAuthor = "Ilya Mezerowsky";
inline constexpr const char* kKofiUrl = "https://ko-fi.com/ilyamezerowsky";
inline constexpr const wchar_t* kSteamAppId = L"1817070";

std::string narrow(std::wstring_view text);
std::wstring widen(std::string_view text);

enum class Build { unknown, checking, supported, unsupported, unreadable };

struct Runtime {
    std::wstring manifest;
    std::string name;
    bool active{}, tested{};
};

struct Scan {
    bool done{};
    // Spidy's own files: the folder with tools\run_game_vr.py and the modules.
    std::wstring root, python;
    std::string pythonVersion;
    std::vector<std::string> missing;
    bool writable = true;
    // The game.
    std::wstring steam, gameExe;
    std::string gameSource;
    bool epicFound{}, gameRunning{};
    Build build = Build::unknown;
    std::string expectedHash;
    std::wstring hashPath;
    uint64_t hashSize{}, hashTime{};
    std::string hashValue;
    // VR and Windows.
    std::vector<Runtime> runtimes;
    bool vcInstalled{}, vcCurrent{};
    std::string vcVersion;
    double freeCommitGb{}, neededCommitGb = 19;
};

struct Settings {
    std::wstring gameExe, runtime;
    SessionOptions options;
    std::wstring hashPath;
    uint64_t hashSize{}, hashTime{};
    std::string hashValue;
    bool shortcutsAsked{};
};
Settings loadSettings();
void saveSettings(const Settings& settings);

class Scanner {
public:
    ~Scanner();
    void start(const Settings& settings);
    Scan snapshot();
    bool busy();

private:
    void run(Settings settings);
    std::mutex mutex_;
    Scan scan_;
    std::thread worker_;
    bool busy_{};
};

enum class Outcome { idle, running, ok, warning, error };

// spidy_headset_probe.exe against the chosen runtime.
class HeadsetCheck {
public:
    ~HeadsetCheck();
    void start(const std::wstring& root, const std::wstring& manifest);
    Outcome state();
    std::string summary();
    void reset();

private:
    std::mutex mutex_;
    Outcome state_ = Outcome::idle;
    std::string summary_;
    std::thread worker_;
};

// Downloads Microsoft's Visual C++ runtime installer, checks its signature, runs it.
class RuntimeInstall {
public:
    ~RuntimeInstall();
    void start();
    Outcome state();
    std::string message();

private:
    std::mutex mutex_;
    Outcome state_ = Outcome::idle;
    std::string message_;
    std::thread worker_;
};

struct LogLine {
    std::string text;
    LineKind kind;
};

class Session {
public:
    ~Session();
    bool start(const std::wstring& python, const std::wstring& root, const std::vector<std::wstring>& args,
               const std::wstring& report, std::string& error);
    void requestStop();
    void forceStop();
    bool running();
    bool stopRequested();
    std::optional<DWORD> exitCode();
    // Lines from `from` on; returns the total so far.
    size_t lines(size_t from, std::vector<LogLine>& out);
    void clear();
    std::chrono::steady_clock::time_point started() const { return started_; }
    std::chrono::steady_clock::time_point stopRequestedAt() const { return stopAt_; }
    const std::wstring& report() const { return report_; }
    std::wstring stopEventName() const { return eventName_; }

private:
    void read(HANDLE pipe, HANDLE process);
    std::mutex mutex_;
    std::vector<LogLine> lines_;
    std::thread reader_;
    HANDLE process_{}, stopEvent_{};
    std::wstring eventName_, report_;
    bool running_{}, stopRequested_{};
    std::optional<DWORD> exit_;
    std::chrono::steady_clock::time_point started_, stopAt_;
};

// Shell actions.
void openUrl(const char* url);
void openPath(const std::wstring& path);
std::optional<std::wstring> browseForGame(HWND owner, const std::wstring& current);
bool createShortcuts(std::string& error);
std::wstring newReportPath(const std::wstring& root);
std::wstring newStopEventName();

} // namespace launcher
