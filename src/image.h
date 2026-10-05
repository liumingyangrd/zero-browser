#pragma once

#include "common.h"

#include <memory>
#include <string>
#include <vector>

namespace zb {

// 解码后的位图。像素是 premultiplied BGRA（32bpp top-down），
// 与现有视频帧格式一致，便于 GDI 直接上屏。
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;
    std::string source_url;
};

// 用系统 WIC 只做“图片字节 -> 位图像素”的解码。格式识别、尺寸计算、
// 布局、裁剪、缩放和绘制仍是浏览器自己负责。
std::shared_ptr<Image> DecodeImage(const uint8_t* data, size_t len);

inline std::shared_ptr<Image> DecodeImage(const std::string& bytes) {
    return DecodeImage(reinterpret_cast<const uint8_t*>(bytes.data()),
                       bytes.size());
}

}  // namespace zb
