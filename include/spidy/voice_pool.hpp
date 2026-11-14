#pragma once
#include <cstddef>

// A few sounds at once. PlaySound plays one sound at a time: every new call cuts the one before, so a run of
// punches would leave silence after the first few. Voices opened once and held for the whole session are no
// answer either: a device that comes and goes while the headset starts (the Oculus audio device
// re-initializes twice then) leaves the handles opened at that moment dead, and nothing says so.
//
// So a thread of the pool's own does all of it, and nothing is held for the whole session:
//   - play() only queues the sound (it never waits for the audio device) and wakes the thread;
//   - the thread opens a voice when a sound needs one (up to the number of voices wanted at once; a sound
//   that would
//     be one too many cuts the one that began longest ago), starts the sound on it, takes the voice back when
//     the sound is over and keeps it open for a few seconds for the next one, then closes it;
//   - a voice that has not finished a while after it should have is stuck: it is closed (the next sound opens
//   a new
//     one), and so is anything the device refuses;
//   - a sound no voice can play is played with PlaySound from the thread, so a refusing device costs
//     the overlap, not the sound;
//   - the session log (game_fp::note) says what happened: the first sounds one by one (which voice, how long
//   the device
//     took, how long the sound ran), every refusal with the code the device gave, and counts.
//
// The sounds are whole WAV files in memory (impact_sound.hpp's impactWave: 16-bit mono PCM) at the rate the
// pool was opened for. Windows only (winmm).
namespace spidy::voice_pool {
// Allows up to `voices` (1..8) sounds at once, all 16-bit mono PCM at `rate` Hz. Opens nothing: the audio
// device is not touched until a sound plays. True when the settings are good. Calling it again with the pool
// open does nothing.
bool open(unsigned rate, unsigned voices = 4);
// Starts a sound: a whole WAV file ("RIFF", the 44-byte header impactWave writes, then the samples) at the
// rate given to open(). The memory must stay valid until the sound has ended or close() has returned (the
// mod's sounds are static data). False when the pool is not open or the sound is not as it should be; the
// caller then plays it some other way. True means the sound is queued: the pool's thread plays it, with
// PlaySound if no voice will.
bool play(const void* wav, size_t bytes);
// Stops every sound, ends the thread (waiting for it up to two and a half seconds), closes the voices and
// writes the counts to the session log. open() may be called again.
void close();
// open() was called and close() has not been.
bool isOpen();
// What the pool has done since the module was loaded.
struct Stats {
    unsigned requested{};  // sounds play() queued
    unsigned started{};    // ... started on a voice
    unsigned finished{};   // ... of those that ended by themselves
    unsigned overlapped{}; // ... started while another sound was still going
    unsigned fallbacks{};  // ... played with PlaySound because no voice would
    unsigned failures{};   // refusals by the device (an open, a prepare or a write)
    unsigned stuck{};      // voices that never finished and were closed
    unsigned dropped{};    // sounds dropped: the thread was too late for them, or the queue was full
    unsigned opened{};     // voices opened
    unsigned idleClosed{}; // voices closed after a while unused
};
Stats stats();
} // namespace spidy::voice_pool
