#pragma once

#include "common.h"

#include <memory>
#include <string>
#include <vector>

namespace zb {

// Decoded bitmap. Pixels are premultiplied BGRA (32bpp top-down),
// matching the existing video frame format so GDI can blit it directly.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;
    std::string source_url;
};

// Use the system WIC only for "image bytes -> bitmap pixels" decoding. Format
// detection, size computation, layout, cropping, scaling, and drawing remain the
// browser's own responsibility.
std::shared_ptr<Image> DecodeImage(const uint8_t* data, size_t len);

inline std::shared_ptr<Image> DecodeImage(const std::string& bytes) {
    return DecodeImage(reinterpret_cast<const uint8_t*>(bytes.data()),
                       bytes.size());
}

}  // namespace zb
