#include "image.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>
#endif

namespace zb {

namespace {

struct ComScope {
    ComScope() { CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope() { CoUninitialize(); }
};

}  // namespace

std::shared_ptr<Image> DecodeImage(const uint8_t* data, size_t len) {
    if (!data || len == 0 || len > (size_t)INT32_MAX) return nullptr;

    ComScope com;
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return nullptr;

    auto out = std::make_shared<Image>();
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(
        const_cast<BYTE*>(data), (UINT)len);
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromStream(
            stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = frame->GetSize((UINT*)&out->width,
                                           (UINT*)&out->height);
    if (SUCCEEDED(hr) && out->width > 0 && out->height > 0)
        hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) {
        out->bgra.resize((size_t)out->width * out->height * 4);
        hr = converter->CopyPixels(nullptr,
                                   (UINT)out->width * 4,
                                   (UINT)out->bgra.size(), out->bgra.data());
    }
    if (SUCCEEDED(hr)) {
        // Convert to premultiplied alpha so GDI AlphaBlend can composite
        // transparent PNG correctly.
        for (size_t i = 0; i + 3 < out->bgra.size(); i += 4) {
            uint8_t a = out->bgra[i + 3];
            if (a == 255) continue;
            out->bgra[i + 0] = (uint8_t)(out->bgra[i + 0] * a / 255);
            out->bgra[i + 1] = (uint8_t)(out->bgra[i + 1] * a / 255);
            out->bgra[i + 2] = (uint8_t)(out->bgra[i + 2] * a / 255);
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();

    if (FAILED(hr) || out->bgra.empty()) return nullptr;
    return out;
}

}  // namespace zb
