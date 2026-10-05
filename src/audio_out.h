#pragma once

#include <cstdint>
#include <memory>

namespace zb {

// WASAPI render output. Kept in its own translation unit because MinGW's
// audioclient headers and Media Foundation headers conflict on ksmedia/ddraw.
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    bool Init();
    bool ok() const;
    void Write(const uint8_t* data, uint32_t frames);
    void Close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace zb