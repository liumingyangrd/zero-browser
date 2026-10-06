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
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <objbase.h>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif


extern "C" HRESULT WINAPI MFCreateMFByteStreamOnStream(
    IStream* pStream, IMFByteStream** ppByteStream);

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

std::string WideToUtf8Local(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0,
                                nullptr, nullptr);
    if (n <= 0) return "";
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n,
                        nullptr, nullptr);
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



bool WriteOwnTempFile(const std::string& bytes, std::string* path_out,
                      std::string* error) {
    static std::atomic<unsigned> counter{0};
    unsigned idx = counter.fetch_add(1);
    wchar_t tmp[MAX_PATH] = {};
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(tmp) : std::wstring();
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) {
        dir.pop_back();
    }
    std::wstring full;
    if (!dir.empty()) {
        dir += L"\\zero-browser-media";
        CreateDirectoryW(dir.c_str(), nullptr);
        wchar_t name[128] = {};
        std::swprintf(name, 128, L"\\zb-%u-%u.media",
                      (unsigned)GetCurrentProcessId(), idx);
        full = dir + name;
    }
    auto open = [&](const std::wstring& path) -> HANDLE {
        return CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
    };
    HANDLE h = full.empty() ? INVALID_HANDLE_VALUE : open(full);
    if (h == INVALID_HANDLE_VALUE) {
        wchar_t alt[128] = {};
        std::swprintf(alt, 128, L"zb-media-%u-%u.media",
                      (unsigned)GetCurrentProcessId(), idx);
        full = alt;
        h = open(full);
    }
    if (h == INVALID_HANDLE_VALUE) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "无法创建媒体缓存文件 (Win32 %lu)",
                      (unsigned long)GetLastError());
        *error = buf;
        return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != bytes.size()) {
        DeleteFileW(full.c_str());
        *error = "写入媒体缓存文件失败";
        return false;
    }
    if (path_out) *path_out = WideToUtf8Local(full);
    return true;
}

bool CreateByteStream(const std::string& url, IMFByteStream** out,
                      std::string* error, std::string* temp_path) {
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
            std::string path;
            if (!WriteOwnTempFile(res.html, &path, error)) return false;
            std::wstring wpath = U8ToWideLocal(path);
            hr = MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST,
                              MF_FILEFLAGS_NONE, wpath.c_str(), out);
            if (FAILED(hr) || !*out) {
                DeleteFileW(wpath.c_str());
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                              "无法打开媒体缓存文件 (0x%08lX)",
                              (unsigned long)hr);
                *error = buf;
                return false;
            }
            if (temp_path) *temp_path = path;
            return true;
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

}  

struct MediaPlayer::Impl {
    std::string url;
    
    std::string media_temp_path;
    std::atomic<bool> stop{false};
    std::atomic<bool> playing{false};
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> muted{false};
    std::atomic<bool> loop{false};
    std::atomic<bool> frame_dirty{false};
    std::atomic<bool> has_audio{false};
    
    
    
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

    std::string temp_media_path;
    if (!CreateByteStream(url, &stream, &error, &temp_media_path)) {
        MDBG("media: byte stream failed: %s\n", error.c_str());
        Fail(error);
        CoUninitialize();
        return;
    }
    if (!temp_media_path.empty()) {
        std::lock_guard<std::mutex> lock(mtx);
        media_temp_path = temp_media_path;  
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
        audio.Init();  
    }

    ready = true;
    MDBG("media: ready %dx%d duration=%.2f audio=%d\n", width.load(),
         height.load(), duration.load(), has_audio.load() ? 1 : 0);
    if (playing.load()) StartClock();

    
    
    bool first_frame_done = false;

    while (!stop.load()) {
        
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
            
            first_frame_done = false;
        }

        if (!playing.load()) {
            if (first_frame_done) {
                Sleep(10);
                continue;
            }
            
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
                        
                        
                        
                        
                        for (size_t i = 3; i < frame.size(); i += 4) {
                            frame[i] = 255;
                        }
                    }
                    frame_dirty = true;
                    
                    if (playing.load()) SetPosition(target);
                    first_frame_done = true;
                    buffer->Unlock();
                }
                SafeRelease(&buffer);
            }
        } else if (stream_index == audio_index) {
            
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
    
    
    Impl* impl = impl_.get();
    impl->worker = std::thread([impl] { impl->Run(); });
}

void MediaPlayer::Close() {
    if (!impl_) return;
    impl_->stop = true;
    impl_->playing = false;
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->audio.Close();
    std::string temp_path;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->frame.clear();
        temp_path = impl_->media_temp_path;
        impl_->media_temp_path.clear();
    }
    
    if (!temp_path.empty()) {
        std::wstring wpath = U8ToWideLocal(temp_path);
        DeleteFileW(wpath.c_str());
    }
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

}  