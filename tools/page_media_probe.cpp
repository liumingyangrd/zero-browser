// page_media_probe: 无 GUI 的页面媒体探针。
//
// 目的：完全复用浏览器真实代码路径（Page::ParseHtml -> BuildMediaPlayers ->
// UpdateMedia -> Paint），但把页面画到内存 DC 上再回读像素，从而在没有人眼和
// 窗口截图的条件下给出可复现证据：
//   1. <video> 的属性是否被解析器正确识别（autoplay / loop / muted / controls）
//   2. MediaPlayer 是否 ready、是否在播放、是否产出帧
//   3. 视频区域最终画出来的像素里有多少不是黑色（证明画面真的进了画布）
//
// 用法：
//   page_media_probe <url|file> [--seconds N] [--dump] [--click]

// 注意：windows.h 必须最先包含。gdi.h 里 GdiCanvas::DrawText 会被 windows.h
// 的 DrawTextA/DrawTextW 宏影响，若 gfx.h 先于 windows.h 展开就会出现
// “marked override but does not override”。浏览器本体各编译单元都满足这个
// 顺序，探针也必须保持一致。
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

// 遍历 DOM，打印 <video> 节点的全部属性，并检查 HasAttr 判定结果。
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

// 在布局树里找第一个 <video> 盒子，用于模拟点击与统计像素。
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

// 统计矩形内“非黑”像素数量。返回 -1 表示矩形无效。
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

    std::printf("== DOM 属性检查 ==\n");
    {
        std::unique_ptr<zb::Node> dom = zb::ParseHtml(html);
        DumpVideoNodes(dom.get());
    }

    // 内存画布：GdiCanvas 自建兼容位图，画完之后再 BitBlt 到 DIB 回读像素。
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
    std::printf("== 布局 ==\n");
    std::printf("title=%s content_height=%d\n", page.Data().title.c_str(),
                page.ContentHeight());
    const zb::Box* vbox = FindVideoBox(page.RootBox());
    if (vbox) {
        std::printf("video box rect=%d,%d,%d,%d hidden=%d\n", vbox->rect.x,
                    vbox->rect.y, vbox->rect.w, vbox->rect.h,
                    vbox->hidden ? 1 : 0);
    } else {
        std::printf("video box: 未找到\n");
    }

    if (click && vbox) {
        int cx = vbox->rect.x + vbox->rect.w / 2;
        int cy = vbox->rect.y + vbox->rect.h / 2;
        bool hit = page.MediaClick(cx, cy);
        std::printf("== 模拟点击画面中心 (%d,%d) ==\n MediaClick=%d\n", cx, cy,
                    hit ? 1 : 0);
    }

    std::printf("== 播放时间线 ==\n");
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
            // 只统计“画面区”，排除底部 34px 控件条：否则控件条本身就会贡献
            // 上万个非黑像素，画面全黑时也会误判成“有内容”。
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
                    has ? "" : "(无)");
                if (p->Failed()) std::printf(" err=%s", p->Error().c_str());
                (void)info;
            }
            std::printf(" | videoRectNonBlack=%ld\n", nb);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(tick_ms));
    }

    // 最后一帧存盘作为证据。
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

    std::printf("== 汇总 ==\n");
    std::printf("frames painted=%d\n", painted);
    if (last_fw) std::printf("last frame=%dx%d\n", last_fw, last_fh);
    if (last_non_black >= 0) {
        std::printf(
            "video frame non-black pixels=%ld (不含控件条；0 表示画面全黑)\n",
            last_non_black);
    }

    if (old_dib) SelectObject(dibdc, old_dib);
    if (dib) DeleteObject(dib);
    DeleteDC(dibdc);
    DeleteDC(memdc);
    return painted > 0 ? 0 : 2;
}
