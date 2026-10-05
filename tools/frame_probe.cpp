// frame_probe: inspect directly what MediaPlayer hands back as its frame buffer
// (this tool is not part of the browser itself).
//
// Goal: pin "video black screen" down to one specific stage -- decode, buffer format,
// or composition.
// Output: frame size, buffer byte count, expected byte count (w*h*4), alpha
//         distribution, and sampled pixels of the top/middle/bottom rows, which show
//         whether alpha is 0, whether the image is flipped vertically, and whether
//         there is row padding.
//
// Usage: frame_probe <media URL> [wait milliseconds]

#include "media.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string url = argc > 1 ? argv[1] : "testpage/anim.wmv";
    int wait_ms = argc > 2 ? std::atoi(argv[2]) : 2500;

    zb::MediaPlayer player;
    player.Open(url, true, false, true);

    DWORD end = GetTickCount() + (DWORD)wait_ms;
    std::vector<uint8_t> frame;
    int w = 0;
    int h = 0;
    int got = 0;
    while ((long)(GetTickCount() - end) < 0) {
        player.Update();
        int fw = 0;
        int fh = 0;
        std::vector<uint8_t> f;
        if (player.CopyFrame(&f, &fw, &fh)) {
            frame.swap(f);
            w = fw;
            h = fh;
            got++;
        }
        Sleep(15);
    }

    std::printf("url=%s\n", url.c_str());
    std::printf("ready=%d failed=%d error=%s\n", player.IsReady() ? 1 : 0,
                player.Failed() ? 1 : 0, player.Error().c_str());
    std::printf("frames_copied=%d size=%dx%d bytes=%zu expected=%d\n", got, w, h,
                frame.size(), w * h * 4);
    if (frame.empty() || w <= 0 || h <= 0) {
        std::printf("result: no frame buffer obtained\n");
        return 1;
    }

    int alpha_zero = 0;
    int alpha_255 = 0;
    int alpha_other = 0;
    for (size_t i = 3; i < frame.size(); i += 4) {
        uint8_t a = frame[i];
        if (a == 0) alpha_zero++;
        else if (a == 255) alpha_255++;
        else alpha_other++;
    }
    std::printf("alpha: zero=%d full=%d other=%d (of %d pixels)\n", alpha_zero,
                alpha_255, alpha_other, w * h);

    auto row_stat = [&](const char* tag, int y) {
        size_t base = (size_t)y * w * 4;
        if (base + (size_t)w * 4 > frame.size()) {
            std::printf("%s: out of bounds\n", tag);
            return;
        }
        long long lum = 0;
        int bright = 0;
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = &frame[base + (size_t)x * 4];
            int l = p[0] + p[1] + p[2];
            lum += l;
            if (l > 90) bright++;
        }
        std::printf("%s(y=%d): mean_luma=%lld non_dark_pixels=%d/%d first_pixel BGR=(%d,%d,%d) A=%d\n",
                    tag, y, lum / w, bright, w, frame[base], frame[base + 1],
                    frame[base + 2], frame[base + 3]);
    };
    row_stat("top row", 0);
    row_stat("middle", h / 2);
    row_stat("bottom row", h - 1);

    return 0;
}
