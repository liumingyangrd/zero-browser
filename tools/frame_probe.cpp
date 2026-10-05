// frame_probe: 直接检查 MediaPlayer 交出来的帧缓冲内容（不参与浏览器本体）。
//
// 目的：把“视频黑屏”定位到具体一段——解码、缓冲格式、还是合成。
// 输出：帧尺寸、缓冲字节数、期望字节数（w*h*4）、alpha 分布、
//       顶部/中部/底部行的采样像素，用于判断 alpha 是否为 0、是否上下翻转、是否有行填充。
//
// 用法: frame_probe <媒体URL> [等待毫秒]

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
        std::printf("结果：没有取到帧缓冲\n");
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
    std::printf("alpha: zero=%d full=%d other=%d (共 %d 像素)\n", alpha_zero,
                alpha_255, alpha_other, w * h);

    auto row_stat = [&](const char* tag, int y) {
        size_t base = (size_t)y * w * 4;
        if (base + (size_t)w * 4 > frame.size()) {
            std::printf("%s: 越界\n", tag);
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
        std::printf("%s(y=%d): 平均亮度=%lld 非暗像素=%d/%d 首个像素 BGR=(%d,%d,%d) A=%d\n",
                    tag, y, lum / w, bright, w, frame[base], frame[base + 1],
                    frame[base + 2], frame[base + 3]);
    };
    row_stat("顶行", 0);
    row_stat("中部", h / 2);
    row_stat("底行", h - 1);

    return 0;
}
