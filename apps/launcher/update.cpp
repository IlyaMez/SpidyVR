#include "update.hpp"
#include "system.hpp"
#include <tlhelp32.h>
#include <winhttp.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;
namespace text = spidy::launcher;

namespace launcher {
namespace {

constexpr wchar_t kLatestUrl[] = L"https://api.github.com/repos/IlyaMez/SpidyVR/releases/latest";
// Where an update works in Spidy's folder: the download, the new files, and the old files they replaced
// (the running launcher among them). The next start clears it.
constexpr wchar_t kWorkFolder[] = L".spidy-update";
// What a release's zip must hold before its files may replace the folder's.
constexpr const wchar_t* kRequired[] = {L"Spidy Launcher.exe", L"tools/run_game_vr.py", L"python/python.exe",
                                        L"build/windows-ninja/spidy_stereo_probe.dll"};

struct Internet {
    HINTERNET value{};
    explicit Internet(HINTERNET h) : value(h) {}
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
    ~Internet() {
        if (value)
            WinHttpCloseHandle(value);
    }
    explicit operator bool() const { return value != nullptr; }
};

std::string networkError(DWORD code) {
    switch (code) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR: return "GitHub could not be reached. Is this PC online?";
    case ERROR_WINHTTP_TIMEOUT: return "GitHub did not answer in time.";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return "Windows could not secure the connection to GitHub (a proxy or antivirus may be in the way).";
    }
    return "The connection to GitHub failed (Windows error " + std::to_string(code) + ").";
}

// GETs an https:// address, following redirects, and hands its body to `sink` piece by piece; `sink`
// returning false stops it (false, with `error` empty).
bool get(const std::wstring& url, const wchar_t* headers, const std::function<bool(const char*, size_t)>& sink,
         std::string& error) {
    wchar_t host[256]{}, path[4096]{}, query[4096]{};
    URL_COMPONENTS parts{sizeof(parts)};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    parts.lpszExtraInfo = query;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(query));
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        error = "Not a secure web address: " + narrow(url);
        return false;
    }
    const std::wstring agent = L"SpidyLauncher/" + widen(kVersion);
    Internet session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) // Windows before 8.1
        session.value = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = networkError(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(session.value, 15000, 15000, 30000, 30000);
    Internet connection(WinHttpConnect(session.value, host, parts.nPort, 0));
    const std::wstring target = std::wstring(path) + query;
    Internet request(connection ? WinHttpOpenRequest(connection.value, L"GET", target.c_str(), nullptr,
                                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     WINHTTP_FLAG_SECURE)
                                : nullptr);
    if (!request || !WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0,
                                        0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        error = networkError(GetLastError());
        return false;
    }
    DWORD status{}, size = sizeof(status);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        error = status == 403 || status == 429
                    ? "GitHub turned the check away for now (too many from this network). Try again later."
                : status == 404 ? "GitHub has no release of Spidy to offer."
                                : "GitHub answered with error " + std::to_string(status) + ".";
        return false;
    }
    std::vector<char> buffer(1 << 16);
    for (;;) {
        DWORD got{};
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &got)) {
            error = networkError(GetLastError());
            return false;
        }
        if (!got)
            return true;
        if (!sink(buffer.data(), got)) {
            error.clear();
            return false;
        }
    }
}

std::string localTime() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    char clock[8];
    snprintf(clock, sizeof(clock), "%02u:%02u", now.wHour, now.wMinute);
    return clock;
}

bool inside(const std::wstring& path, const std::wstring& folder) {
    return path.size() > folder.size() && path[folder.size()] == L'\\' &&
           CompareStringOrdinal(path.c_str(), static_cast<int>(folder.size()), folder.c_str(),
                                static_cast<int>(folder.size()), TRUE) == CSTR_EQUAL;
}

// A program that holds Spidy's files: one started from its folder (a VR session's Python, the headset
// check), or the game with Spidy's modules still loaded after VR. Empty: none.
std::string holder(const std::wstring& folder) {
    const HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE)
        return {};
    std::string found;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL more = Process32FirstW(processes, &entry); more && found.empty();
         more = Process32NextW(processes, &entry)) {
        if (entry.th32ProcessID == GetCurrentProcessId())
            continue;
        if (const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)) {
            wchar_t image[MAX_PATH * 4];
            DWORD length = static_cast<DWORD>(std::size(image));
            if (QueryFullProcessImageNameW(process, 0, image, &length) && inside(image, folder))
                found = narrow(entry.szExeFile);
            CloseHandle(process);
        }
        if (!found.empty() || _wcsicmp(entry.szExeFile, L"Spider-Man.exe") != 0)
            continue;
        // A game whose modules cannot be listed may hold them too.
        found = narrow(entry.szExeFile);
        HANDLE modules = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 3 && modules == INVALID_HANDLE_VALUE; ++attempt)
            modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, entry.th32ProcessID);
        if (modules == INVALID_HANDLE_VALUE)
            continue;
        bool loaded = false;
        MODULEENTRY32W module{sizeof(module)};
        for (BOOL next = Module32FirstW(modules, &module); next && !loaded; next = Module32NextW(modules, &module))
            loaded = inside(module.szExePath, folder);
        CloseHandle(modules);
        if (!loaded)
            found.clear();
    }
    CloseHandle(processes);
    return found;
}

// Every file under `folder`, relative to it with '/' between parts. Top folders named in `skip` are left
// out, and so are links (never followed), unless `noLinks`: then a link fails, with `error` naming it.
bool listFiles(const fs::path& folder, std::initializer_list<const wchar_t*> skip, bool noLinks,
               std::vector<std::wstring>& out, std::string& error) {
    std::error_code code, ignored;
    for (fs::recursive_directory_iterator it(folder, code), end; !code && it != end; it.increment(code)) {
        const auto type = it->symlink_status(ignored).type();
        if (it.depth() == 0 && type == fs::file_type::directory &&
            std::any_of(skip.begin(), skip.end(),
                        [&](const wchar_t* name) { return _wcsicmp(it->path().filename().c_str(), name) == 0; })) {
            it.disable_recursion_pending();
        } else if (type == fs::file_type::regular) {
            out.push_back(it->path().lexically_relative(folder).generic_wstring());
        } else if (type != fs::file_type::directory) {
            it.disable_recursion_pending();
            if (noLinks) {
                error = "The release's zip holds a link (" + narrow(it->path().filename().wstring()) +
                        "), which an update does not install.";
                return false;
            }
        }
    }
    if (code)
        error = "Spidy could not list " + narrow(folder.wstring()) + " (" + code.message() + ").";
    return !code;
}

struct Move {
    fs::path from, to;
};

// Renames within one drive, which Windows allows even for the running launcher and loaded modules.
bool move(const fs::path& from, const fs::path& to, std::vector<Move>& done, std::string& error) {
    std::error_code ignored;
    fs::create_directories(to.parent_path(), ignored);
    // An antivirus scan holds a new file for a moment.
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), 0)) {
            done.push_back({from, to});
            return true;
        }
        const DWORD code = GetLastError();
        if (attempt == 20 ||
            (code != ERROR_SHARING_VIOLATION && code != ERROR_ACCESS_DENIED && code != ERROR_LOCK_VIOLATION)) {
            error = "Windows kept " + narrow(from.filename().wstring()) + " in use (error " + std::to_string(code) +
                    "): close what uses Spidy's files and try again.";
            return false;
        }
        Sleep(150);
    }
}

} // namespace

std::wstring installFolder() {
    wchar_t exe[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    if (!length || length >= std::size(exe))
        return {};
    const fs::path folder = fs::path(exe).parent_path();
    std::error_code error;
    if (!fs::is_regular_file(folder / L"tools" / L"run_game_vr.py", error) || fs::exists(folder / L".git", error) ||
        fs::exists(folder / L"CMakeLists.txt", error))
        return {};
    return folder.wstring();
}

Updater::~Updater() {
    cancel_ = true;
    if (worker_.joinable())
        worker_.join();
}

void Updater::start(const std::wstring& folder, bool check) {
    folder_ = folder;
    if (folder.empty() && !check)
        return;
    if (check) {
        std::lock_guard lock(mutex_);
        status_.state = UpdateState::checking;
    }
    worker_ = std::thread([this, check] { run(!folder_.empty(), check); });
}

void Updater::check() {
    {
        std::lock_guard lock(mutex_);
        if (status_.state == UpdateState::checking || status_.state == UpdateState::downloading ||
            status_.state == UpdateState::installing || status_.state == UpdateState::installed)
            return;
        status_.state = UpdateState::checking;
        status_.message.clear();
    }
    if (worker_.joinable())
        worker_.join();
    worker_ = std::thread([this] { run(false, true); });
}

void Updater::run(bool clear, bool check) {
    if (clear) {
        std::error_code ignored;
        fs::remove_all(fs::path(folder_) / kWorkFolder, ignored);
    }
    if (!check)
        return;
    std::string answer, error;
    const bool reached = get(kLatestUrl, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28",
                             [&](const char* data, size_t size) {
                                 answer.append(data, size);
                                 return !cancel_ && answer.size() < (4u << 20);
                             },
                             error);
    std::optional<Release> release;
    if (reached)
        release = text::parseRelease(answer, error);
    else if (error.empty())
        error = "GitHub's answer was too long.";
    std::lock_guard lock(mutex_);
    if (!release) {
        status_.state = UpdateState::checkFailed;
        status_.message = error;
        return;
    }
    status_.checkedAt = localTime();
    status_.state = text::newerVersion(release->version, kVersion) ? UpdateState::available : UpdateState::current;
    status_.release = std::move(release);
}

void Updater::install() {
    Release release;
    {
        std::lock_guard lock(mutex_);
        if (folder_.empty() || !status_.release ||
            (status_.state != UpdateState::available && status_.state != UpdateState::installFailed))
            return;
        release = *status_.release;
        status_.state = UpdateState::downloading;
        status_.received = 0;
        status_.total = release.zipSize;
        status_.message.clear();
    }
    if (worker_.joinable())
        worker_.join();
    cancel_ = false;
    worker_ = std::thread([this, release] { update(release); });
}

void Updater::update(Release release) {
    const fs::path folder(folder_), work = folder / kWorkFolder;
    const auto fail = [&](std::string message) {
        std::lock_guard lock(mutex_);
        status_.state = UpdateState::installFailed;
        status_.message = std::move(message);
    };
    if (const auto name = holder(folder_); !name.empty())
        return fail("Close " + name + " first: it still uses Spidy's files.");
    std::error_code ignored;
    fs::remove_all(work, ignored);
    fs::create_directories(work, ignored);
    SetFileAttributesW(work.c_str(), FILE_ATTRIBUTE_HIDDEN);

    const fs::path zip = work / widen(release.zipName);
    const HANDLE file = CreateFileW(zip.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return fail("Spidy could not write in its folder.");
    uint64_t received = 0;
    std::string stopped, error;
    const bool downloaded = get(widen(release.zipUrl), L"Accept: application/octet-stream",
                                [&](const char* data, size_t size) {
                                    DWORD written{};
                                    if (cancel_)
                                        stopped = "cancelled";
                                    else if (received + size > release.zipSize)
                                        stopped = "The download is larger than the release says.";
                                    else if (!WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) ||
                                             written != size)
                                        stopped = "Spidy could not save the download (is the drive full?).";
                                    if (!stopped.empty())
                                        return false;
                                    received += size;
                                    std::lock_guard lock(mutex_);
                                    status_.received = received;
                                    return true;
                                },
                                error);
    CloseHandle(file);
    if (stopped == "cancelled") {
        fs::remove_all(work, ignored);
        std::lock_guard lock(mutex_);
        status_.state = UpdateState::available;
        return;
    }
    if (!downloaded)
        return fail(stopped.empty() ? error : stopped);
    if (received != release.zipSize)
        return fail("The download stopped early. Try again.");
    if (sha256(zip.wstring()) != release.sha256) {
        fs::remove(zip, ignored);
        return fail("The download does not match the release's SHA-256, so it was not installed. Try again.");
    }
    {
        std::lock_guard lock(mutex_);
        status_.state = UpdateState::installing;
    }

    // Windows 10 and 11 have tar, which unpacks zips; Spidy's own Python can too.
    const fs::path unpacked = work / L"package";
    fs::create_directories(unpacked, ignored);
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    const fs::path tar = fs::path(system) / L"tar.exe", python = folder / L"python" / L"python.exe";
    const std::wstring command =
        fs::is_regular_file(tar, ignored)
            ? text::quoteArgument(tar.wstring()) + L" -xf " + text::quoteArgument(zip.wstring()) + L" -C " +
                  text::quoteArgument(unpacked.wstring())
            : text::quoteArgument(python.wstring()) + L" -I -B -m zipfile -e " + text::quoteArgument(zip.wstring()) +
                  L" " + text::quoteArgument(unpacked.wstring());
    std::string output;
    if (capture(command, work.wstring(), nullptr, 180000, output) != 0u) {
        while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back())))
            output.pop_back();
        return fail("The zip could not be unpacked" + (output.empty() ? std::string(".") : ": " + output));
    }
    const fs::path package = unpacked / widen("Spidy-" + release.version);
    for (const auto* required : kRequired)
        if (!fs::is_regular_file(package / required, ignored))
            return fail("The release's zip lacks " + narrow(fs::path(required).make_preferred().wstring()) + ".");
    std::vector<std::wstring> shipped, installed;
    if (!listFiles(package, {}, true, shipped, error) ||
        !listFiles(folder, {kWorkFolder, L"reports"}, false, installed, error))
        return fail(error);
    const auto plan = text::updatePlan(installed, shipped);
    // The download took a while.
    if (const auto name = holder(folder_); !name.empty())
        return fail("Close " + name + " first: it still uses Spidy's files.");

    // Old files go to previous\ and the package's take their place. A failure puts everything back.
    const fs::path previous = work / L"previous";
    std::vector<Move> done;
    bool moved = true;
    for (const auto& path : plan.retire)
        moved = moved && move(folder / path, previous / path, done, error);
    for (const auto& path : plan.replace)
        moved = moved && move(folder / path, previous / path, done, error) && move(package / path, folder / path, done, error);
    for (const auto& path : plan.add)
        moved = moved && move(package / path, folder / path, done, error);
    if (!moved) {
        bool restored = true;
        for (auto it = done.rbegin(); it != done.rend(); ++it)
            restored = MoveFileExW(it->to.c_str(), it->from.c_str(), 0) && restored;
        return fail(error + (restored ? " Nothing was changed."
                                      : " Some files could not be put back; the old ones are in " +
                                            narrow(previous.wstring()) + "."));
    }
    fs::remove(zip, ignored);
    fs::remove_all(unpacked, ignored);
    std::lock_guard lock(mutex_);
    status_.state = UpdateState::installed;
}

void Updater::cancel() { cancel_ = true; }

UpdateStatus Updater::status() {
    std::lock_guard lock(mutex_);
    return status_;
}

bool Updater::busy() {
    std::lock_guard lock(mutex_);
    return status_.state == UpdateState::checking || status_.state == UpdateState::downloading ||
           status_.state == UpdateState::installing;
}

bool restartLauncher(const std::wstring& folder, std::string& error) {
    const std::wstring exe = (fs::path(folder) / L"Spidy Launcher.exe").wstring();
    std::wstring command = text::quoteArgument(exe) + L" --updated-from " + widen(kVersion) + L" --wait-for " +
                           std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup,
                        &info)) {
        error = "Spidy is updated, but Windows did not start its launcher (error " + std::to_string(GetLastError()) +
                "). Start Spidy Launcher from the Spidy folder.";
        return false;
    }
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return true;
}

} // namespace launcher
