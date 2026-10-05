#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zb {

struct MediaInfo {
    int width = 0;
    int height = 0;
    double duration = 0.0;
    bool has_video = false;
    bool has_audio = false;
    bool ready = false;
    bool failed = false;
    std::string error;
};

// Video/audio playback built on Windows Media Foundation for decode and
// WASAPI for audio output. Layout, controls, timeline and frame painting stay
// in the self-built browser engine.
class MediaPlayer {
public:
    MediaPlayer();
    ~MediaPlayer();

    void Open(const std::string& url, bool autoplay, bool loop, bool muted);
    void Close();

    void Play();
    void Pause();
    void TogglePlay();
    bool IsPlaying() const;

    bool IsReady() const;
    bool Failed() const;
    std::string Error() const;

    void Seek(double seconds);
    double Position() const;
    double Duration() const;
    void SetMuted(bool muted);
    bool Muted() const;

    // Called from the UI timer. Returns true when a new frame is available.
    bool Update();
    bool CopyFrame(std::vector<uint8_t>* bgra, int* width, int* height);

    MediaInfo Info() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace zb