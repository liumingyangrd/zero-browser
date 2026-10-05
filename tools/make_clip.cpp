// make_clip: generate a test video with "obviously visible content" (this tool is not
// part of the browser itself).
//
// Motivation: the sample.wmv that shipped with the repo is almost entirely black (only
// one small white dot), so the eye cannot tell whether the picture is actually moving,
// which leads to the false conclusion "black screen = playback failure".
//
// This tool uses the Media Foundation Sink Writer (the system encoding capability, used
// here only as a "low-level pipe") to encode a per-frame GDI-drawn animation into WMV:
// moving color blocks + frame number + progress bar, unmistakable while playing.
//
// Usage: make_clip <output.wmv> [frames] [width] [height] [fps]

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// WMV3 is not declared in some MinGW headers, so we carry our own GUID here.
static const GUID kZbWmv3 = {
    0x31564D57, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

namespace {

template <typename T>
void SafeRelease(T** p) {
    if (*p) {
        (*p)->Release();
        *p = nullptr;
    }
}

void FillPattern(HDC dc, int w, int h, int frame, int total) {
    // Make the background a gradient block that changes over time, so the motion is
    // obvious to the eye.
    int phase = (frame * 6) % 360;
    for (int y = 0; y < h; y += 4) {
        int t = (phase + y * 180 / h) % 360;
        int r = (int)(127 + 120 * sin(t * 3.14159265 / 180.0));
        int g = (int)(127 + 120 * sin((t + 120) * 3.14159265 / 180.0));
        int b = (int)(127 + 120 * sin((t + 240) * 3.14159265 / 180.0));
        RECT rc{0, y, w, y + 4};
        HBRUSH br = CreateSolidBrush(RGB(r, g, b));
        FillRect(dc, &rc, br);
        DeleteObject(br);
    }

    // Moving white square: its position shifts horizontally with the frame number.
    int box = h / 5;
    int x = (int)((double)(w - box) * (double)frame / (double)(total - 1));
    RECT marker{x, h / 2 - box / 2, x + box, h / 2 + box / 2};
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &marker, white);
    DeleteObject(white);

    // Frame number + time text.
    char label[64];
    std::snprintf(label, sizeof(label), "FRAME %d / %d", frame + 1, total);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    HFONT font = CreateFontW(-MulDiv(28, 96, 72), 0, 0, 0, FW_BOLD, FALSE,
                             FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);
    TextOutA(dc, 16, 12, label, (int)std::char_traits<char>::length(label));
    SelectObject(dc, old_font);
    DeleteObject(font);

    // Bottom progress bar.
    RECT track{16, h - 28, w - 16, h - 14};
    HBRUSH dark = CreateSolidBrush(RGB(30, 41, 59));
    FillRect(dc, &track, dark);
    DeleteObject(dark);
    RECT done{16, h - 28, 16 + (int)((double)(w - 32) * (frame + 1) / total),
              h - 14};
    HBRUSH accent = CreateSolidBrush(RGB(56, 189, 248));
    FillRect(dc, &done, accent);
    DeleteObject(accent);
}

}  // namespace

namespace {

#pragma pack(push, 1)
struct BmpFileHeader {
    uint16_t type = 0x4D42;
    uint32_t size = 0;
    uint16_t reserved1 = 0;
    uint16_t reserved2 = 0;
    uint32_t off_bits = 54;
};
struct BmpInfoHeader {
    uint32_t size = 40;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t planes = 1;
    uint16_t bit_count = 32;
    uint32_t compression = 0;
    uint32_t size_image = 0;
    int32_t x_ppm = 0;
    int32_t y_ppm = 0;
    uint32_t clr_used = 0;
    uint32_t clr_important = 0;
};
#pragma pack(pop)

// Save the frame from before it enters the encoder, to isolate the four stages
// "drawing / encoding / decoding / browser painting".
bool SaveBmp32(const char* path, const void* bits, int w, int h) {
    const uint8_t* src = (const uint8_t*)bits;
    std::vector<uint8_t> copy((size_t)w * h * 4);
    for (size_t i = 0; i < copy.size(); i += 4) {
        copy[i + 0] = src[i + 0];
        copy[i + 1] = src[i + 1];
        copy[i + 2] = src[i + 2];
        copy[i + 3] = 255;
    }
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    BmpFileHeader fh;
    BmpInfoHeader ih;
    ih.width = w;
    ih.height = -h;
    ih.size_image = (uint32_t)copy.size();
    fh.size = sizeof(fh) + sizeof(ih) + ih.size_image;
    std::fwrite(&fh, sizeof(fh), 1, f);
    std::fwrite(&ih, sizeof(ih), 1, f);
    std::fwrite(copy.data(), 1, copy.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "testpage/anim.wmv";
    int frames = argc > 2 ? std::atoi(argv[2]) : 75;
    int w = argc > 3 ? std::atoi(argv[3]) : 640;
    int h = argc > 4 ? std::atoi(argv[4]) : 360;
    int fps = argc > 5 ? std::atoi(argv[5]) : 15;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::printf("CoInitializeEx failed 0x%08lX\n", (unsigned long)hr);
        return 1;
    }
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        std::printf("MFStartup failed 0x%08lX\n", (unsigned long)hr);
        return 1;
    }

    std::wstring wout(out.begin(), out.end());
    IMFSinkWriter* writer = nullptr;
    hr = MFCreateSinkWriterFromURL(wout.c_str(), nullptr, nullptr, &writer);
    if (FAILED(hr) || !writer) {
        std::printf("MFCreateSinkWriterFromURL failed 0x%08lX\n",
                    (unsigned long)hr);
        MFShutdown();
        return 1;
    }

    IMFMediaType* out_type = nullptr;
    MFCreateMediaType(&out_type);
    out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out_type->SetGUID(MF_MT_SUBTYPE, kZbWmv3);
    out_type->SetUINT32(MF_MT_AVG_BITRATE, 1500000);
    out_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(out_type, MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
    MFSetAttributeRatio(out_type, MF_MT_FRAME_RATE, (UINT32)fps, 1);
    MFSetAttributeRatio(out_type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD stream_index = 0;
    hr = writer->AddStream(out_type, &stream_index);
    SafeRelease(&out_type);
    if (FAILED(hr)) {
        std::printf("AddStream failed 0x%08lX\n", (unsigned long)hr);
        SafeRelease(&writer);
        MFShutdown();
        return 1;
    }

    IMFMediaType* in_type = nullptr;
    MFCreateMediaType(&in_type);
    in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    in_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(in_type, MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
    MFSetAttributeRatio(in_type, MF_MT_FRAME_RATE, (UINT32)fps, 1);
    MFSetAttributeRatio(in_type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = writer->SetInputMediaType(stream_index, in_type, nullptr);
    SafeRelease(&in_type);
    if (FAILED(hr)) {
        std::printf("SetInputMediaType(RGB32) failed 0x%08lX\n",
                    (unsigned long)hr);
        SafeRelease(&writer);
        MFShutdown();
        return 1;
    }

    hr = writer->BeginWriting();
    if (FAILED(hr)) {
        std::printf("BeginWriting failed 0x%08lX\n", (unsigned long)hr);
        SafeRelease(&writer);
        MFShutdown();
        return 1;
    }

    // Draw each frame with GDI into a DIB, then feed it to the encoder.
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, dib);

    const LONG long frame_dur = 10000000LL / fps;
    int written = 0;
    for (int i = 0; i < frames; ++i) {
        FillPattern(dc, w, h, i, frames);
        GdiFlush();
        if (i == 0) {
            if (SaveBmp32("build\\clip-source-frame0.bmp", bits, w, h)) {
                std::printf("saved build\\clip-source-frame0.bmp\n");
            }
        }

        IMFMediaBuffer* buffer = nullptr;
        if (FAILED(MFCreateMemoryBuffer((DWORD)(w * h * 4), &buffer)) || !buffer) {
            break;
        }
        BYTE* dst = nullptr;
        DWORD max_len = 0;
        if (FAILED(buffer->Lock(&dst, &max_len, nullptr)) || !dst) {
            SafeRelease(&buffer);
            break;
        }
        std::memcpy(dst, bits, (size_t)w * h * 4);
        buffer->Unlock();
        buffer->SetCurrentLength((DWORD)(w * h * 4));

        IMFSample* sample = nullptr;
        MFCreateSample(&sample);
        sample->AddBuffer(buffer);
        sample->SetSampleTime(frame_dur * i);
        sample->SetSampleDuration(frame_dur);
        hr = writer->WriteSample(stream_index, sample);
        SafeRelease(&sample);
        SafeRelease(&buffer);
        if (FAILED(hr)) {
            std::printf("WriteSample frame %d failed 0x%08lX\n", i,
                        (unsigned long)hr);
            break;
        }
        written++;
    }

    std::printf("frames written=%d\n", written);
    hr = writer->Finalize();
    std::printf("Finalize hr=0x%08lX\n", (unsigned long)hr);

    SelectObject(dc, old);
    DeleteObject(dib);
    DeleteDC(dc);
    SafeRelease(&writer);
    MFShutdown();
    CoUninitialize();
    return written > 0 && SUCCEEDED(hr) ? 0 : 1;
}
