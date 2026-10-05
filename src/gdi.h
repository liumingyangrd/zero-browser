#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "common.h"
#include "gfx.h"

#include <map>

namespace zb {

class GdiCanvas : public Canvas {
public:
    GdiCanvas(HDC dc, int width, int height);
    ~GdiCanvas() override;

    void Init(int width, int height);
    size_t MeasureText(const std::string& utf8, int font_size,
                       bool bold, int max_width = 0) const override;
    void DrawText(const std::string& utf8, int x, int y,
                  int font_size, uint32_t rgb, bool bold,
                  bool italic, bool underline) override;

    void FillRect(int x, int y, int w, int h, uint32_t rgb);
    void FillRoundRect(int x, int y, int w, int h, int radius, uint32_t rgb);
    void StrokeRect(int x, int y, int w, int h, uint32_t rgb);
    void StrokeLine(int x1, int y1, int x2, int y2, uint32_t rgb, int width = 1);
    void StrokeArc(const Rect& rc, int start, int sweep, uint32_t rgb);

    int Width() const { return width_; }
    int Height() const { return height_; }
    HDC Dc() const { return dc_; }

    void Clip(const Rect& rect);
    void ResetClip();
    void DrawImage(const uint8_t* bgra, int src_w, int src_h, int dst_x,
                   int dst_y, int dst_w, int dst_h) override;

private:
    mutable std::map<std::pair<int, bool>, HFONT> fonts_;
    HDC dc_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    HGDIOBJ old_bmp_ = nullptr;
    HBITMAP bitmap_ = nullptr;
};

std::string Utf8ToAnsi(const std::string& utf8);
std::string AnsiToUtf8(const std::string& ansi);
std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);

}  // namespace zb