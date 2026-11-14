// A few sounds at once (voice_pool.hpp): the queue, the voices, the refusals, the stuck voice and the end.
#include "spidy/voice_pool.hpp"
#include "spidy/game_fp.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <windows.h>
#include <mmsystem.h>

namespace spidy::voice_pool {
namespace {
constexpr unsigned maxVoices = 8, queueSize = 8, headerBytes = 44;
constexpr size_t longestSound = size_t{1} << 22;
// A voice left unused this long is closed; a sound that waited this long for the thread is dropped; a voice
// that has not finished this long after it should have is stuck; after trouble with the voices the sounds go
// to PlaySound for this long.
constexpr ULONGLONG idleCloseMs = 12000, staleMs = 700, graceMs = 700, cooldownMs = 3000;
// How many of the first sounds, and of the refusals, are written to the log one by one.
constexpr unsigned loggedOneByOne = 12, loggedTrouble = 8;

// A sound waiting for the thread: the WAV in the caller's memory (kept valid by the caller), its whole length
// and when play() queued it.
struct Request {
    const unsigned char* wav{};
    DWORD bytes{};
    ULONGLONG at{};
};
// The thread's voices, and nobody else's: a handle, the buffer it plays from, and when.
struct Voice {
    HWAVEOUT handle{};
    WAVEHDR header{};
    bool prepared{}, playing{};
    ULONGLONG started{}, endsAt{}, lastUsed{};
    unsigned serial{};
};
struct Worker {
    Voice voices[maxVoices]{};
    unsigned limit{}, rate{}, failureStreak{};
    ULONGLONG voicesOffUntil{};
};

// Under `lock`: the queue, whether the pool is open, its settings, the thread and the event that wakes it.
// (`stopping` is close() at work; the thread reads it between its steps, without the lock, to leave a slow
// device at once.)
SRWLOCK lock = SRWLOCK_INIT;
Request queue[queueSize];
unsigned queued{};
bool enabled{};
std::atomic<bool> stopping{};
unsigned wantedRate{}, wantedVoices{};
HANDLE wake{}, worker{};
// The counts (Stats), the thread's to write.
std::atomic<unsigned> requestedCount{}, startedCount{}, finishedCount{}, overlappedCount{}, fallbackCount{},
    failureCount{}, stuckCount{}, droppedCount{}, openedCount{}, idleClosedCount{};

void say(const char* text) {
    spidy::game_fp::note(text);
}
const char* describe(MMRESULT code) {
    switch (code) {
    case MMSYSERR_NOERROR:
        return "ok";
    case MMSYSERR_BADDEVICEID:
        return "no such device";
    case MMSYSERR_ALLOCATED:
        return "the device is in use";
    case MMSYSERR_INVALHANDLE:
        return "the handle is no longer valid";
    case MMSYSERR_NODRIVER:
        return "no driver (the device is gone?)";
    case MMSYSERR_NOMEM:
        return "out of memory";
    case WAVERR_BADFORMAT:
        return "the format is not supported";
    case WAVERR_STILLPLAYING:
        return "still playing";
    case WAVERR_UNPREPARED:
        return "the header is not prepared";
    default:
        return "an unnamed code";
    }
}
// The device refused a step: counted, the first few written to the log; three in a row and the voices rest
// for a few seconds (the sounds go to PlaySound meanwhile) so a device that keeps refusing is not asked for
// every sound.
void trouble(Worker& w, ULONGLONG now, const char* step, MMRESULT code) {
    const unsigned n = failureCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= loggedTrouble) {
        char line[200];
        std::snprintf(line, sizeof(line), "block sounds: %s was refused by the audio device: %s (code %u)",
                      step, describe(code), static_cast<unsigned>(code));
        say(line);
    }
    if (++w.failureStreak >= 3) {
        w.voicesOffUntil = now + cooldownMs;
        w.failureStreak = 0;
        if (n <= loggedTrouble + 3)
            say("block sounds: the voices rest for a few seconds (PlaySound plays meanwhile)");
    }
}

// The voice's buffer is taken back (the sound stopped, if asked); the handle stays open.
void takeBack(Voice& v, bool stop) {
    if (!v.handle)
        return;
    if (stop && v.playing)
        waveOutReset(v.handle);
    if (v.prepared) {
        waveOutUnprepareHeader(v.handle, &v.header, sizeof(WAVEHDR));
        v.prepared = false;
    }
    v.playing = false;
}
// The voice is stopped and its handle closed.
void shut(Voice& v) {
    if (v.handle) {
        takeBack(v, true);
        waveOutClose(v.handle);
    }
    v = Voice{};
}
bool openVoice(Worker& w, Voice& v, ULONGLONG now) {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = w.rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = w.rate * 2;
    HWAVEOUT handle{};
    const MMRESULT code = waveOutOpen(&handle, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (code != MMSYSERR_NOERROR) {
        trouble(w, now, "waveOutOpen", code);
        return false;
    }
    v = Voice{};
    v.handle = handle;
    v.lastUsed = now;
    openedCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}
unsigned playingNow(const Worker& w) {
    unsigned n = 0;
    for (unsigned i = 0; i < w.limit; ++i)
        n += w.voices[i].playing ? 1u : 0u;
    return n;
}

// Starts a sound on a voice: a free open one, else a new one, else (all busy) the one that began longest ago
// is cut. False when the device would not (the caller plays the sound with PlaySound).
bool launch(Worker& w, const Request& r, ULONGLONG now) {
    if (now < w.voicesOffUntil)
        return false;
    const DWORD data = r.bytes - headerBytes;
    for (int attempt = 0; attempt < 2; ++attempt) {
        Voice* pick = nullptr;
        const char* kind = "warm";
        // (The second time, a voice that was open is not trusted: it may be what failed.)
        for (unsigned i = 0; attempt == 0 && i < w.limit && !pick; ++i)
            if (w.voices[i].handle && !w.voices[i].playing)
                pick = &w.voices[i];
        for (unsigned i = 0; i < w.limit && !pick; ++i)
            if (!w.voices[i].handle) {
                if (!openVoice(w, w.voices[i], now))
                    return false;
                pick = &w.voices[i];
                kind = "new";
            }
        if (!pick) {
            pick = &w.voices[0];
            for (unsigned i = 1; i < w.limit; ++i)
                if (w.voices[i].started < pick->started)
                    pick = &w.voices[i];
            takeBack(*pick, true);
            kind = "cut the oldest";
        }
        const bool over = playingNow(w) > 0;
        pick->header = WAVEHDR{};
        pick->header.lpData = const_cast<LPSTR>(reinterpret_cast<const char*>(r.wav + headerBytes));
        pick->header.dwBufferLength = data;
        const char* step = "waveOutPrepareHeader";
        MMRESULT code = waveOutPrepareHeader(pick->handle, &pick->header, sizeof(WAVEHDR));
        if (code == MMSYSERR_NOERROR) {
            pick->prepared = true;
            step = "waveOutWrite";
            code = waveOutWrite(pick->handle, &pick->header, sizeof(WAVEHDR));
        }
        if (code != MMSYSERR_NOERROR) {
            trouble(w, now, step, code);
            shut(*pick); // it may be dead: the second time a voice is opened new
            continue;
        }
        const ULONGLONG began = GetTickCount64();
        pick->playing = true;
        pick->started = pick->lastUsed = began;
        pick->endsAt = began + static_cast<ULONGLONG>(data) * 1000 / (static_cast<ULONGLONG>(w.rate) * 2);
        pick->serial = startedCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (over)
            overlappedCount.fetch_add(1, std::memory_order_relaxed);
        w.failureStreak = 0;
        if (pick->serial <= loggedOneByOne) {
            char line[200];
            std::snprintf(line, sizeof(line),
                          "block sound %u: started (%s voice, %u going, device took %llu ms, %llu ms long)",
                          pick->serial, kind, playingNow(w), static_cast<unsigned long long>(began - r.at),
                          static_cast<unsigned long long>(pick->endsAt - began));
            say(line);
        }
        return true;
    }
    return false;
}
// The sound no voice would play: the system's own player, one sound at a time.
void fallBack(const Request& r) {
    const unsigned n = fallbackCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= loggedTrouble)
        say("block sounds: a sound was played with PlaySound (no voice would)");
    PlaySoundW(reinterpret_cast<LPCWSTR>(r.wav), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
// The voices that finished are taken back; one that should have finished long ago is stuck, and closed with
// every voice that sat open beside it (the device may have changed under them all).
void reap(Worker& w, ULONGLONG now) {
    bool stuck = false;
    for (unsigned i = 0; i < w.limit; ++i) {
        Voice& v = w.voices[i];
        if (!v.playing)
            continue;
        const DWORD flags = *reinterpret_cast<const volatile DWORD*>(&v.header.dwFlags);
        if (flags & WHDR_DONE) {
            takeBack(v, false);
            v.lastUsed = now;
            finishedCount.fetch_add(1, std::memory_order_relaxed);
            if (v.serial <= loggedOneByOne) {
                char line[200];
                std::snprintf(line, sizeof(line), "block sound %u: finished after %llu ms", v.serial,
                              static_cast<unsigned long long>(now - v.started));
                say(line);
            }
        } else if (now > v.endsAt + graceMs) {
            const unsigned n = stuckCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= loggedTrouble) {
                char line[200];
                std::snprintf(line, sizeof(line),
                              "block sound %u: not finished %llu ms after it should have, voice closed",
                              v.serial, static_cast<unsigned long long>(now - v.endsAt));
                say(line);
            }
            shut(v);
            stuck = true;
        }
    }
    if (stuck) {
        for (unsigned i = 0; i < w.limit; ++i)
            if (!w.voices[i].playing)
                shut(w.voices[i]);
        w.voicesOffUntil = now + cooldownMs;
    }
}
// Voices that sat unused for a while are closed: nothing is held open between fights.
void closeIdle(Worker& w, ULONGLONG now) {
    for (unsigned i = 0; i < w.limit; ++i) {
        Voice& v = w.voices[i];
        if (v.handle && !v.playing && now - v.lastUsed > idleCloseMs) {
            shut(v);
            idleClosedCount.fetch_add(1, std::memory_order_relaxed);
        }
    }
}
// After a sound started with nothing else waiting: a spare voice is opened, so the next blow's sound, thrown
// while this one still goes, does not wait for the device to open one.
void keepSpare(Worker& w, ULONGLONG now) {
    if (now < w.voicesOffUntil)
        return;
    unsigned open = 0, free = 0;
    for (unsigned i = 0; i < w.limit; ++i)
        if (w.voices[i].handle) {
            ++open;
            free += w.voices[i].playing ? 0u : 1u;
        }
    if (free > 0 || open >= w.limit)
        return;
    for (unsigned i = 0; i < w.limit; ++i)
        if (!w.voices[i].handle) {
            openVoice(w, w.voices[i], now);
            return;
        }
}

DWORD WINAPI run(void*) {
    Worker w;
    AcquireSRWLockShared(&lock);
    w.rate = wantedRate;
    w.limit = wantedVoices < 1 ? 1u : wantedVoices > maxVoices ? maxVoices : wantedVoices;
    ReleaseSRWLockShared(&lock);
    for (;;) {
        Request batch[queueSize];
        unsigned count = 0;
        bool stop = false;
        AcquireSRWLockExclusive(&lock);
        count = queued;
        std::memcpy(batch, queue, sizeof(Request) * count);
        queued = 0;
        stop = stopping.load(std::memory_order_acquire);
        ReleaseSRWLockExclusive(&lock);
        if (stop)
            break;
        ULONGLONG now = GetTickCount64();
        reap(w, now);
        bool started = false;
        for (unsigned i = 0; i < count && !stopping.load(std::memory_order_acquire); ++i) {
            now = GetTickCount64();
            if (now - batch[i].at > staleMs) {
                droppedCount.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (launch(w, batch[i], now))
                started = true;
            else
                fallBack(batch[i]);
        }
        now = GetTickCount64();
        if (started && !stopping.load(std::memory_order_acquire))
            keepSpare(w, now);
        closeIdle(w, now);
        bool anyOpen = false, anyPlaying = false;
        for (unsigned i = 0; i < w.limit; ++i) {
            anyOpen = anyOpen || w.voices[i].handle != nullptr;
            anyPlaying = anyPlaying || w.voices[i].playing;
        }
        // Playing: look for the end of the sounds often. Open and idle: look for the time to close them now
        // and then. Neither: sleep till a sound or the end comes.
        WaitForSingleObject(wake, anyPlaying ? 15 : anyOpen ? 500 : INFINITE);
    }
    for (unsigned i = 0; i < maxVoices; ++i)
        shut(w.voices[i]);
    return 0;
}
} // namespace

bool open(unsigned rate, unsigned voices) {
    if (rate < 8000 || rate > 96000)
        return false;
    AcquireSRWLockExclusive(&lock);
    // A thread close() gave up on (stuck in a call to the device): if it has ended since, the pool may open
    // again.
    if (!enabled && worker && WaitForSingleObject(worker, 0) == WAIT_OBJECT_0) {
        CloseHandle(worker);
        worker = nullptr;
        stopping.store(false, std::memory_order_release);
        queued = 0;
    }
    if (!enabled && !worker) {
        enabled = true;
        stopping.store(false, std::memory_order_release);
        queued = 0;
        wantedRate = rate;
        wantedVoices = voices < 1 ? 1u : voices > maxVoices ? maxVoices : voices;
    }
    const bool ok = enabled;
    ReleaseSRWLockExclusive(&lock);
    return ok;
}

bool play(const void* wav, size_t bytes) {
    const auto* b = static_cast<const unsigned char*>(wav);
    if (!b || bytes <= headerBytes || bytes > longestSound)
        return false;
    // The WAV impactWave writes: PCM, mono, 16 bits, a 16-byte format chunk and the data chunk right after
    // it.
    const auto u16 = [&](size_t at) { return static_cast<unsigned>(b[at] | b[at + 1] << 8); };
    const auto u32 = [&](size_t at) {
        return static_cast<DWORD>(b[at]) | static_cast<DWORD>(b[at + 1]) << 8 |
               static_cast<DWORD>(b[at + 2]) << 16 | static_cast<DWORD>(b[at + 3]) << 24;
    };
    if (std::memcmp(b, "RIFF", 4) || std::memcmp(b + 8, "WAVEfmt ", 8) || std::memcmp(b + 36, "data", 4) ||
        u32(16) != 16 || u16(20) != 1 || u16(22) != 1 || u16(34) != 16)
        return false;
    const DWORD rate = u32(24), data = u32(40) & ~DWORD{1};
    if (data < 2 || data > bytes - headerBytes)
        return false;
    bool queuedIt = false;
    AcquireSRWLockExclusive(&lock);
    if (enabled && !stopping.load(std::memory_order_acquire) && rate == wantedRate) {
        if (!worker) {
            if (!wake)
                wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (wake)
                worker = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
            if (worker)
                say("block sounds: the pool's thread started (voices open when a sound needs one)");
        }
        if (worker) {
            if (queued == queueSize) {
                std::memmove(queue, queue + 1, sizeof(Request) * (queueSize - 1));
                --queued;
                droppedCount.fetch_add(1, std::memory_order_relaxed);
            }
            queue[queued++] = Request{b, headerBytes + data, GetTickCount64()};
            requestedCount.fetch_add(1, std::memory_order_relaxed);
            queuedIt = true;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (queuedIt)
        SetEvent(wake);
    return queuedIt;
}

void close() {
    HANDLE joining{};
    AcquireSRWLockExclusive(&lock);
    if (!enabled && !worker) {
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    enabled = false;
    stopping.store(true, std::memory_order_release);
    joining = worker;
    ReleaseSRWLockExclusive(&lock);
    bool ended = true;
    if (joining) {
        SetEvent(wake);
        ended = WaitForSingleObject(joining, 2500) == WAIT_OBJECT_0;
        if (ended)
            CloseHandle(joining);
    }
    if (ended) {
        AcquireSRWLockExclusive(&lock);
        worker = nullptr;
        stopping.store(false, std::memory_order_release);
        queued = 0;
        ReleaseSRWLockExclusive(&lock);
    } else {
        say("block sounds: the pool's thread did not end in time (it is left; PlaySound plays from here on)");
    }
    const Stats s = stats();
    char line[200];
    std::snprintf(line, sizeof(line),
                  "block sounds: pool closed - %u asked for, %u started (%u over another), %u finished, %u "
                  "by PlaySound, "
                  "%u refused, %u stuck, %u dropped, %u voices opened, %u closed idle",
                  s.requested, s.started, s.overlapped, s.finished, s.fallbacks, s.failures, s.stuck,
                  s.dropped, s.opened, s.idleClosed);
    say(line);
}

bool isOpen() {
    AcquireSRWLockShared(&lock);
    const bool on = enabled;
    ReleaseSRWLockShared(&lock);
    return on;
}

Stats stats() {
    Stats s;
    s.requested = requestedCount.load(std::memory_order_relaxed);
    s.started = startedCount.load(std::memory_order_relaxed);
    s.finished = finishedCount.load(std::memory_order_relaxed);
    s.overlapped = overlappedCount.load(std::memory_order_relaxed);
    s.fallbacks = fallbackCount.load(std::memory_order_relaxed);
    s.failures = failureCount.load(std::memory_order_relaxed);
    s.stuck = stuckCount.load(std::memory_order_relaxed);
    s.dropped = droppedCount.load(std::memory_order_relaxed);
    s.opened = openedCount.load(std::memory_order_relaxed);
    s.idleClosed = idleClosedCount.load(std::memory_order_relaxed);
    return s;
}
} // namespace spidy::voice_pool
