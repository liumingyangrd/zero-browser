#pragma once

#include <cstdint>
#include <memory>

namespace zb {



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

}  