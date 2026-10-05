#pragma once

#include "common.h"

namespace zb {

class Canvas {
public:
    virtual ~Canvas() = default;

    // italic 参与字体选择：斜体与正体的字宽不同，忽略它会让排版与绘制不一致。
    // family/weight 让 CSS 的 font-family / font-weight 真正生效（默认值保持旧行为）。
    virtual size_t MeasureText(const std::string& utf8, int font_size,
                               bool bold, int max_width = 0,
                               bool italic = false,
                               const std::string& family = std::string(),
                               int weight = 0) const = 0;
    virtual void DrawText(const std::string& utf8, int x, int y,
                          int font_size, uint32_t rgb, bool bold,
                          bool italic, bool underline,
                          const std::string& family = std::string(),
                          int weight = 0) = 0;
    // 文本行高（像素）。用于把文字在按钮/输入框里垂直居中，
    // 以及实现 line-height: normal（取字体真实度量，贴近 Chromium）。
    virtual int TextHeight(int font_size, bool bold = false,
                           bool italic = false,
                           const std::string& family = std::string(),
                           int weight = 0) const = 0;
    virtual void FillRect(int x, int y, int w, int h, uint32_t rgb) = 0;
    // 带透明度的填充：CSS 的 rgba()/hsla()/8 位 hex 都是半透明的。
    virtual void FillRectAlpha(int x, int y, int w, int h, uint32_t rgb,
                               uint8_t alpha) = 0;
    virtual void FillRoundRect(int x, int y, int w, int h, int radius,
                               uint32_t rgb) = 0;
    virtual void StrokeRect(int x, int y, int w, int h, uint32_t rgb) = 0;
    virtual void StrokeLine(int x1, int y1, int x2, int y2, uint32_t rgb,
                            int width = 1) = 0;
    // 画一段圆弧。角度遵循数学约定（0° 指向右，逆时针为正，y 轴向上），
    // rect 为外接矩形 {x, y, w, h}。sweep 为扫过的角度，可为负。
    virtual void StrokeArc(const Rect& rect, int start, int sweep,
                           uint32_t rgb, int width = 2) = 0;
    virtual void Clip(const Rect& rect) = 0;
    virtual void ResetClip() = 0;
    virtual void DrawImage(const uint8_t* bgra, int src_w, int src_h, int dst_x,
                           int dst_y, int dst_w, int dst_h) = 0;
};

}  // namespace zb