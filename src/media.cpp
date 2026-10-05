#include "media.h"

#include "audio_out.h"
#include "common.h"
#include "network.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef ZB_MEDIA_DEBUG
#define MDBG(...) std::fprintf(stderr, __VA_ARGS__)
#else
#define MDBG(...) ((void)0)
#endif

namespace zb {

namespace {

std::wstring U8ToWideLocal(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

bool IsHttpUrl(const std::string& url) {
    return StartsWith(Lower(url), "http://") || StartsWith(Lower(url), "https://");
}

std::string FileUrlToPath(const std::string& url) {
    std::string path = url;
    if (StartsWith(Lower(path), "file://")) path = path.substr(7);
    if (path.size() > 2 && (path[0] == '/' || path[0] == '\\') && path[2] == ':') {
        path = path.substr(1);
    }
    for (char& c : path) {
        if (c == '/') c = '\\';
    }
    return path;
}

long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
}

void EnsureMediaFoundation() {
    static std::once_flag once;
    std::call_once(once, [] { MFStartup(MF_VERSION, MFSTARTUP_FULL); });
}

template <typename T>
void SafeRelease(T** p) {
    if (*p) {
        (*p)->Release();
        *p = nullptr;
    }
}

bool CreateByteStream(const std::string& url, IMFByteStream** out,
                      std::string* error) {
    *out = nullptr;
    if (IsHttpUrl(url)) {
        FetchResult res;
        FetchBinaryWithCookies(url, &res, 60000);
        if (res.html.empty()) {
            *error = res.error.empty() ? "视频下载失败" : res.error;
            return false;
        }
        IMFByteStream* stream = nullptr;
        HRESULT hr = MFCreateTempFile(MF_ACCESSMODE_READWRITE,
                                      MF_OPENMODE_DELETE_IF_EXIST,
                                      MF_FILEFLAGS_NONE, &stream);
        if (FAILED(hr) || !stream) {
            *error = "无法创建媒体临时文件";
            return false;
        }
        ULONG written = 0;
        hr = stream->Write((const BYTE*)res.html.data(),
                           (ULONG)res.html.size(), &written);
        if (FAILED(hr) || written != res.html.size()) {
            stream->Release();
            *error = "写入媒体临时文件失败";
            return false;
        }
        stream->SetCurrentPosition(0);
        *out = stream;
        return true;
    }

    std::string path = StartsWith(Lower(url), "file://") ? FileUrlToPath(url) : url;
    std::wstring wpath = U8ToWideLocal(path);
    HRESULT hr = MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST,
                              MF_FILEFLAGS_NONE, wpath.c_str(), out);
    if (FAILED(hr) || !*out) {
        *error = "无法打开媒体文件";
        return false;
    }
    return true;
}

}  // namespace

struct MediaPlayer::Impl {
    std::string url;
    std::atomic<bool> stop{false};
    std::atomic<bool> playing{false};
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> muted{false};
    std::atomic<bool> loop{false};
    std::atomic<bool> frame_dirty{false};
    std::atomic<bool> has_audio{false};
    // 播放时钟。这里刻意不用 std::atomic<double>：在 32 位 MinGW + -O2 下实测
    // 出现过跨线程写入不可见（表现为进度条跳转不生效、Seek 后位置不更新）。
    // 改用互斥量保护普通 double，行为完全确定，代价可以忽略。
    mutable std::mutex clock_mtx;
    double position = 0.0;
    double base_pos = 0.0;
    double duration = 0.0;
    double seek_request = -1.0;
    std::atomic<long long> start_ms{0};
    std::atomic<int> width{0};
    std::atomic<int> height{0};

    std::mutex mtx;
    std::vector<uint8_t> frame;
    std::string error;

    std::thread worker;
    AudioOutput audio;

    double GetPosition() const {
        std::lock_guard<std::mutex> lock(clock_mtx);
        return position;
    }
    void SetPosition(double value) {
        std::lock_guard<std::mutex> lock(clock_mtx);
        position = value;
    }
    double GetDuration() const {
        std::lock_guard<std::mutex> lock(clock_mtx);
        return duration;
    }
    void SetDuration(double value) {
        std::lock_guard<std::mutex> lock(clock_mtx);
        duration = value;
    }
    double TakeSeek() {
        std::lock_guard<std::mutex> lock(clock_mtx);
        double value = seek_request;
        seek_request = -1.0;
        return value;
    }
    void RequestSeek(double value) {
        std::lock_guard<std::mutex> lock(clock_mtx);
        seek_request = value;
    }
    // 把播放位置直接拉到 value（跳转或重置用）。
    void ResetClock(double value) {
        std::lock_guard<std::mutex> lock(clock_mtx);
        position = value;
        base_pos = value;
        start_ms = NowMs();
    }
    void ResetAll() {
        std::lock_guard<std::mutex> lock(clock_mtx);
        position = 0.0;
        base_pos = 0.0;
        duration = 0.0;
        seek_request = -1.0;
        start_ms = NowMs();
    }
    void Fail(const std::string& message) {
        std::lock_guard<std::mutex> lock(mtx);
        error = message;
        failed = true;
        ready = true;
    }

    double PositionNow() const {
        std::lock_guard<std::mutex> lock(clock_mtx);
        if (!playing.load()) return position;
        double elapsed = (NowMs() - start_ms.load()) / 1000.0;
        return base_pos + elapsed;
    }

    void StartClock() {
        std::lock_guard<std::mutex> lock(clock_mtx);
        base_pos = position;
        start_ms = NowMs();
    }

    void Run();
};

void MediaPlayer::Impl::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    EnsureMediaFoundation();
    MDBG("media: run url=%s\n", url.c_str());

    IMFByteStream* stream = nullptr;
    IMFAttributes* attrs = nullptr;
    IMFSourceReader* reader = nullptr;
    std::string error;

    if (!CreateByteStream(url, &stream, &error)) {
        MDBG("media: byte stream failed: %s\n", error.c_str());
        Fail(error);
        CoUninitialize();
        return;
    }
    MDBG("media: byte stream ok\n");

    MFCreateAttributes(&attrs, 2);
    if (attrs) {
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    HRESULT hr = MFCreateSourceReaderFromByteStream(stream, attrs, &reader);
    MDBG("media: source reader hr=0x%08lX reader=%p\n", (unsigned long)hr,
         (void*)reader);
    if (FAILED(hr) || !reader) {
        Fail("无法创建媒体解码器");
        SafeRelease(&attrs);
        SafeRelease(&stream);
        CoUninitialize();
        return;
    }

    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    DWORD video_index = (DWORD)-1;
    DWORD audio_index = (DWORD)-1;
    for (DWORD i = 0; i < 16; ++i) {
        IMFMediaType* native = nullptr;
        HRESULT nhr = reader->GetNativeMediaType(i, 0, &native);
        if (FAILED(nhr) || !native) break;
        GUID major{};
        native->GetGUID(MF_MT_MAJOR_TYPE, &major);
        if (major == MFMediaType_Video && video_index == (DWORD)-1) {
            video_index = i;
        } else if (major == MFMediaType_Audio && audio_index == (DWORD)-1) {
            audio_index = i;
        }
        native->Release();
    }
    MDBG("media: video_index=%ld audio_index=%ld\n", (long)video_index,
         (long)audio_index);
    if (video_index == (DWORD)-1) {
        Fail("媒体中没有视频流");
        SafeRelease(&reader);
        SafeRelease(&attrs);
        SafeRelease(&stream);
        CoUninitialize();
        return;
    }

    IMFMediaType* vt = nullptr;
    MFCreateMediaType(&vt);
    vt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                     nullptr, vt);
    SafeRelease(&vt);
    if (FAILED(hr)) {
        Fail("无法设置视频输出格式");
        SafeRelease(&reader);
        SafeRelease(&attrs);
        SafeRelease(&stream);
        CoUninitialize();
        return;
    }

    IMFMediaType* actual = nullptr;
    if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              &actual)) && actual) {
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(actual, MF_MT_FRAME_SIZE, &w, &h);
        width = (int)w;
        height = (int)h;
        SafeRelease(&actual);
    }

    PROPVARIANT dur;
    PropVariantInit(&dur);
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                                    MF_PD_DURATION, &dur))) {
        SetDuration((double)dur.uhVal.QuadPart / 10000000.0);
    }
    PropVariantClear(&dur);

    // Optional audio stream; failures just mean silent playback.
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    IMFMediaType* at = nullptr;
    MFCreateMediaType(&at);
    at->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    at->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    at->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    at->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    at->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    at->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    at->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000);
    at->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr,
                                     at);
    bool audio_ok = SUCCEEDED(hr);
    has_audio = audio_ok;
    SafeRelease(&at);
    if (audio_ok) {
        audio.Init();  // silently disabled when no output device exists
    }

    ready = true;
    MDBG("media: ready %dx%d duration=%.2f audio=%d\n", width.load(),
         height.load(), duration.load(), has_audio.load() ? 1 : 0);
    if (playing.load()) StartClock();

    // 没有 autoplay 时也要先解出一帧当“海报帧”，否则 <video> 只会是一块纯黑。
    // 取到首帧后置 true，暂停期间就回到休眠等待。
    bool first_frame_done = false;

    while (!stop.load()) {
        // TakeSeek 会原子地取走并清空跳转请求（互斥量保护，跨线程可见性确定）。
        double seek = TakeSeek();
        if (seek >= 0.0) {
            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_I8;
            var.hVal.QuadPart = (LONGLONG)(seek * 10000000.0);
            reader->SetCurrentPosition(GUID_NULL, var);
            PropVariantClear(&var);
            ResetClock(seek);
            MDBG("media: worker seek -> seek=%.3f position=%.3f playing=%d\n",
                 seek, GetPosition(), playing.load() ? 1 : 0);
            // 跳转后要重新取一帧，保证暂停状态下画面也跟着跳。
            first_frame_done = false;
        }

        if (!playing.load()) {
            if (first_frame_done) {
                Sleep(10);
                continue;
            }
            // 否则继续往下读一个 sample，只为拿到首帧。
        }

        DWORD stream_index = 0;
        DWORD flags = 0;
        LONGLONG ts = 0;
        IMFSample* sample = nullptr;
        hr = reader->ReadSample(MF_SOURCE_READER_ANY_STREAM, 0, &stream_index,
                                &flags, &ts, &sample);
        if (FAILED(hr)) {
            Fail("视频解码失败");
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            SafeRelease(&sample);
            if (loop.load()) {
                RequestSeek(0.0);
                continue;
            }
            SetPosition(GetDuration());
            playing = false;
            // 播完停住后允许重新取首帧，画面不会停在最后一次解码的中间态。
            first_frame_done = false;
            continue;
        }
        if (!sample) continue;

        if (stream_index == video_index) {
            LONGLONG sample_time = 0;
            sample->GetSampleTime(&sample_time);
            double target = (double)sample_time / 10000000.0;
            while (!stop.load() && playing.load() &&
                   PositionNow() + 0.002 < target) {
                Sleep(2);
            }
            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->GetBufferByIndex(0, &buffer)) && buffer) {
                BYTE* data = nullptr;
                DWORD max_len = 0;
                DWORD cur_len = 0;
                if (SUCCEEDED(buffer->Lock(&data, &max_len, &cur_len)) && data) {
                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        frame.assign(data, data + cur_len);
                        // Media Foundation 输出的 RGB32 实际是 BGRX：X 字节并不保证
                        // 是 255（实测整帧都是 0）。本引擎用预乘 AlphaBlend 合成，
                        // alpha=0 会让整帧完全透明，表现为一块纯黑的 <video>。
                        // 视频帧恒为不透明，这里统一补齐 alpha。
                        for (size_t i = 3; i < frame.size(); i += 4) {
                            frame[i] = 255;
                        }
                    }
                    frame_dirty = true;
                    // 暂停时取首帧不应该推进播放位置。
                    if (playing.load()) SetPosition(target);
                    first_frame_done = true;
                    buffer->Unlock();
                }
                SafeRelease(&buffer);
            }
        } else if (stream_index == audio_index) {
            // 只有真正在播放且未静音时才推音频；暂停取首帧时不要出声。
            if (audio.ok() && playing.load() && !muted.load()) {
                IMFMediaBuffer* buffer = nullptr;
                if (SUCCEEDED(sample->GetBufferByIndex(0, &buffer)) && buffer) {
                    BYTE* data = nullptr;
                    DWORD max_len = 0;
                    DWORD cur_len = 0;
                    if (SUCCEEDED(buffer->Lock(&data, &max_len, &cur_len)) && data) {
                        audio.Write(data, cur_len / 4);
                        buffer->Unlock();
                    }
                    SafeRelease(&buffer);
                }
            }
        }
        SafeRelease(&sample);
    }

    audio.Close();
    SafeRelease(&reader);
    SafeRelease(&attrs);
    SafeRelease(&stream);
    CoUninitialize();
}

MediaPlayer::MediaPlayer() : impl_(new Impl()) {}

MediaPlayer::~MediaPlayer() {
    Close();
}

void MediaPlayer::Open(const std::string& url, bool autoplay, bool loop,
                       bool muted) {
    Close();
    impl_->url = url;
    impl_->stop = false;
    impl_->playing = autoplay;
    impl_->ready = false;
    impl_->failed = false;
    impl_->loop = loop;
    impl_->muted = muted;
    impl_->frame_dirty = false;
    impl_->ResetAll();
    impl_->width = 0;
    impl_->height = 0;
    impl_->has_audio = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->frame.clear();
        impl_->error.clear();
    }
    // 捕获 Impl* 而不是 this：MediaPlayer 对象一旦被移动/析构，捕获 this 再解引用
    // impl_ 就是未定义行为；直接抓住 Impl 的生命周期更稳。
    Impl* impl = impl_.get();
    impl->worker = std::thread([impl] { impl->Run(); });
}

void MediaPlayer::Close() {
    if (!impl_) return;
    impl_->stop = true;
    impl_->playing = false;
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->audio.Close();
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->frame.clear();
}

void MediaPlayer::Play() {
    if (!impl_ || impl_->failed.load()) return;
    impl_->StartClock();
    impl_->playing = true;
}

void MediaPlayer::Pause() {
    if (!impl_) return;
    impl_->SetPosition(impl_->PositionNow());
    impl_->playing = false;
}

void MediaPlayer::TogglePlay() {
    if (!impl_) return;
    if (impl_->playing.load()) {
        Pause();
    } else {
        Play();
    }
}

bool MediaPlayer::IsPlaying() const {
    return impl_ && impl_->playing.load();
}

bool MediaPlayer::IsReady() const {
    return impl_ && impl_->ready.load();
}

bool MediaPlayer::Failed() const {
    return impl_ && impl_->failed.load();
}

std::string MediaPlayer::Error() const {
    if (!impl_) return "";
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->error;
}

void MediaPlayer::Seek(double seconds) {
    if (!impl_) return;
    if (seconds < 0) seconds = 0;
    double d = impl_->GetDuration();
    if (d > 0 && seconds > d) seconds = d;
    MDBG("media: Seek(%.3f) dur=%.3f\n", seconds, d);
    impl_->ResetClock(seconds);
    impl_->RequestSeek(seconds);
}

double MediaPlayer::Position() const {
    return impl_ ? impl_->PositionNow() : 0.0;
}

double MediaPlayer::Duration() const {
    return impl_ ? impl_->GetDuration() : 0.0;
}

void MediaPlayer::SetMuted(bool muted) {
    if (impl_) impl_->muted = muted;
}

bool MediaPlayer::Muted() const {
    return impl_ && impl_->muted.load();
}

bool MediaPlayer::Update() {
    if (!impl_) return false;
    return impl_->frame_dirty.exchange(false);
}

bool MediaPlayer::CopyFrame(std::vector<uint8_t>* bgra, int* width, int* height) {
    if (!impl_ || !bgra || !width || !height) return false;
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->frame.empty()) return false;
    *bgra = impl_->frame;
    *width = impl_->width.load();
    *height = impl_->height.load();
    return *width > 0 && *height > 0;
}

MediaInfo MediaPlayer::Info() const {
    MediaInfo info;
    if (!impl_) return info;
    info.width = impl_->width.load();
    info.height = impl_->height.load();
    info.duration = impl_->GetDuration();
    info.has_video = info.width > 0 && info.height > 0;
    info.has_audio = impl_->has_audio.load();
    info.ready = impl_->ready.load();
    info.failed = impl_->failed.load();
    std::lock_guard<std::mutex> lock(impl_->mtx);
    info.error = impl_->error;
    return info;
}

}  // namespace zb