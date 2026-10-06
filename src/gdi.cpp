#include "gdi.h"

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

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
    
    
    
    
    
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  
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
                              bool bold, int max_width, bool italic,
                              const std::string& family, int weight) const {
    HFONT font = FontFor(font_size, bold, italic, family, weight);
    HFONT old = (HFONT)SelectObject(dc_, font);
    SIZE sz{};
    std::wstring wide = Utf8ToWide(utf8);
    GetTextExtentPoint32W(dc_, wide.c_str(), (int)wide.size(), &sz);
    SelectObject(dc_, old);
    (void)max_width;
    return (size_t)sz.cx;
}

int GdiCanvas::TextHeight(int font_size, bool bold, bool italic,
                          const std::string& family, int weight) const {
    HFONT font = FontFor(font_size, bold, italic, family, weight);
    HFONT old = (HFONT)SelectObject(dc_, font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc_, &tm);
    SelectObject(dc_, old);
    return (int)(tm.tmHeight + tm.tmExternalLeading);
}

HFONT GdiCanvas::FontFor(int font_size, bool bold, bool italic,
                         const std::string& family, int weight) const {
    FontKey key{font_size, bold, italic, weight, family};
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    int w = weight > 0 ? weight : (bold ? FW_BOLD : FW_NORMAL);
    std::wstring fam = family.empty() ? std::wstring(L"Segoe UI")
                                      : Utf8ToWide(family);
    
    
    if (!fam.empty() && fam != L"Segoe UI") {
        HDC screen = CreateCompatibleDC(nullptr);
        LOGFONTW probe{};
        probe.lfHeight = -12;
        probe.lfCharSet = DEFAULT_CHARSET;
        wcsncpy(probe.lfFaceName, fam.c_str(), 31);
        HFONT test = CreateFontIndirectW(&probe);
        wchar_t actual[64] = {};
        if (test) {
            HGDIOBJ old = SelectObject(screen, test);
            GetTextFaceW(screen, 64, actual);
            SelectObject(screen, old);
            DeleteObject(test);
        }
        DeleteDC(screen);
        std::wstring got = actual;
        bool usable = !got.empty() &&
                      _wcsicmp(got.c_str(), fam.c_str()) == 0;
        if (!usable) {
            
            std::string lf = family;
            for (char& c : lf) c = (char)std::tolower((unsigned char)c);
            if (lf == "sans-serif" || lf == "system-ui" || lf == "serif" ||
                lf == "monospace") {
                fam = (lf == "monospace") ? L"Consolas" : L"Segoe UI";
            } else {
                fam = L"Segoe UI";
            }
        }
    }
    HFONT font = CreateFontW(
        
        
        
        -MulDiv(font_size, GetDeviceCaps(dc_, LOGPIXELSY), 96), 0, 0,
        italic ? 10 : 0, w, italic ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fam.c_str());
    fonts_[key] = font;
    return font;
}

void GdiCanvas::DrawText(const std::string& utf8, int x, int y,
                         int font_size, uint32_t rgb, bool bold,
                         bool italic, bool underline, const std::string& family,
                         int weight) {
    
    
    
    HFONT font = FontFor(font_size, bold, italic, family, weight);
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

void GdiCanvas::FillRectAlpha(int x, int y, int w, int h, uint32_t rgb,
                              uint8_t alpha) {
    if (w <= 0 || h <= 0) return;
    if (alpha >= 255) {
        FillRect(x, y, w, h, rgb);
        return;
    }
    if (alpha == 0) return;
    
    
    HDC mem = CreateCompatibleDC(dc_);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 1;
    info.bmiHeader.biHeight = -1;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
        FillRect(x, y, w, h, rgb);
        return;
    }
    uint8_t* p = (uint8_t*)bits;
    uint8_t r = (uint8_t)((rgb >> 16) & 255);
    uint8_t g = (uint8_t)((rgb >> 8) & 255);
    uint8_t b = (uint8_t)(rgb & 255);
    p[0] = (uint8_t)(b * alpha / 255);
    p[1] = (uint8_t)(g * alpha / 255);
    p[2] = (uint8_t)(r * alpha / 255);
    p[3] = alpha;
    HGDIOBJ old = SelectObject(mem, bmp);
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(dc_, x, y, w, h, mem, 0, 0, 1, 1, bf);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
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

void GdiCanvas::StrokeArc(const Rect& rc, int start, int sweep, uint32_t rgb,
                          int width) {
    
    
    
    
    int rx = rc.w / 2;
    int ry = rc.h / 2;
    if (rx <= 0 || ry <= 0 || sweep == 0) return;
    int cx = rc.x + rx;
    int cy = rc.y + ry;
    int steps = std::max(8, std::abs(sweep) / 6);
    std::vector<POINT> pts;
    pts.reserve((size_t)steps + 1);
    const double kPi = 3.14159265358979323846;
    for (int i = 0; i <= steps; ++i) {
        double a = (start + (double)sweep * i / steps) * kPi / 180.0;
        POINT p{};
        p.x = (LONG)std::lround(cx + rx * std::cos(a));
        p.y = (LONG)std::lround(cy - ry * std::sin(a));
        pts.push_back(p);
    }
    HPEN pen = CreatePen(PS_SOLID, width, RGB((rgb >> 16) & 255,
                                              (rgb >> 8) & 255,
                                              rgb & 255));
    HGDIOBJ old_pen = SelectObject(dc_, pen);
    HBRUSH old_br = (HBRUSH)SelectObject(dc_, GetStockObject(NULL_BRUSH));
    Polyline(dc_, pts.data(), (int)pts.size());
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

    
    
    HDC mem = CreateCompatibleDC(dc_);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = src_w;
    info.bmiHeader.biHeight = -src_h;  
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
    
    
    if (dst_w < src_w || dst_h < src_h) {
        SetStretchBltMode(dc_, HALFTONE);
        SetBrushOrgEx(dc_, dst_x, dst_y, nullptr);
    } else {
        SetStretchBltMode(dc_, COLORONCOLOR);
    }
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

}  