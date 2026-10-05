#pragma once

#include "common.h"

namespace zb {

class Canvas {
public:
    virtual ~Canvas() = default;

    virtual size_t MeasureText(const std::string& utf8, int font_size,
                               bool bold, int max_width = 0) const = 0;
    virtual void DrawText(const std::string& utf8, int x, int y,
                          int font_size, uint32_t rgb, bool bold,
                          bool italic, bool underline) = 0;
    virtual void FillRect(int x, int y, int w, int h, uint32_t rgb) = 0;
    virtual void FillRoundRect(int x, int y, int w, int h, int radius,
                               uint32_t rgb) = 0;
    virtual void StrokeRect(int x, int y, int w, int h, uint32_t rgb) = 0;
    virtual void StrokeLine(int x1, int y1, int x2, int y2, uint32_t rgb,
                            int width = 1) = 0;
    virtual void StrokeArc(const Rect& rect, int start, int sweep,
                           uint32_t rgb) = 0;
    virtual void Clip(const Rect& rect) = 0;
    virtual void ResetClip() = 0;
    virtual void DrawImage(const uint8_t* bgra, int src_w, int src_h, int dst_x,
                           int dst_y, int dst_w, int dst_h) = 0;
};

}  // namespace zb