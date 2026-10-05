#include "gdi.h"

#include <cstring>
#include <utility>

namespace zb {

GdiCanvas::GdiCanvas(HDC dc, int width, int height) : dc_(dc) {
    Init(width, height);
}

GdiCanvas::~GdiCanvas() {
    if (bitmap_ && old_bmp_) SelectObject(dc_, old_bmp_);
    if (bitmap_) DeleteObject(bitmap_);
    for (auto& kv : fonts_) DeleteObject(kv.second);
}

void GdiCanvas::Init(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (bitmap_) {
        SelectObject(dc_, old_bmp_);
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
        old_bmp_ = nullptr;
    }
    width_ = width;
    height_ = height;
    // 用 32 位 DIB 段，而不是 CreateCompatibleBitmap。
    // CreateCompatibleBitmap 的位深取决于目标 DC：如果 DC 来自
    // CreateCompatibleDC(nullptr)（默认选中的是 1x1 单色位图），拿到的是 1bpp
    // 单色位图，彩色页面和视频帧会被整体抖动成黑白。DIB 段可以保证画布始终是
    // 32 位彩色，让渲染结果与创建画布时用的 DC 无关。
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap_) old_bmp_ = SelectObject(dc_, bitmap_);
    SetBkMode(dc_, TRANSPARENT);
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                        out.data(), n);
    return out;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                        out.data(), n, nullptr, nullptr);
    return out;
}

size_t GdiCanvas::MeasureText(const std::string& utf8, int font_size,
                              bool bold, int max_width) const {
    auto key = std::make_pair(font_size, bold);
    HFONT font = nullptr;
    auto it = fonts_.find(key);
    if (it != fonts_.end()) {
        font = it->second;
    } else {
        font = CreateFontW(
            -MulDiv(font_size, GetDeviceCaps(dc_, LOGPIXELSY), 72),
            0, 0, 0,
            bold ? FW_BOLD : FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        fonts_[key] = font;
    }
    HFONT old = (HFONT)SelectObject(dc_, font);
    SIZE sz{};
    std::wstring wide = Utf8ToWide(utf8);
    GetTextExtentPoint32W(dc_, wide.c_str(), (int)wide.size(), &sz);
    SelectObject(dc_, old);
    (void)max_width;
    return (size_t)sz.cx;
}

void GdiCanvas::DrawText(const std::string& utf8, int x, int y,
                         int font_size, uint32_t rgb, bool bold,
                         bool italic, bool underline) {
    auto key = std::make_pair(font_size, bold);
    HFONT font = nullptr;
    auto it = fonts_.find(key);
    if (it != fonts_.end()) {
        font = it->second;
    } else {
        font = CreateFontW(
            -MulDiv(font_size, GetDeviceCaps(dc_, LOGPIXELSY), 72),
            0, 0, italic ? 10 : 0,
            bold ? FW_BOLD : FW_NORMAL,
            italic ? TRUE : FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        fonts_[key] = font;
    }
    HFONT old = (HFONT)SelectObject(dc_, font);
    COLORREF old_color = SetTextColor(dc_, RGB((rgb >> 16) & 255,
                                               (rgb >> 8) & 255,
                                               rgb & 255));
    std::wstring wide = Utf8ToWide(utf8);
    TextOutW(dc_, x, y, wide.c_str(), (int)wide.size());
    if (underline) {
        SIZE sz{};
        GetTextExtentPoint32W(dc_, wide.c_str(), (int)wide.size(), &sz);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB((rgb >> 16) & 255,
                                              (rgb >> 8) & 255,
                                              rgb & 255));
        HGDIOBJ old_pen = SelectObject(dc_, pen);
        MoveToEx(dc_, x, y + sz.cy - 1, nullptr);
        LineTo(dc_, x + sz.cx, y + sz.cy - 1);
        SelectObject(dc_, old_pen);
        DeleteObject(pen);
    }
    SetTextColor(dc_, old_color);
    SelectObject(dc_, old);
}

void GdiCanvas::FillRect(int x, int y, int w, int h, uint32_t rgb) {
    RECT rc{x, y, x + w, y + h};
    HBRUSH br = CreateSolidBrush(RGB((rgb >> 16) & 255, (rgb >> 8) & 255,
                                     rgb & 255));
    ::FillRect(dc_, &rc, br);
    DeleteObject(br);
}

void GdiCanvas::FillRoundRect(int x, int y, int w, int h, int radius,
                              uint32_t rgb) {
    if (radius <= 0) {
        FillRect(x, y, w, h, rgb);
        return;
    }
    HBRUSH br = CreateSolidBrush(RGB((rgb >> 16) & 255, (rgb >> 8) & 255,
                                     rgb & 255));
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ old_br = SelectObject(dc_, br);
    HGDIOBJ old_pen = SelectObject(dc_, pen);
    RoundRect(dc_, x, y, x + w, y + h, radius * 2, radius * 2);
    SelectObject(dc_, old_pen);
    SelectObject(dc_, old_br);
    DeleteObject(pen);
    DeleteObject(br);
}

void GdiCanvas::StrokeRect(int x, int y, int w, int h, uint32_t rgb) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB((rgb >> 16) & 255, (rgb >> 8) & 255,
                                          rgb & 255));
    HGDIOBJ old_pen = SelectObject(dc_, pen);
    HBRUSH old_br = (HBRUSH)SelectObject(dc_, GetStockObject(NULL_BRUSH));
    Rectangle(dc_, x, y, x + w, y + h);
    SelectObject(dc_, old_br);
    SelectObject(dc_, old_pen);
    DeleteObject(pen);
}

void GdiCanvas::StrokeLine(int x1, int y1, int x2, int y2, uint32_t rgb,
                           int width) {
    HPEN pen = CreatePen(PS_SOLID, width, RGB((rgb >> 16) & 255,
                                              (rgb >> 8) & 255,
                                              rgb & 255));
    HGDIOBJ old_pen = SelectObject(dc_, pen);
    MoveToEx(dc_, x1, y1, nullptr);
    LineTo(dc_, x2, y2);
    SelectObject(dc_, old_pen);
    DeleteObject(pen);
}

void GdiCanvas::StrokeArc(const Rect& rc, int start, int sweep,
                          uint32_t rgb) {
    (void)start;
    (void)sweep;
    HPEN pen = CreatePen(PS_SOLID, 2, RGB((rgb >> 16) & 255,
                                          (rgb >> 8) & 255,
                                          rgb & 255));
    HGDIOBJ old_pen = SelectObject(dc_, pen);
    HBRUSH old_br = (HBRUSH)SelectObject(dc_, GetStockObject(NULL_BRUSH));
    Ellipse(dc_, rc.x, rc.y, rc.x + rc.w, rc.y + rc.h);
    SelectObject(dc_, old_br);
    SelectObject(dc_, old_pen);
    DeleteObject(pen);
}

void GdiCanvas::Clip(const Rect& rect) {
    RECT rc{rect.x, rect.y, rect.x + rect.w, rect.y + rect.h};
    IntersectClipRect(dc_, rc.left, rc.top, rc.right, rc.bottom);
}

void GdiCanvas::ResetClip() {
    SelectClipRgn(dc_, nullptr);
}

void GdiCanvas::DrawImage(const uint8_t* bgra, int src_w, int src_h, int dst_x,
                          int dst_y, int dst_w, int dst_h) {
    if (!bgra || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;

    // 用 AlphaBlend 合成，支持透明 PNG。传入的 BGRA 需要是 premultiplied；
    // 图片解码时已做 premultiply，视频帧都是不透明像素，同样兼容。
    HDC mem = CreateCompatibleDC(dc_);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = src_w;
    info.bmiHeader.biHeight = -src_h;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
        return;
    }
    std::memcpy(bits, bgra, (size_t)src_w * src_h * 4);
    HGDIOBJ old = SelectObject(mem, bmp);
    SetStretchBltMode(dc_, COLORONCOLOR);
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(dc_, dst_x, dst_y, dst_w, dst_h, mem, 0, 0, src_w, src_h, bf);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

}  // namespace zb