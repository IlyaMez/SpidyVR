#pragma once
// Spidy's own updates: the newest release on GitHub, and, when the player asks, its zip
// downloaded, checked against its SHA-256 and put in place of this folder's files. The
// launcher then starts again from the new files (restartLauncher).
#include <spidy/launcher_update.hpp>
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace launcher {
using spidy::launcher::Release;

inline constexpr const char* kReleasesUrl = "https://github.com/IlyaMez/SpidyVR/releases";

enum class UpdateState { idle, checking, current, available, checkFailed, downloading, installing, installed, installFailed };

struct UpdateStatus {
    UpdateState state = UpdateState::idle;
    std::optional<Release> release; // the newest release, once a check found one
    std::string message;            // why a check or an update failed
    uint64_t received{}, total{};   // the download so far
    std::string checkedAt;          // when the last check reached GitHub ("14:05")
};

// The folder this launcher runs from when it is a player's copy (Spidy Launcher.exe beside tools\);
// empty in a checkout (build\windows-ninja), which git updates.
std::wstring installFolder();

class Updater {
public:
    ~Updater();
    // Clears what an earlier update left in `folder` (empty: no player's copy), then checks GitHub when `check`.
    void start(const std::wstring& folder, bool check);
    void check();
    // Downloads the newest release and puts its files in place of the folder's.
    void install();
    // Stops a download (an update past it finishes: it takes a moment).
    void cancel();
    UpdateStatus status();
    bool busy();

private:
    void run(bool clear, bool check);
    void update(Release release);
    std::mutex mutex_;
    UpdateStatus status_;
    std::wstring folder_;
    std::thread worker_;
    std::atomic<bool> cancel_{};
};

// Starts the launcher in `folder` again (the update's), telling it the version it replaced and to wait
// for this one to close.
bool restartLauncher(const std::wstring& folder, std::string& error);

} // namespace launcher
