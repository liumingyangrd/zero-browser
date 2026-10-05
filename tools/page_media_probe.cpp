// page_media_probe: GUI-less page media probe.
//
// Goal: reuse the browser's real code path completely (Page::ParseHtml ->
// BuildMediaPlayers -> UpdateMedia -> Paint), but draw the page onto an in-memory DC and
// read the pixels back, so that reproducible evidence can be produced without a human
// eye or a window screenshot:
//   1. whether the <video> attributes are recognized correctly by the parser
//      (autoplay / loop / muted / controls)
//   2. whether the MediaPlayer is ready, whether it is playing, whether it emits frames
//   3. how many of the pixels finally painted in the video area are not black (proving
//      the picture really reached the canvas)
//
// Usage:
//   page_media_probe <url|file> [--seconds N] [--dump] [--click]

// Note: windows.h must be included first. GdiCanvas::DrawText in gdi.h is affected by
// the DrawTextA/DrawTextW macros from windows.h, and if gfx.h is expanded before
// windows.h you get "marked override but does not override". Every translation unit of
// the browser itself satisfies this order, and the probe must stay consistent with it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "engine.h"
#include "gdi.h"
#include "html.h"
#include "network.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

const int kWidth = 1000;
const int kHeight = 800;

std::string ReadFileBinary(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

// Walk the DOM, print every attribute of the <video> nodes, and check the HasAttr
// results.
void DumpVideoNodes(const zb::Node* node) {
    if (!node) return;
    if (node->type == zb::NodeType::Element && node->tag == "video") {
        std::printf("video attrs:");
        for (const auto& kv : node->attrs) {
            std::printf(" %s=\"%s\"", kv.first.c_str(), kv.second.c_str());
        }
        std::printf("\n");
        std::printf("  HasAttr autoplay=%d loop=%d muted=%d controls=%d\n",
                    node->HasAttr("autoplay") ? 1 : 0,
                    node->HasAttr("loop") ? 1 : 0,
                    node->HasAttr("muted") ? 1 : 0,
                    node->HasAttr("controls") ? 1 : 0);
        std::printf("  src=%s\n", node->Attr("src").c_str());
    }
    for (const auto& c : node->children) DumpVideoNodes(c.get());
}

// Find the first <video> box in the layout tree; used for the simulated click and for
// pixel counting.
const zb::Box* FindVideoBox(const zb::Box* box) {
    if (!box) return nullptr;
    if (box->node && box->node->tag == "video") return box;
    for (const auto& c : box->children) {
        if (const zb::Box* found = FindVideoBox(c.get())) return found;
    }
    return nullptr;
}

#pragma pack(push, 1)
struct BmpFileHeader {
    uint16_t type = 0x4D42;
    uint32_t size = 0;
    uint16_t reserved1 = 0;
    uint16_t reserved2 = 0;
    uint32_t off_bits = 54;
};
struct BmpInfoHeader {
    uint32_t size = 40;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t planes = 1;
    uint16_t bit_count = 32;
    uint32_t compression = 0;
    uint32_t size_image = 0;
    int32_t x_ppm = 0;
    int32_t y_ppm = 0;
    uint32_t clr_used = 0;
    uint32_t clr_important = 0;
};
#pragma pack(pop)

bool SaveBmp32(const char* path, const uint8_t* bgra, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    BmpFileHeader fh;
    BmpInfoHeader ih;
    ih.width = w;
    ih.height = -h;  // top-down
    ih.size_image = (uint32_t)((size_t)w * h * 4);
    fh.size = sizeof(fh) + sizeof(ih) + ih.size_image;
    f.write((const char*)&fh, sizeof(fh));
    f.write((const char*)&ih, sizeof(ih));
    f.write((const char*)bgra, (std::streamsize)ih.size_image);
    return f.good();
}

// Count the "non-black" pixels inside a rectangle. Returns -1 if the rectangle is
// invalid.
long CountNonBlack(const uint8_t* bgra, int w, int h, const zb::Rect& r) {
    if (!bgra || r.w <= 0 || r.h <= 0) return -1;
    long count = 0;
    for (int y = r.y; y < r.y + r.h && y < h; ++y) {
        if (y < 0) continue;
        const uint8_t* row = bgra + (size_t)y * w * 4;
        for (int x = r.x; x < r.x + r.w && x < w; ++x) {
            if (x < 0) continue;
            const uint8_t* px = row + (size_t)x * 4;
            if (px[0] > 8 || px[1] > 8 || px[2] > 8) count++;
        }
    }
    return count;
}

}  // namespace

int main(int argc, char** argv) {
    std::string source = argc > 1 ? argv[1] : "video.html";
    double seconds = 3.0;
    bool dump = false;
    bool click = false;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--seconds" && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (a == "--dump") dump = true;
        else if (a == "--click") click = true;
    }

    std::string html;
    std::string url = source;
    if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0) {
        zb::FetchResult res;
        zb::FetchUrl(source, &res, 15000);
        html = res.html;
        if (!res.final_url.empty()) url = res.final_url;
        std::printf("fetch status=%d ok=%d bytes=%zu error=%s\n", res.status,
                    res.ok ? 1 : 0, res.html.size(), res.error.c_str());
    } else {
        html = ReadFileBinary(source);
        std::printf("read bytes=%zu\n", html.size());
    }
    if (html.empty()) {
        std::printf("no html\n");
        return 1;
    }

    std::printf("== DOM attribute check ==\n");
    {
        std::unique_ptr<zb::Node> dom = zb::ParseHtml(html);
        DumpVideoNodes(dom.get());
    }

    // In-memory canvas: GdiCanvas creates its own compatible bitmap, and after painting
    // we BitBlt to the DIB to read the pixels back.
    HDC memdc = CreateCompatibleDC(nullptr);
    if (!memdc) {
        std::printf("CreateCompatibleDC failed\n");
        return 1;
    }
    zb::GdiCanvas canvas(memdc, kWidth, kHeight);

    HDC dibdc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kWidth;
    bi.bmiHeader.biHeight = -kHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* dib_bits = nullptr;
    HBITMAP dib = CreateDIBSection(dibdc, &bi, DIB_RGB_COLORS, &dib_bits,
                                   nullptr, 0);
    HGDIOBJ old_dib = dib ? SelectObject(dibdc, dib) : nullptr;

    zb::Page page;
    page.ParseHtml(html, url);
    page.Relayout(kWidth, kHeight, &canvas);
    std::printf("== layout ==\n");
    std::printf("title=%s content_height=%d\n", page.Data().title.c_str(),
                page.ContentHeight());
    const zb::Box* vbox = FindVideoBox(page.RootBox());
    if (vbox) {
        std::printf("video box rect=%d,%d,%d,%d hidden=%d\n", vbox->rect.x,
                    vbox->rect.y, vbox->rect.w, vbox->rect.h,
                    vbox->hidden ? 1 : 0);
    } else {
        std::printf("video box: not found\n");
    }

    if (click && vbox) {
        int cx = vbox->rect.x + vbox->rect.w / 2;
        int cy = vbox->rect.y + vbox->rect.h / 2;
        bool hit = page.MediaClick(cx, cy);
        std::printf("== simulated click at picture center (%d,%d) ==\n MediaClick=%d\n", cx, cy,
                    hit ? 1 : 0);
    }

    std::printf("== playback timeline ==\n");
    int painted = 0;
    long last_non_black = -1;
    int last_fw = 0;
    int last_fh = 0;
    int tick_ms = 33;
    int ticks = (int)(seconds * 1000.0 / tick_ms);
    for (int i = 0; i < ticks; ++i) {
        bool fresh = page.UpdateMedia();
        if (fresh) painted++;

        canvas.FillRect(0, 0, kWidth, kHeight, 0xffffff);
        page.Paint(&canvas, {0, 0, kWidth, kHeight}, 0);

        if (i % 15 == 0 || i == ticks - 1) {
            BitBlt(dibdc, 0, 0, kWidth, kHeight, memdc, 0, 0, SRCCOPY);
            GdiFlush();
            const uint8_t* bits = (const uint8_t*)dib_bits;
            // Count only the "picture area" and exclude the bottom 34px control bar:
            // otherwise the control bar alone contributes tens of thousands of non-black
            // pixels, and a fully black picture would still be misjudged as "has content".
            zb::Rect frame_only = vbox ? vbox->rect : zb::Rect{};
            if (frame_only.h > 34) frame_only.h -= 34;
            long nb = vbox ? CountNonBlack(bits, kWidth, kHeight, frame_only) : -1;
            last_non_black = nb;

            std::printf("tick=%3d freshFrames=%3d", i, painted);
            for (const auto& kv : page.MediaPlayers()) {
                zb::MediaPlayer* p = kv.second.get();
                if (!p) continue;
                zb::MediaInfo info = p->Info();
                std::vector<uint8_t> f;
                int fw = 0, fh = 0;
                bool has = p->CopyFrame(&f, &fw, &fh);
                last_fw = fw;
                last_fh = fh;
                std::printf(
                    " | playing=%d ready=%d failed=%d %.2f/%.2f frame=%dx%d%s",
                    p->IsPlaying() ? 1 : 0, p->IsReady() ? 1 : 0,
                    p->Failed() ? 1 : 0, p->Position(), p->Duration(), fw, fh,
                    has ? "" : "(none)");
                if (p->Failed()) std::printf(" err=%s", p->Error().c_str());
                (void)info;
            }
            std::printf(" | videoRectNonBlack=%ld\n", nb);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(tick_ms));
    }

    // Save the last frame to disk as evidence.
    canvas.FillRect(0, 0, kWidth, kHeight, 0xffffff);
    page.Paint(&canvas, {0, 0, kWidth, kHeight}, 0);
    BitBlt(dibdc, 0, 0, kWidth, kHeight, memdc, 0, 0, SRCCOPY);
    GdiFlush();
    if (dib_bits) {
        if (SaveBmp32("build\\page-media-last.bmp", (const uint8_t*)dib_bits,
                      kWidth, kHeight)) {
            std::printf("saved build\\page-media-last.bmp\n");
        }
    }

    std::printf("== summary ==\n");
    std::printf("frames painted=%d\n", painted);
    if (last_fw) std::printf("last frame=%dx%d\n", last_fw, last_fh);
    if (last_non_black >= 0) {
        std::printf(
            "video frame non-black pixels=%ld (excluding the control bar; 0 means the picture is fully black)\n",
            last_non_black);
    }

    if (old_dib) SelectObject(dibdc, old_dib);
    if (dib) DeleteObject(dib);
    DeleteDC(dibdc);
    DeleteDC(memdc);
    return painted > 0 ? 0 : 2;
}
