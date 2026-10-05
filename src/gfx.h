#pragma once

#include "common.h"

namespace zb {

class Canvas {
public:
    virtual ~Canvas() = default;

    // italic participates in font selection: italic and upright glyphs have different
    // advance widths, and ignoring it makes measurement and painting disagree.
    // family/weight make the CSS font-family / font-weight actually take effect
    // (default values keep the old behavior).
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
    // Text line height (pixels). Used to vertically center text inside buttons/inputs
    // and to implement line-height: normal (using the font's real metrics, close to Chromium).
    virtual int TextHeight(int font_size, bool bold = false,
                           bool italic = false,
                           const std::string& family = std::string(),
                           int weight = 0) const = 0;
    virtual void FillRect(int x, int y, int w, int h, uint32_t rgb) = 0;
    // Fill with transparency: CSS rgba()/hsla()/8-digit hex are all semi-transparent.
    virtual void FillRectAlpha(int x, int y, int w, int h, uint32_t rgb,
                               uint8_t alpha) = 0;
    virtual void FillRoundRect(int x, int y, int w, int h, int radius,
                               uint32_t rgb) = 0;
    virtual void StrokeRect(int x, int y, int w, int h, uint32_t rgb) = 0;
    virtual void StrokeLine(int x1, int y1, int x2, int y2, uint32_t rgb,
                            int width = 1) = 0;
    // Draw an arc. Angles follow the mathematical convention (0° points right,
    // counter-clockwise positive, y axis pointing up), and rect is the bounding box
    // {x, y, w, h}. sweep is the swept angle and may be negative.
    virtual void StrokeArc(const Rect& rect, int start, int sweep,
                           uint32_t rgb, int width = 2) = 0;
    virtual void Clip(const Rect& rect) = 0;
    virtual void ResetClip() = 0;
    virtual void DrawImage(const uint8_t* bgra, int src_w, int src_h, int dst_x,
                           int dst_y, int dst_w, int dst_h) = 0;
};

}  // namespace zb