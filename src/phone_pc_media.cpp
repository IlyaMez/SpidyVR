#include "spidy/phone_pc_media.hpp"
#include "spidy/phone.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace spidy {
namespace {
using namespace winrt::Windows::Media::Control;
using Manager = GlobalSystemMediaTransportControlsSessionManager;
using PlaybackStatus = GlobalSystemMediaTransportControlsSessionPlaybackStatus;

std::string utf8(std::wstring_view w) {
    const int size = static_cast<int>(w.size());
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, w.data(), size, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(std::max(bytes, 0)), '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.data(), size, s.data(), bytes, nullptr, nullptr);
    return s;
}
// A process's file name, as phoneMediaAppMatches takes it; empty if it
// cannot be read (a process of another user, one gone).
std::string processName(DWORD id) {
    std::string name;
    if (const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id)) {
        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, path, &size))
            name = utf8(std::wstring_view(path, size));
        CloseHandle(process);
    }
    return name;
}
} // namespace

// The thread, and everything Windows' media controls and mixer: COM in the
// multithreaded apartment, where waiting on the controls' answers (get()) is
// allowed.
struct PcMedia::Player {
    enum class Command { toggle, next, previous, volume };
    mutable std::mutex lock;
    Status shown;
    int volumeWanted{};
    std::deque<Command> commands;
    std::atomic<bool> quit{};
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread thread;
    // The thread's own.
    Manager manager{nullptr};
    ULONGLONG managerAt{};
    std::string app; // the session's app as the controls name it

    Player() {
        try {
            if (wake)
                thread = std::thread([this] { run(); });
        } catch (...) {
        }
    }
    ~Player() {
        quit = true;
        if (wake)
            SetEvent(wake);
        if (thread.joinable())
            thread.join();
        if (wake)
            CloseHandle(wake);
    }
    void send(Command c) {
        {
            std::lock_guard guard(lock);
            commands.push_back(c);
        }
        if (wake)
            SetEvent(wake);
    }
    GlobalSystemMediaTransportControlsSession session() {
        // The controls once, and again every 5 s while Windows will not give them.
        if (!manager && GetTickCount64() >= managerAt) {
            managerAt = GetTickCount64() + 5000;
            try {
                manager = Manager::RequestAsync().get();
            } catch (...) {
                manager = nullptr;
            }
        }
        if (!manager)
            return nullptr;
        try {
            return manager.GetCurrentSession();
        } catch (...) {
            return nullptr;
        }
    }
    // The app's audio sessions in Windows' mixer (on the default speakers,
    // where the headset's stream takes the sound from): set to `set` (0 to
    // 1), or with `set` below 0 only read. Their loudest, or -1 with none.
    float mixer(float set) const {
        if (app.empty())
            return -1;
        float loudest = -1;
        IMMDeviceEnumerator* devices{};
        IMMDevice* speakers{};
        IAudioSessionManager2* sessions{};
        IAudioSessionEnumerator* list{};
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&devices))) &&
            SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &speakers)) &&
            SUCCEEDED(speakers->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                         reinterpret_cast<void**>(&sessions))) &&
            SUCCEEDED(sessions->GetSessionEnumerator(&list))) {
            int count{};
            list->GetCount(&count);
            for (int i = 0; i < count; ++i) {
                IAudioSessionControl* control{};
                IAudioSessionControl2* control2{};
                ISimpleAudioVolume* volume{};
                DWORD id{};
                if (SUCCEEDED(list->GetSession(i, &control)) &&
                    SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                                      reinterpret_cast<void**>(&control2))) &&
                    SUCCEEDED(control2->GetProcessId(&id)) && id &&
                    phoneMediaAppMatches(app, processName(id)) &&
                    SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                      reinterpret_cast<void**>(&volume)))) {
                    if (set >= 0)
                        volume->SetMasterVolume(std::clamp(set, 0.f, 1.f), nullptr);
                    float level{};
                    if (SUCCEEDED(volume->GetMasterVolume(&level)))
                        loudest = std::max(loudest, level);
                }
                if (volume)
                    volume->Release();
                if (control2)
                    control2->Release();
                if (control)
                    control->Release();
            }
        }
        if (list)
            list->Release();
        if (sessions)
            sessions->Release();
        if (speakers)
            speakers->Release();
        if (devices)
            devices->Release();
        return loudest;
    }
    // What plays now, for the phone.
    void poll() {
        Status now;
        try {
            if (const auto current = session()) {
                app = utf8(current.SourceAppUserModelId());
                now.session = true;
                now.playing = current.GetPlaybackInfo().PlaybackStatus() == PlaybackStatus::Playing;
                const auto properties = current.TryGetMediaPropertiesAsync().get();
                const auto artist =
                    properties.Artist().empty() ? properties.AlbumArtist() : properties.Artist();
                strncpy_s(now.song, phoneMediaSong(utf8(artist), utf8(properties.Title())).c_str(),
                          _TRUNCATE);
                strncpy_s(now.source, phoneMediaSource(app).c_str(), _TRUNCATE);
            } else {
                app.clear();
            }
        } catch (...) {
            // A session gone between asking for it and reading it: as none.
            app.clear();
            now = {};
        }
        const float level = mixer(-1);
        now.volumeKnown = level >= 0;
        std::lock_guard guard(lock);
        now.volume = now.volumeKnown ? static_cast<int>(std::lround(level * 100)) : shown.volume;
        shown = now;
    }
    void command(Command c) {
        if (c == Command::volume) {
            int percent;
            {
                std::lock_guard guard(lock);
                percent = volumeWanted;
            }
            mixer(percent / 100.f);
            return;
        }
        try {
            if (const auto current = session()) {
                if (c == Command::toggle)
                    current.TryTogglePlayPauseAsync().get();
                else if (c == Command::next)
                    current.TrySkipNextAsync().get();
                else
                    current.TrySkipPreviousAsync().get();
            }
        } catch (...) {
        }
    }
    void run() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (!quit) {
            std::deque<Command> todo;
            {
                std::lock_guard guard(lock);
                todo.swap(commands);
            }
            for (const auto c : todo)
                if (!quit)
                    command(c);
            if (quit)
                break;
            poll();
            // Twice a second, and at once after a command.
            WaitForSingleObject(wake, 500);
        }
        manager = nullptr;
        if (SUCCEEDED(com))
            CoUninitialize();
    }
};

PcMedia::PcMedia() : player_(std::make_unique<Player>()) {}

PcMedia::~PcMedia() = default;

PcMedia::Status PcMedia::status() const {
    std::lock_guard guard(player_->lock);
    return player_->shown;
}

void PcMedia::toggle() {
    player_->send(Player::Command::toggle);
}

void PcMedia::next() {
    player_->send(Player::Command::next);
}

void PcMedia::previous() {
    player_->send(Player::Command::previous);
}

void PcMedia::setVolume(int percent) {
    {
        std::lock_guard guard(player_->lock);
        player_->volumeWanted = player_->shown.volume = std::clamp(percent, 0, 100);
    }
    player_->send(Player::Command::volume);
}
} // namespace spidy
