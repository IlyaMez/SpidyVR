#pragma once
#include <memory>

// The phone's MUSIC with PC MEDIA on (vr_settings.hpp): what the PC itself
// plays (Spotify's app, a browser, any player Windows' media keys work), not
// the Music folder's songs (phone_music.hpp). Windows' media controls (the
// GlobalSystemMediaTransportControls, as the volume flyout's player) name the
// song and its app and take play, pause, next and previous; the app's own
// volume in Windows' mixer (its audio sessions) is the phone's volume. Nothing
// to sign in to, no Spotify Premium; a playlist is picked on the PC. Each call
// returns at once: the controls answer on a thread of their own, and what
// plays shows in status() a moment later.
namespace spidy {
class PcMedia {
  public:
    struct Status {
        // playing: a session the controls name, playing (not paused).
        bool session{}, playing{};
        // volumeKnown: the app has an audio session in Windows' mixer, whose
        // volume this is (a browser that names itself apart from its process,
        // as Firefox does, has none to find).
        bool volumeKnown{};
        int volume{};      // percent
        char song[64]{};   // ARTIST - TITLE (phoneMediaSong)
        char source[24]{}; // its app (phoneMediaSource)
    };
    PcMedia();
    ~PcMedia();
    PcMedia(const PcMedia&) = delete;
    PcMedia& operator=(const PcMedia&) = delete;
    Status status() const;
    // Plays, or pauses.
    void toggle();
    void next();
    void previous();
    // Percent, 0 to 100: the app's volume in Windows' mixer.
    void setVolume(int percent);

  private:
    struct Player;
    std::unique_ptr<Player> player_;
};
} // namespace spidy
