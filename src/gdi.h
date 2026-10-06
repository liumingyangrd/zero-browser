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
                       bool bold, int max_width = 0,
                       bool italic = false,
                       const std::string& family = std::string(),
                       int weight = 0) const override;
    int TextHeight(int font_size, bool bold = false,
                   bool italic = false,
                   const std::string& family = std::string(),
                   int weight = 0) const override;
    void DrawText(const std::string& utf8, int x, int y,
                  int font_size, uint32_t rgb, bool bold,
                  bool italic, bool underline,
                  const std::string& family = std::string(),
                  int weight = 0) override;

    void FillRect(int x, int y, int w, int h, uint32_t rgb);
    void FillRectAlpha(int x, int y, int w, int h, uint32_t rgb,
                       uint8_t alpha) override;
    void FillRoundRect(int x, int y, int w, int h, int radius, uint32_t rgb);
    void StrokeRect(int x, int y, int w, int h, uint32_t rgb);
    void StrokeLine(int x1, int y1, int x2, int y2, uint32_t rgb, int width = 1);
    void StrokeArc(const Rect& rc, int start, int sweep, uint32_t rgb,
                   int width = 2) override;

    int Width() const { return width_; }
    int Height() const { return height_; }
    HDC Dc() const { return dc_; }

    void Clip(const Rect& rect);
    void ResetClip();
    void DrawImage(const uint8_t* bgra, int src_w, int src_h, int dst_x,
                   int dst_y, int dst_w, int dst_h) override;

private:
    struct FontKey {
        int size = 0;
        bool bold = false;
        bool italic = false;
        int weight = 0;
        std::string family;
        bool operator<(const FontKey& o) const {
            if (size != o.size) return size < o.size;
            if (bold != o.bold) return bold < o.bold;
            if (italic != o.italic) return italic < o.italic;
            if (weight != o.weight) return weight < o.weight;
            return family < o.family;
        }
    };
    HFONT FontFor(int font_size, bool bold, bool italic,
                  const std::string& family = std::string(),
                  int weight = 0) const;

    mutable std::map<FontKey, HFONT> fonts_;
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

}  