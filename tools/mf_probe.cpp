#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::wstring path = L"C:\\Windows\\System32\\DriverStore\\FileRepository\\"
                        L"cui_dch.inf_amd64_03c6376789dc6e23\\"
                        L"ColorImageEnhancement.wmv";
    if (argc > 1) {
        std::string a = argv[1];
        path.assign(a.begin(), a.end());
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::printf("CoInitializeEx hr=0x%08lX\n", (unsigned long)hr);
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    std::printf("MFStartup hr=0x%08lX\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;

    IMFSourceReader* reader = nullptr;
    hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    std::printf("CreateSourceReader(path) hr=0x%08lX reader=%p\n",
                (unsigned long)hr, (void*)reader);
    if (FAILED(hr)) {
        std::wstring url = L"file:///";
        for (wchar_t c : path) {
            if (c == L'\\') url.push_back(L'/');
            else url.push_back(c);
        }
        hr = MFCreateSourceReaderFromURL(url.c_str(), nullptr, &reader);
        std::printf("CreateSourceReader(file url) hr=0x%08lX reader=%p\n",
                    (unsigned long)hr, (void*)reader);
    }
    if (FAILED(hr)) {
        IMFByteStream* stream = nullptr;
        HRESULT h2 = MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST,
                                  MF_FILEFLAGS_NONE, path.c_str(), &stream);
        std::printf("MFCreateFile hr=0x%08lX stream=%p\n", (unsigned long)h2,
                    (void*)stream);
        if (SUCCEEDED(h2) && stream) {
            hr = MFCreateSourceReaderFromByteStream(stream, nullptr, &reader);
            std::printf("CreateSourceReader(byte stream) hr=0x%08lX reader=%p\n",
                        (unsigned long)hr, (void*)reader);
            stream->Release();
        }
    }
    if (FAILED(hr)) { MFShutdown(); return 1; }

    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    IMFMediaType* type = nullptr;
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
    std::printf("SetCurrentMediaType RGB32 hr=0x%08lX\n", (unsigned long)hr);
    type->Release();

    IMFMediaType* actual = nullptr;
    reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual);
    UINT32 w = 0, h = 0;
    if (actual) {
        MFGetAttributeSize(actual, MF_MT_FRAME_SIZE, &w, &h);
        std::printf("frame=%ux%u\n", w, h);
        actual->Release();
    }

    PROPVARIANT dur;
    PropVariantInit(&dur);
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                                    MF_PD_DURATION, &dur))) {
        std::printf("duration_100ns=%lld\n", (long long)dur.uhVal.QuadPart);
    }
    PropVariantClear(&dur);

    int got = 0;
    for (int i = 0; i < 60 && got < 3; ++i) {
        DWORD flags = 0;
        LONGLONG ts = 0;
        IMFSample* sample = nullptr;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr,
                                &flags, &ts, &sample);
        if (FAILED(hr)) {
            std::printf("ReadSample hr=0x%08lX\n", (unsigned long)hr);
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            std::printf("end of stream\n");
            break;
        }
        if (!sample) continue;
        IMFMediaBuffer* buffer = nullptr;
        if (SUCCEEDED(sample->GetBufferByIndex(0, &buffer)) && buffer) {
            BYTE* data = nullptr;
            DWORD max_len = 0, cur_len = 0;
            if (SUCCEEDED(buffer->Lock(&data, &max_len, &cur_len))) {
                std::printf("frame[%d] ts=%lld bytes=%lu first=%02X %02X %02X %02X\n",
                            got, (long long)ts, (unsigned long)cur_len,
                            data[0], data[1], data[2], data[3]);
                got++;
                buffer->Unlock();
            }
            buffer->Release();
        }
        sample->Release();
    }

    reader->Release();
    MFShutdown();
    CoUninitialize();
    std::printf("done got=%d\n", got);
    return 0;
}
