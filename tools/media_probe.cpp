#include "media.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

namespace {

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

bool SaveBmp(const char* path, const std::vector<uint8_t>& bgra, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    // The alpha channel of 32-bit GDI / MF buffers is usually 0, so saving the buffer
    // as-is makes image viewers treat it as fully transparent. Fill it in as 255 here so
    // the decoded result can be checked by eye after converting to PNG.
    std::vector<uint8_t> copy = bgra;
    for (size_t i = 3; i < copy.size(); i += 4) copy[i] = 255;
    BmpFileHeader fh;
    BmpInfoHeader ih;
    ih.width = w;
    ih.height = -h;  // top-down
    ih.size_image = (uint32_t)copy.size();
    fh.size = sizeof(fh) + sizeof(ih) + (uint32_t)copy.size();
    f.write((const char*)&fh, sizeof(fh));
    f.write((const char*)&ih, sizeof(ih));
    f.write((const char*)copy.data(), (std::streamsize)copy.size());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string url = argc > 1 ? argv[1] : "testmedia/sample.wmv";
    zb::MediaPlayer player;
    player.Open(url, true, false, true);

    for (int i = 0; i < 600; ++i) {
        if (player.Failed()) {
            std::printf("failed: %s\n", player.Error().c_str());
            return 1;
        }
        bool fresh = player.Update();
        std::vector<uint8_t> frame;
        int w = 0, h = 0;
        if (fresh && player.CopyFrame(&frame, &w, &h)) {
            std::printf("frame %dx%d bytes=%zu\n", w, h, frame.size());
            if (SaveBmp("build\\media-frame.bmp", frame, w, h)) {
                std::printf("saved build\\media-frame.bmp\n");
            }
        }
        if (player.IsReady() && w > 0) {
            zb::MediaInfo info = player.Info();
            std::printf("info ready=%d duration=%.2f audio=%d video=%d\n",
                        info.ready ? 1 : 0, info.duration,
                        info.has_audio ? 1 : 0, info.has_video ? 1 : 0);
            for (int step = 0; step < 30; ++step) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                bool more = player.Update();
                std::vector<uint8_t> later;
                int lw = 0, lh = 0;
                if (more && player.CopyFrame(&later, &lw, &lh)) {
                    SaveBmp("build\\media-frame-later.bmp", later, lw, lh);
                }
                std::printf("t=%.2f playing=%d\n", player.Position(),
                            player.IsPlaying() ? 1 : 0);
            }
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    std::printf("timeout: %s\n", player.Error().c_str());
    return 1;
}
