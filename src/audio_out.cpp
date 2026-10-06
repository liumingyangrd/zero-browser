#include "audio_out.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audioclient.h>
#include <audiosessiontypes.h>
#include <mmdeviceapi.h>

#include <cstring>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

namespace zb {


static const GUID kClsidMMDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID kIidIMMDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID kIidIAudioClient = {
    0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const GUID kIidIAudioRenderClient = {
    0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};

template <typename T>
static void ReleaseOne(T** p) {
    if (*p) {
        (*p)->Release();
        *p = nullptr;
    }
}

struct AudioOutput::Impl {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    HANDLE event = nullptr;
    UINT32 buffer_frames = 0;
    bool started = false;

    ~Impl() { Close(); }

    void Close() {
        if (client && started) {
            client->Stop();
            started = false;
        }
        ReleaseOne(&render);
        ReleaseOne(&client);
        ReleaseOne(&device);
        ReleaseOne(&enumerator);
        if (event) {
            CloseHandle(event);
            event = nullptr;
        }
    }
};

AudioOutput::AudioOutput() : impl_(new Impl()) {}
AudioOutput::~AudioOutput() = default;

bool AudioOutput::Init() {
    if (!impl_) return false;
    Impl& d = *impl_;
    HRESULT hr = CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                  kIidIMMDeviceEnumerator,
                                  (void**)&d.enumerator);
    if (FAILED(hr) || !d.enumerator) return false;
    hr = d.enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &d.device);
    if (FAILED(hr) || !d.device) return false;
    hr = d.device->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr,
                            (void**)&d.client);
    if (FAILED(hr) || !d.client) return false;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = 48000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    d.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!d.event) return false;

    hr = d.client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        10000000, 0, &fmt, nullptr);
    if (FAILED(hr)) return false;
    d.client->SetEventHandle(d.event);
    d.client->GetBufferSize(&d.buffer_frames);
    hr = d.client->GetService(kIidIAudioRenderClient, (void**)&d.render);
    if (FAILED(hr) || !d.render) return false;
    d.client->Start();
    d.started = true;
    return true;
}

bool AudioOutput::ok() const {
    return impl_ && impl_->client && impl_->render;
}

void AudioOutput::Write(const uint8_t* data, uint32_t frames) {
    if (!impl_ || !data || frames == 0) return;
    Impl& d = *impl_;
    if (!d.client || !d.render) return;
    const uint32_t frame_bytes = 4;  
    while (frames > 0) {
        UINT32 padding = 0;
        if (FAILED(d.client->GetCurrentPadding(&padding))) return;
        UINT32 available = d.buffer_frames > padding ? d.buffer_frames - padding : 0;
        UINT32 n = frames < available ? frames : available;
        if (n == 0) {
            Sleep(2);
            continue;
        }
        BYTE* dst = nullptr;
        if (FAILED(d.render->GetBuffer(n, &dst)) || !dst) return;
        std::memcpy(dst, data, (size_t)n * frame_bytes);
        d.render->ReleaseBuffer(n, 0);
        data += (size_t)n * frame_bytes;
        frames -= n;
    }
}

void AudioOutput::Close() {
    if (impl_) impl_->Close();
}

}  