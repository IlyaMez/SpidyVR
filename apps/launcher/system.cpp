#include "system.hpp"
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <urlmon.h>
#include <wintrust.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace text = spidy::launcher;

namespace launcher {
namespace {

// What tools/launch-game-vr.ps1 requires before it starts a session.
constexpr std::array kModules{L"spidy_headset_probe.exe", L"spidy_bridge.dll",       L"spidy_render_probe.dll",
                              L"spidy_ray_bridge.dll",    L"spidy_movement_bridge.dll", L"spidy_stereo_probe.dll",
                              L"spidy_render_memory.dll"};
// Binaries built with MSVC 14.40 or later need at least that runtime (std::mutex changed).
constexpr std::array kMinimumRuntime{14, 40, 0, 0};

struct Handle {
    HANDLE value{};
    Handle() = default;
    explicit Handle(HANDLE h) : value(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
    explicit operator bool() const { return value != nullptr; }
};

std::wstring registryString(HKEY root, const wchar_t* key, const wchar_t* name, DWORD flags = 0) {
    wchar_t buffer[2048];
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | flags, nullptr, buffer, &size) !=
        ERROR_SUCCESS)
        return {};
    return buffer;
}

std::wstring environment(const wchar_t* name) {
    wchar_t buffer[4096];
    const DWORD size = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    return size && size < std::size(buffer) ? std::wstring(buffer, size) : std::wstring();
}

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR path{};
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
        result = path;
    CoTaskMemFree(path);
    return result;
}

std::string readText(const fs::path& path, size_t limit = 4 << 20) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    std::string content(limit, '\0');
    file.read(content.data(), static_cast<std::streamsize>(limit));
    content.resize(static_cast<size_t>(file.gcount()));
    return content;
}

bool isFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

// The path as Windows spells it (Steam's registry keeps it in lower case).
std::wstring truePath(const std::wstring& path) {
    Handle file(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = file ? GetFinalPathNameByHandleW(file.value, buffer, static_cast<DWORD>(std::size(buffer)),
                                                          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)
                              : 0;
    if (!length || length >= std::size(buffer))
        return path;
    std::wstring result(buffer, length);
    if (result.rfind(L"\\\\?\\UNC\\", 0) == 0)
        return L"\\\\" + result.substr(8);
    return result.rfind(L"\\\\?\\", 0) == 0 ? result.substr(4) : result;
}

bool sameFile(const std::wstring& a, const std::wstring& b) {
    return !a.empty() && CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

// The folder that holds tools\run_game_vr.py: the package folder, or the
// checkout when the launcher runs from build\windows-ninja.
std::wstring findRoot() {
    wchar_t exe[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    fs::path folder = fs::path(exe).parent_path();
    for (int i = 0; i < 5 && !folder.empty(); ++i, folder = folder.parent_path()) {
        if (isFile(folder / L"tools" / L"run_game_vr.py"))
            return folder.wstring();
        if (folder == folder.parent_path())
            break;
    }
    return {};
}

// Environment block with one variable set (or added).
std::wstring environmentWith(const wchar_t* name, const std::wstring& value) {
    std::wstring block;
    const size_t nameLength = wcslen(name);
    if (wchar_t* strings = GetEnvironmentStringsW()) {
        for (const wchar_t* entry = strings; *entry; entry += wcslen(entry) + 1) {
            const bool same = _wcsnicmp(entry, name, nameLength) == 0 && entry[nameLength] == L'=';
            if (!same) {
                block += entry;
                block += L'\0';
            }
        }
        FreeEnvironmentStringsW(strings);
    }
    block += name;
    block += L'=';
    block += value;
    block += L'\0';
    block += L'\0';
    return block;
}

// Starts a hidden process whose output goes to `output`; only the listed
// handles are inherited, so parallel checks never hold each other's pipes.
HANDLE spawn(std::wstring commandLine, const std::wstring& folder, std::wstring* environmentBlock, HANDLE output,
             std::string& error) {
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
                             nullptr));
    HANDLE handles[] = {input.value, output};
    SIZE_T size{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> storage(size);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!input || !InitializeProcThreadAttributeList(attributes, 1, 0, &size) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr,
                                   nullptr)) {
        error = "Windows refused to prepare the process.";
        return nullptr;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = output;
    startup.StartupInfo.hStdError = output;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION info{};
    const BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                                   environmentBlock ? environmentBlock->data() : nullptr,
                                   folder.empty() ? nullptr : folder.c_str(), &startup.StartupInfo, &info);
    DeleteProcThreadAttributeList(attributes);
    if (!ok) {
        error = "Could not start " + narrow(commandLine) + " (Windows error " + std::to_string(GetLastError()) + ").";
        return nullptr;
    }
    CloseHandle(info.hThread);
    return info.hProcess;
}

} // namespace

// Runs a command to completion (or `timeoutMs`), collecting what it prints.
std::optional<DWORD> capture(const std::wstring& commandLine, const std::wstring& folder, std::wstring* environmentBlock,
                             DWORD timeoutMs, std::string& output) {
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE readEnd{}, writeEnd{};
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0))
        return std::nullopt;
    Handle reader(readEnd), writer(writeEnd);
    SetHandleInformation(reader.value, HANDLE_FLAG_INHERIT, 0);
    std::string error;
    Handle process(spawn(commandLine, folder, environmentBlock, writer.value, error));
    if (!process) {
        output = error;
        return std::nullopt;
    }
    CloseHandle(writer.value);
    writer.value = nullptr;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    char buffer[4096];
    for (;;) {
        DWORD available{};
        if (PeekNamedPipe(reader.value, nullptr, 0, nullptr, &available, nullptr) && available) {
            DWORD got{};
            if (ReadFile(reader.value, buffer, std::min<DWORD>(available, sizeof(buffer)), &got, nullptr))
                output.append(buffer, got);
            continue;
        }
        if (WaitForSingleObject(process.value, 20) == WAIT_OBJECT_0) {
            while (PeekNamedPipe(reader.value, nullptr, 0, nullptr, &available, nullptr) && available) {
                DWORD got{};
                if (!ReadFile(reader.value, buffer, std::min<DWORD>(available, sizeof(buffer)), &got, nullptr))
                    break;
                output.append(buffer, got);
            }
            DWORD code{};
            GetExitCodeProcess(process.value, &code);
            return code;
        }
        if (GetTickCount64() > deadline) {
            TerminateProcess(process.value, 1);
            output += "\n(timed out)";
            return std::nullopt;
        }
    }
}

namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;
    return value.substr(start);
}

// --- Spidy's files ---------------------------------------------------------

std::wstring findPython(const std::wstring& root) {
    std::vector<fs::path> candidates{fs::path(root) / L"python" / L"python.exe"};
    const std::wstring profile = environment(L"USERPROFILE");
    if (!profile.empty())
        candidates.push_back(fs::path(profile) / L".cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe");
    wchar_t found[MAX_PATH * 2];
    if (SearchPathW(nullptr, L"python.exe", nullptr, static_cast<DWORD>(std::size(found)), found, nullptr)) {
        // The Microsoft Store stub opens the Store instead of running Python.
        if (!wcsstr(found, L"WindowsApps"))
            candidates.emplace_back(found);
    }
    const std::wstring local = environment(L"LOCALAPPDATA");
    std::error_code error;
    if (!local.empty())
        for (const auto& entry : fs::directory_iterator(fs::path(local) / L"Programs/Python", error))
            candidates.push_back(entry.path() / L"python.exe");
    for (const auto& candidate : candidates)
        if (isFile(candidate))
            return candidate.wstring();
    return {};
}

bool folderWritable(const fs::path& folder) {
    std::error_code error;
    fs::create_directories(folder, error);
    const fs::path probe = folder / L".spidy-write-check";
    {
        std::ofstream file(probe, std::ios::binary);
        if (!file)
            return false;
        file << "ok";
    }
    fs::remove(probe, error);
    return true;
}

// --- The game ----------------------------------------------------------------

std::vector<std::wstring> steamRoots() {
    std::vector<std::wstring> roots;
    for (auto path : {registryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"),
                      registryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"),
                      registryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath"),
                      environment(L"ProgramFiles(x86)") + L"\\Steam"}) {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        if (!path.empty() && isFile(fs::path(path) / L"steam.exe") &&
            std::none_of(roots.begin(), roots.end(), [&](const auto& r) { return sameFile(r, path); }))
            roots.push_back(path);
    }
    return roots;
}

std::wstring steamGame(const std::vector<std::wstring>& roots) {
    std::vector<std::wstring> libraries;
    for (const auto& root : roots) {
        libraries.push_back(root);
        for (const auto* file : {L"steamapps\\libraryfolders.vdf", L"config\\libraryfolders.vdf"})
            for (const auto& path : text::vdfValues(readText(fs::path(root) / file), "path"))
                libraries.push_back(widen(path));
    }
    const std::wstring manifest = std::wstring(L"steamapps\\appmanifest_") + kSteamAppId + L".acf";
    for (const auto& library : libraries) {
        const fs::path acf = fs::path(library) / manifest;
        if (!isFile(acf))
            continue;
        auto folders = text::vdfValues(readText(acf), "installdir");
        const std::wstring folder = folders.empty() ? L"Marvel's Spider-Man Remastered" : widen(folders.front());
        const fs::path exe = fs::path(library) / L"steamapps\\common" / folder / L"Spider-Man.exe";
        if (isFile(exe))
            return exe.wstring();
    }
    // Steam also records the folder for Windows' installed-apps list.
    const std::wstring uninstall = registryString(
        HKEY_LOCAL_MACHINE,
        (std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App ") + kSteamAppId).c_str(),
        L"InstallLocation", RRF_SUBKEY_WOW6464KEY);
    if (!uninstall.empty() && isFile(fs::path(uninstall) / L"Spider-Man.exe"))
        return (fs::path(uninstall) / L"Spider-Man.exe").wstring();
    return {};
}

// The Epic Games Store build differs from the Steam build Spidy reads.
std::wstring epicGame() {
    const std::wstring data = environment(L"ProgramData");
    std::error_code error;
    if (data.empty())
        return {};
    for (const auto& entry : fs::directory_iterator(fs::path(data) / L"Epic/EpicGamesLauncher/Data/Manifests", error)) {
        if (entry.path().extension() != L".item")
            continue;
        const std::string manifest = readText(entry.path());
        if (manifest.find("Spider-Man") == std::string::npos || manifest.find("Remastered") == std::string::npos)
            continue;
        // JSON, but its quoted keys and values read like VDF's.
        for (const auto& folder : text::vdfValues(manifest, "InstallLocation")) {
            const fs::path exe = fs::path(widen(folder)) / L"Spider-Man.exe";
            if (isFile(exe))
                return exe.wstring();
        }
        return L"?"; // installed, folder unknown
    }
    return {};
}

// What Steam's app manifest says about the game whose Spider-Man.exe is `exe`, when Steam
// installed it there (steamapps\common\<installdir>\Spider-Man.exe).
SteamApp steamAppFor(const fs::path& exe) {
    const fs::path folder = exe.parent_path(), steamapps = folder.parent_path().parent_path();
    const std::string manifest = readText(steamapps / (std::wstring(L"appmanifest_") + kSteamAppId + L".acf"));
    const auto installdir = text::vdfValues(manifest, "installdir");
    return !installdir.empty() && sameFile(widen(installdir.front()), folder.filename().wstring())
               ? text::steamApp(manifest)
               : SteamApp{};
}

} // namespace

std::optional<std::string> sha256(const std::wstring& path) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return std::nullopt;
    std::optional<std::string> result;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::ifstream file(fs::path(path), std::ios::binary);
        std::vector<char> buffer(1 << 20);
        bool ok = static_cast<bool>(file);
        while (ok && file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto got = static_cast<ULONG>(file.gcount());
            if (got && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), got, 0) != 0)
                ok = false;
        }
        unsigned char digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
            static constexpr char hex[] = "0123456789abcdef";
            std::string value;
            for (unsigned char byte : digest) {
                value += hex[byte >> 4];
                value += hex[byte & 15];
            }
            result = value;
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

namespace {

bool processRunning(const wchar_t* name) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!snapshot || !Process32FirstW(snapshot.value, &entry))
        return false;
    do {
        if (_wcsicmp(entry.szExeFile, name) == 0)
            return true;
    } while (Process32NextW(snapshot.value, &entry));
    return false;
}

// --- VR and Windows --------------------------------------------------------

std::string manifestName(const std::wstring& manifest) {
    if (auto label = text::runtimeLabel(narrow(manifest)); !label.empty())
        return label;
    const std::string json = readText(fs::path(manifest), 1 << 16);
    const size_t runtime = json.find("\"runtime\"");
    const size_t name = json.find("\"name\"", runtime == std::string::npos ? 0 : runtime);
    if (name != std::string::npos) {
        const size_t open = json.find('"', json.find(':', name) + 1);
        const size_t close = open == std::string::npos ? open : json.find('"', open + 1);
        if (close != std::string::npos)
            return json.substr(open + 1, close - open - 1);
    }
    return narrow(fs::path(manifest).stem().wstring());
}

std::vector<Runtime> openXrRuntimes() {
    const std::wstring virtualDesktop =
        environment(L"ProgramFiles") + L"\\Virtual Desktop Streamer\\OpenXR\\virtualdesktop-openxr.json";
    const std::wstring active =
        registryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", RRF_SUBKEY_WOW6464KEY);
    std::vector<std::wstring> manifests{virtualDesktop, active};
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1\\AvailableRuntimes", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        wchar_t name[2048];
        for (DWORD i = 0;; ++i) {
            DWORD length = static_cast<DWORD>(std::size(name));
            if (RegEnumValueW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            manifests.emplace_back(name, length);
        }
        RegCloseKey(key);
    }
    std::vector<Runtime> runtimes;
    for (const auto& manifest : manifests) {
        if (manifest.empty() || !isFile(manifest) ||
            std::any_of(runtimes.begin(), runtimes.end(), [&](const Runtime& r) { return sameFile(r.manifest, manifest); }))
            continue;
        runtimes.push_back({manifest, manifestName(manifest), sameFile(manifest, active), sameFile(manifest, virtualDesktop)});
    }
    return runtimes;
}

std::string fileVersion(const std::wstring& path, std::array<int, 4>& fields) {
    DWORD ignored{};
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    std::vector<char> data(size);
    VS_FIXEDFILEINFO* info{};
    UINT length{};
    if (!size || !GetFileVersionInfoW(path.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info)
        return {};
    fields = {static_cast<int>(HIWORD(info->dwFileVersionMS)), static_cast<int>(LOWORD(info->dwFileVersionMS)),
              static_cast<int>(HIWORD(info->dwFileVersionLS)), static_cast<int>(LOWORD(info->dwFileVersionLS))};
    return std::to_string(fields[0]) + "." + std::to_string(fields[1]) + "." + std::to_string(fields[2]);
}

std::wstring settingsPath() {
    const std::wstring appData = knownFolder(FOLDERID_RoamingAppData);
    return appData.empty() ? std::wstring() : (fs::path(appData) / L"Spidy" / L"launcher.ini").wstring();
}

bool signedByMicrosoft(const std::wstring& file) {
    WINTRUST_FILE_INFO info{sizeof(info)};
    info.pcwszFilePath = file.c_str();
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA data{sizeof(data)};
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &info;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    bool microsoft = false;
    if (WinVerifyTrust(nullptr, &action, &data) == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR* signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        CRYPT_PROVIDER_CERT* certificate = signer ? WTHelperGetProvCertFromChain(signer, 0) : nullptr;
        wchar_t name[256]{};
        if (certificate &&
            CertGetNameStringW(certificate->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256) > 1)
            microsoft = wcscmp(name, L"Microsoft Corporation") == 0;
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &data);
    return microsoft;
}

} // namespace

std::string narrow(std::wstring_view value) {
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring widen(std::string_view value) {
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

// --- Settings ------------------------------------------------------------------

Settings loadSettings() {
    Settings settings;
    std::istringstream lines(readText(settingsPath(), 1 << 16));
    std::string line;
    int weight = 0, legacyWeight = 0;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(0, equals), value = line.substr(equals + 1);
        const auto number = [&] { return std::atoi(value.c_str()); };
        auto& o = settings.options;
        // "runtime" (before October 7's automatic runtime) mostly held the launcher's own first pick,
        // Virtual Desktop when installed: such settings start on Automatic.
        if (key == "game") settings.gameExe = widen(value);
        else if (key == "xr_runtime") settings.runtime = value.empty() ? kAutoRuntime : widen(value);
        // web_grab, overlay_webs, body, punch and web_shooter (options before October 8's sixth build) are no
        // longer read: those features are always on, so one switched off there comes back on.
        else if (key == "small_window") o.smallWindow = number() != 0;
        else if (key == "stock_monitor_view") o.stockMonitorView = number() != 0;
        else if (key == "aim_markers") o.aimMarkers = number() != 0;
        else if (key == "air_webs") o.airWebs = number() != 0;
        else if (key == "flips") o.flips = number() != 0;
        else if (key == "trigger_webs") o.triggerWebs = number() != 0;
        else if (key == "stand_on_walls") o.standOnWalls = number() != 0;
        // eye_size (a square size, before October 8) is no longer read: 2048 x 2048 rendered one player's game
        // below the 2496 x 2688 their headset asked for, and it looked blurry.
        else if (key == "render_scale") o.renderScale = spidy::validRenderScale(number()) ? number() : 100;
        else if (key == "swing_speed") o.swingSpeed = std::clamp(number(), 10, 65);
        else if (key == "snap_turn") o.snapTurn = std::clamp(number(), 0, 90);
        else if (key == "smooth_turn") o.smoothTurn = std::clamp(number(), 0, 360);
        else if (key == "haptics") o.haptics = std::clamp(number(), 0, 100);
        else if (key == "screen_size") o.screenSize = std::clamp(number(), 0, 2);
        else if (key == "hud") o.hud = std::clamp(number(), 0, 3);
        else if (key == "flip_speed") o.flipSpeed = std::clamp(number(), 90, 480);
        // "weight" (before October 9) was saved whether chosen or not: its old default, 60, starts on today's.
        else if (key == "weight") legacyWeight = std::clamp(number(), 40, 300);
        else if (key == "weight_percent") weight = std::clamp(number(), 40, 300);
        else if (key == "eye_height_mm") o.eyeHeightMm = number();
        else if (key == "arm_length_mm") o.armLengthMm = number();
        else if (key == "calibration_prompt") o.calibrationPrompt = number() != 0;
        else if (key == "hash_path") settings.hashPath = widen(value);
        else if (key == "hash_size") settings.hashSize = std::strtoull(value.c_str(), nullptr, 10);
        else if (key == "hash_time") settings.hashTime = std::strtoull(value.c_str(), nullptr, 10);
        else if (key == "hash") settings.hashValue = value;
        else if (key == "shortcuts_asked") settings.shortcutsAsked = number() != 0;
        else if (key == "update_check") settings.updateCheck = number() != 0;
    }
    if (weight)
        settings.options.weight = weight;
    else if (legacyWeight && legacyWeight != 60)
        settings.options.weight = legacyWeight;
    // A calibration is both measurements within their ranges, or none.
    if (!text::calibrated(settings.options))
        settings.options.eyeHeightMm = settings.options.armLengthMm = 0;
    return settings;
}

void saveSettings(const Settings& settings) {
    const fs::path path = settingsPath();
    if (path.empty())
        return;
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const auto& o = settings.options;
    file << "game=" << narrow(settings.gameExe) << "\nxr_runtime=" << narrow(settings.runtime)
         << "\nsmall_window=" << o.smallWindow << "\nstock_monitor_view=" << o.stockMonitorView
         << "\naim_markers=" << o.aimMarkers << "\nair_webs=" << o.airWebs << "\nflips=" << o.flips
         << "\ntrigger_webs=" << o.triggerWebs << "\nstand_on_walls=" << o.standOnWalls
         << "\nrender_scale=" << o.renderScale
         << "\nswing_speed=" << o.swingSpeed << "\nweight_percent=" << o.weight << "\nsnap_turn=" << o.snapTurn
         << "\nsmooth_turn=" << o.smoothTurn
         << "\nhaptics=" << o.haptics << "\nscreen_size=" << o.screenSize << "\nhud=" << o.hud
         << "\nflip_speed=" << o.flipSpeed << "\neye_height_mm=" << o.eyeHeightMm
         << "\narm_length_mm=" << o.armLengthMm << "\ncalibration_prompt=" << o.calibrationPrompt
         << "\nhash_path=" << narrow(settings.hashPath)
         << "\nhash_size=" << settings.hashSize << "\nhash_time=" << settings.hashTime
         << "\nhash=" << settings.hashValue << "\nshortcuts_asked=" << settings.shortcutsAsked
         << "\nupdate_check=" << settings.updateCheck << "\n";
}

// --- Scanner -------------------------------------------------------------------

Scanner::~Scanner() {
    if (worker_.joinable())
        worker_.join();
}

void Scanner::start(const Settings& settings) {
    {
        std::lock_guard lock(mutex_);
        if (busy_)
            return;
        busy_ = true;
    }
    if (worker_.joinable())
        worker_.join();
    worker_ = std::thread([this, settings] { run(settings); });
}

Scan Scanner::snapshot() {
    std::lock_guard lock(mutex_);
    return scan_;
}

bool Scanner::busy() {
    std::lock_guard lock(mutex_);
    return busy_;
}

void Scanner::run(Settings settings) {
    Scan scan;
    scan.root = findRoot();
    if (!scan.root.empty()) {
        const fs::path root(scan.root), modules = root / L"build" / L"windows-ninja";
        for (const auto* module : kModules)
            if (!isFile(modules / module))
                scan.missing.push_back(narrow(module));
        scan.writable = folderWritable(root / L"reports");
        const std::string inspect = readText(root / L"tools/inspect_game.py");
        if (auto hash = text::pythonConstant(inspect, "EXPECTED_SHA256"))
            scan.expectedHash = *hash;
        if (auto version = text::pythonConstant(inspect, "EXPECTED_VERSION"))
            scan.expectedVersion = *version;
        const std::string session = readText(root / L"tools/run_game_vr.py");
        if (auto need = text::pythonConstant(session, "VR_COMMIT_MB"))
            scan.neededCommitGb = std::atof(need->c_str()) / 1024;
        if (auto bytes = text::pythonConstant(session, "EYE_COMMIT_BYTES"))
            scan.eyeCommitBytes = std::atof(bytes->c_str());
        scan.python = findPython(scan.root);
        if (!scan.python.empty()) {
            std::string output;
            if (capture(text::quoteArgument(scan.python) + L" -c \"import sys;print('%d.%d.%d'%sys.version_info[:3])\"",
                        scan.root, nullptr, 8000, output) == 0u)
                scan.pythonVersion = trim(output);
        }
    }
    const auto roots = steamRoots();
    if (!roots.empty())
        scan.steam = (fs::path(roots.front()) / L"steam.exe").wstring();
    const std::wstring epic = epicGame();
    scan.epicFound = !epic.empty();
    if (!settings.gameExe.empty() && isFile(settings.gameExe)) {
        scan.gameExe = settings.gameExe;
        scan.gameSource = "chosen by you";
    } else if (auto steam = steamGame(roots); !steam.empty()) {
        scan.gameExe = steam;
        scan.gameSource = "Steam";
    } else if (scan.epicFound && epic != L"?") {
        scan.gameExe = epic;
        scan.gameSource = "Epic Games Store";
    }
    if (!scan.gameExe.empty()) {
        // Before the true path too: that follows a junction the game folder may have been moved through.
        scan.steamApp = steamAppFor(scan.gameExe);
        scan.gameExe = truePath(scan.gameExe);
        if (!scan.steamApp.found)
            scan.steamApp = steamAppFor(scan.gameExe);
        std::array<int, 4> fields{};
        if (!fileVersion(scan.gameExe, fields).empty())
            scan.gameVersion = std::to_string(fields[0]) + '.' + std::to_string(fields[1]) + '.' +
                               std::to_string(fields[2]) + '.' + std::to_string(fields[3]);
    }
    scan.gameRunning = processRunning(L"Spider-Man.exe");
    scan.runtimes = openXrRuntimes();
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    std::array<int, 4> runtimeVersion{};
    scan.vcInstalled = isFile(fs::path(system) / L"vcruntime140.dll") && isFile(fs::path(system) / L"vcruntime140_1.dll") &&
                       isFile(fs::path(system) / L"msvcp140.dll");
    scan.vcVersion = fileVersion((fs::path(system) / L"msvcp140.dll").wstring(), runtimeVersion);
    scan.vcCurrent = scan.vcInstalled && runtimeVersion >= kMinimumRuntime;
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory))
        scan.freeCommitGb = static_cast<double>(memory.ullAvailPageFile) / (1ull << 30);
    scan.build = scan.gameExe.empty() ? Build::unknown : Build::checking;
    scan.done = true;
    {
        std::lock_guard lock(mutex_);
        scan_ = scan;
    }
    // Hashing 120 MB takes a moment; reuse the last result while the file is unchanged.
    if (!scan.gameExe.empty()) {
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        uint64_t size{}, time{};
        if (GetFileAttributesExW(scan.gameExe.c_str(), GetFileExInfoStandard, &attributes)) {
            size = (uint64_t(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
            time = (uint64_t(attributes.ftLastWriteTime.dwHighDateTime) << 32) | attributes.ftLastWriteTime.dwLowDateTime;
        }
        std::optional<std::string> hash;
        if (sameFile(settings.hashPath, scan.gameExe) && settings.hashSize == size && settings.hashTime == time &&
            settings.hashValue.size() == 64)
            hash = settings.hashValue;
        else
            hash = sha256(scan.gameExe);
        std::lock_guard lock(mutex_);
        scan_.hashPath = scan.gameExe;
        scan_.hashSize = size;
        scan_.hashTime = time;
        scan_.hashValue = hash.value_or("");
        scan_.build = !hash ? Build::unreadable
                      : scan.expectedHash.empty() || *hash == scan.expectedHash ? Build::supported
                                                                                 : Build::unsupported;
    }
    std::lock_guard lock(mutex_);
    busy_ = false;
}

// --- Headset check ---------------------------------------------------------------

HeadsetCheck::~HeadsetCheck() {
    if (worker_.joinable())
        worker_.join();
}

void HeadsetCheck::start(const std::wstring& root, const std::wstring& manifest, const std::wstring& python) {
    const bool automatic = manifest == kAutoRuntime;
    {
        std::lock_guard lock(mutex_);
        if (state_ == Outcome::running)
            return;
        state_ = Outcome::running;
        summary_ = automatic ? "Asking the VR runtimes for a headset..." : "Asking the VR runtime for a headset...";
    }
    if (worker_.joinable())
        worker_.join();
    worker_ = std::thread([this, root, manifest, python, automatic] {
        std::string output;
        std::optional<DWORD> code;
        if (automatic) {
            // Up to three runtimes, one of which may start meanwhile (SteamVR takes up to half a minute).
            const std::wstring script = (fs::path(root) / L"tools" / L"xr_runtime.py").wstring();
            code = capture(text::quoteArgument(python) + L" -B -X utf8 " + text::quoteArgument(script) + L" --detect",
                           root, nullptr, 180000, output);
        } else {
            const std::wstring probe = (fs::path(root) / L"build/windows-ninja/spidy_headset_probe.exe").wstring();
            std::wstring block = environmentWith(L"XR_RUNTIME_JSON", manifest);
            code = capture(text::quoteArgument(probe), root, &block, 60000, output);
        }
        const bool found = code == 0u;
        std::lock_guard lock(mutex_);
        state_ = found ? Outcome::ok : Outcome::error;
        summary_ = text::headsetSummary(output, found);
        eye_ = found ? text::recommendedEye(output) : std::nullopt;
    });
}

Outcome HeadsetCheck::state() {
    std::lock_guard lock(mutex_);
    return state_;
}

std::string HeadsetCheck::summary() {
    std::lock_guard lock(mutex_);
    return summary_;
}

std::optional<std::array<uint32_t, 2>> HeadsetCheck::eye() {
    std::lock_guard lock(mutex_);
    return state_ == Outcome::ok ? eye_ : std::nullopt;
}

void HeadsetCheck::reset() {
    std::lock_guard lock(mutex_);
    if (state_ != Outcome::running) {
        state_ = Outcome::idle;
        summary_.clear();
        eye_.reset();
    }
}

// --- Visual C++ runtime ----------------------------------------------------------

RuntimeInstall::~RuntimeInstall() {
    if (worker_.joinable())
        worker_.join();
}

void RuntimeInstall::start() {
    {
        std::lock_guard lock(mutex_);
        if (state_ == Outcome::running)
            return;
        state_ = Outcome::running;
        message_ = "Downloading from Microsoft...";
    }
    if (worker_.joinable())
        worker_.join();
    worker_ = std::thread([this] {
        const auto finish = [this](Outcome state, std::string message) {
            std::lock_guard lock(mutex_);
            state_ = state;
            message_ = std::move(message);
        };
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const std::wstring installer = (fs::path(temp) / L"spidy_vc_redist.x64.exe").wstring();
        if (FAILED(URLDownloadToFileW(nullptr, L"https://aka.ms/vs/17/release/vc_redist.x64.exe", installer.c_str(), 0,
                                      nullptr)))
            return finish(Outcome::error, "Download failed. Get it from aka.ms/vs/17/release/vc_redist.x64.exe");
        if (!signedByMicrosoft(installer)) {
            DeleteFileW(installer.c_str());
            return finish(Outcome::error, "The download was not signed by Microsoft, so it was not run.");
        }
        {
            std::lock_guard lock(mutex_);
            message_ = "Installing (approve the Windows prompt)...";
        }
        SHELLEXECUTEINFOW run{sizeof(run)};
        run.fMask = SEE_MASK_NOCLOSEPROCESS;
        run.lpFile = installer.c_str();
        run.lpParameters = L"/install /passive /norestart";
        run.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&run) || !run.hProcess)
            return finish(Outcome::error, "The installer did not start (the Windows prompt may have been declined).");
        WaitForSingleObject(run.hProcess, INFINITE);
        DWORD code{};
        GetExitCodeProcess(run.hProcess, &code);
        CloseHandle(run.hProcess);
        DeleteFileW(installer.c_str());
        if (code == 0 || code == 1638)
            return finish(Outcome::ok, "Installed.");
        if (code == 3010)
            return finish(Outcome::warning, "Installed. Restart Windows before playing.");
        finish(Outcome::error, "The installer stopped with code " + std::to_string(code) + ".");
    });
}

Outcome RuntimeInstall::state() {
    std::lock_guard lock(mutex_);
    return state_;
}

std::string RuntimeInstall::message() {
    std::lock_guard lock(mutex_);
    return message_;
}

// --- Session ---------------------------------------------------------------------

Session::~Session() {
    if (reader_.joinable())
        reader_.join();
    if (process_)
        CloseHandle(process_);
    if (stopEvent_)
        CloseHandle(stopEvent_);
}

bool Session::start(const std::wstring& python, const std::wstring& root, const std::vector<std::wstring>& args,
                    const std::wstring& report, std::string& error) {
    if (running()) {
        error = "VR is already running.";
        return false;
    }
    if (reader_.joinable())
        reader_.join();
    if (process_)
        CloseHandle(process_);
    if (stopEvent_)
        CloseHandle(stopEvent_);
    process_ = stopEvent_ = nullptr;
    const auto at = std::find(args.begin(), args.end(), std::wstring(L"--stop-event"));
    eventName_ = at != args.end() && at + 1 != args.end() ? *(at + 1) : std::wstring();
    if (!eventName_.empty() && !(stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, eventName_.c_str()))) {
        error = "Could not create the stop signal.";
        return false;
    }
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE readEnd{}, writeEnd{};
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) {
        error = "Could not create the output pipe.";
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    // -B: no __pycache__ in the package; -u: lines arrive as printed; -X utf8: any text prints.
    std::wstring command = text::quoteArgument(python) + L" -B -u -X utf8 " +
                           text::quoteArgument((fs::path(root) / L"tools" / L"run_game_vr.py").wstring());
    for (const auto& arg : args)
        command += L" " + text::quoteArgument(arg);
    process_ = spawn(command, root, nullptr, writeEnd, error);
    CloseHandle(writeEnd);
    if (!process_) {
        CloseHandle(readEnd);
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        lines_.clear();
        running_ = true;
        stopRequested_ = false;
        exit_.reset();
    }
    report_ = report;
    started_ = std::chrono::steady_clock::now();
    reader_ = std::thread([this, readEnd, process = process_] { read(readEnd, process); });
    return true;
}

void Session::read(HANDLE pipe, HANDLE process) {
    std::string pending;
    char buffer[4096];
    const auto flush = [&](bool all) {
        size_t newline;
        std::lock_guard lock(mutex_);
        while ((newline = pending.find('\n')) != std::string::npos || (all && !pending.empty())) {
            std::string line = pending.substr(0, newline == std::string::npos ? pending.size() : newline);
            pending.erase(0, newline == std::string::npos ? pending.size() : newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (lines_.size() >= 6000)
                lines_.erase(lines_.begin(), lines_.begin() + 1000);
            const auto kind = text::classifyLine(line);
            lines_.push_back({std::move(line), kind});
        }
    };
    // Polls rather than blocks: a child that inherited the pipe (Steam, started
    // by the session) must not keep this reader waiting after the session ends.
    for (bool exited = false;;) {
        DWORD available{};
        const bool open = PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr);
        if (open && available) {
            DWORD got{};
            if (ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &got, nullptr))
                pending.append(buffer, got);
            flush(false);
            continue;
        }
        if (exited || !open)
            break;
        exited = WaitForSingleObject(process, 30) == WAIT_OBJECT_0;
    }
    flush(true);
    CloseHandle(pipe);
    WaitForSingleObject(process, INFINITE);
    DWORD code{};
    GetExitCodeProcess(process, &code);
    std::lock_guard lock(mutex_);
    exit_ = code;
    running_ = false;
}

void Session::requestStop() {
    std::lock_guard lock(mutex_);
    if (!running_ || stopRequested_)
        return;
    stopRequested_ = true;
    stopAt_ = std::chrono::steady_clock::now();
    if (stopEvent_)
        SetEvent(stopEvent_);
}

void Session::forceStop() {
    std::lock_guard lock(mutex_);
    if (running_ && process_)
        TerminateProcess(process_, 1);
}

bool Session::running() {
    std::lock_guard lock(mutex_);
    return running_;
}

bool Session::stopRequested() {
    std::lock_guard lock(mutex_);
    return stopRequested_;
}

std::optional<DWORD> Session::exitCode() {
    std::lock_guard lock(mutex_);
    return exit_;
}

size_t Session::lines(size_t from, std::vector<LogLine>& out) {
    std::lock_guard lock(mutex_);
    for (size_t i = from; i < lines_.size(); ++i)
        out.push_back(lines_[i]);
    return lines_.size();
}

void Session::clear() {
    std::lock_guard lock(mutex_);
    if (!running_)
        lines_.clear();
}

// --- Shell -------------------------------------------------------------------------

void openUrl(const char* url) {
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void openPath(const std::wstring& path) {
    std::error_code error;
    fs::create_directories(path, error);
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::optional<std::wstring> browseForGame(HWND owner, const std::wstring& current) {
    IFileOpenDialog* dialog{};
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return std::nullopt;
    const COMDLG_FILTERSPEC filters[] = {{L"Spider-Man.exe", L"Spider-Man.exe"}, {L"Programs", L"*.exe"}};
    dialog->SetFileTypes(2, filters);
    dialog->SetTitle(L"Find Marvel's Spider-Man Remastered (Spider-Man.exe)");
    if (!current.empty()) {
        IShellItem* folder{};
        if (SUCCEEDED(SHCreateItemFromParsingName(fs::path(current).parent_path().c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }
    std::optional<std::wstring> chosen;
    IShellItem* item{};
    if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item))) {
        PWSTR path{};
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            chosen = path;
        CoTaskMemFree(path);
        item->Release();
    }
    dialog->Release();
    return chosen;
}

bool createShortcuts(std::string& error) {
    wchar_t exe[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    int made = 0;
    for (const auto& folder : {knownFolder(FOLDERID_Desktop), knownFolder(FOLDERID_Programs)}) {
        if (folder.empty())
            continue;
        IShellLinkW* link{};
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
            continue;
        link->SetPath(exe);
        link->SetWorkingDirectory(fs::path(exe).parent_path().c_str());
        link->SetDescription(L"Spidy - VR for Marvel's Spider-Man Remastered, by Ilya Mezerowsky");
        link->SetIconLocation(exe, 0);
        IPersistFile* file{};
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
            if (SUCCEEDED(file->Save((fs::path(folder) / L"Spidy VR.lnk").c_str(), TRUE)))
                ++made;
            file->Release();
        }
        link->Release();
    }
    if (!made)
        error = "Windows did not create the shortcuts.";
    return made > 0;
}

std::wstring newReportPath(const std::wstring& root) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t name[64];
    swprintf_s(name, L"game-vr-%04u%02u%02u-%02u%02u%02u.json", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond);
    return (fs::path(root) / L"reports" / name).wstring();
}

bool restartAsAdministrator(std::string& error) {
    wchar_t exe[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    DWORD code = ERROR_FILE_NOT_FOUND;
    if (length && length < std::size(exe)) {
        // main.cpp's --wait-for: the one-at-a-time lock is this launcher's until it has closed.
        const std::wstring arguments = L"--wait-for " + std::to_wstring(GetCurrentProcessId());
        const std::wstring folder = fs::path(exe).parent_path().wstring();
        SHELLEXECUTEINFOW run{sizeof(run)};
        run.lpVerb = L"runas";
        run.lpFile = exe;
        run.lpParameters = arguments.c_str();
        run.lpDirectory = folder.c_str();
        run.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&run))
            return true;
        code = GetLastError();
    }
    error = code == ERROR_CANCELLED ? std::string()
                                    : "Windows did not start Spidy as administrator (error " + std::to_string(code) +
                                          "). Right-click Spidy Launcher and choose Run as administrator.";
    return false;
}

std::wstring newStopEventName() {
    return L"Local\\SpidyLauncherStop-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
}

} // namespace launcher
