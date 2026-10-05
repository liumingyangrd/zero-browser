#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "app.h"
#include "network.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// --shot 无窗口截图诊断：复用真实 Render / OnLButtonDown 代码路径，把页面画到
// 内存 DC 并存成 BMP。不依赖桌面窗口截图，因此在受限会话里也能取证。
//
//   zero-browser.exe --shot --url <地址> --out <bmp> [--out2 <bmp>]
//                    [--wait 6000] [--after 1500] [--size 1180x820]
//                    [--click X,Y] [--scroll Y]
static int RunShotMode(int argc, char** argv) {
    zb::BrowserApp::ShotOptions opt;
    opt.url = "browser://home";
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* dst) {
            if (i + 1 < argc) *dst = argv[++i];
        };
        if (a == "--url") {
            next(&opt.url);
        } else if (a == "--out") {
            next(&opt.out);
        } else if (a == "--out2") {
            next(&opt.out2);
        } else if (a == "--wait") {
            std::string v;
            next(&v);
            opt.wait_ms = std::atoi(v.c_str());
        } else if (a == "--after") {
            std::string v;
            next(&v);
            opt.after_ms = std::atoi(v.c_str());
        } else if (a == "--size") {
            std::string v;
            next(&v);
            int w = 0;
            int h = 0;
            if (std::sscanf(v.c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                opt.width = w;
                opt.height = h;
            }
        } else if (a == "--scroll") {
            std::string v;
            next(&v);
            opt.scroll = std::atoi(v.c_str());
        } else if (a == "--dump-boxes") {
            opt.dump_boxes = true;
        } else if (a == "--click") {
            std::string v;
            next(&v);
            int x = 0;
            int y = 0;
            if (std::sscanf(v.c_str(), "%d,%d", &x, &y) == 2) {
                opt.clicks.push_back({x, y});
            }
        }
    }
    if (opt.out.empty()) {
        std::printf(
            "用法: zero-browser.exe --shot --url <地址> --out <bmp> "
            "[--out2 <bmp>] [--click X,Y]... [--wait ms] [--after ms] "
            "[--size WxH] [--scroll Y]\n");
        return 1;
    }
    std::printf("shot url=%s size=%dx%d wait=%dms clicks=%zu\n", opt.url.c_str(),
                opt.width, opt.height, opt.wait_ms, opt.clicks.size());
    for (const auto& c : opt.clicks) {
        std::printf("  click (%d,%d)\n", c.first, c.second);
    }
    zb::BrowserApp app(GetModuleHandleA(nullptr));
    bool ok = app.HeadlessShot(opt);
    std::printf(ok ? "shot ok\n" : "shot failed\n");
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    bool shot_mode = argc > 1 && std::strcmp(argv[1], "--shot") == 0;
    bool net_test = argc > 1 && std::strcmp(argv[1], "--net-test") == 0;

    // Console subsystem keeps startup reliable; hide the console and run as GUI.
    if (!net_test && !shot_mode &&
        !GetEnvironmentVariableA("ZB_KEEP_CONSOLE", nullptr, 0)) {
        FreeConsole();
    }

    if (shot_mode) return RunShotMode(argc, argv);

    if (net_test && argc > 2) {
        zb::FetchResult res;
        bool ok = zb::FetchUrl(argv[2], &res, 20000);
        std::printf("call_ok=%d status=%d ok=%d\n", ok ? 1 : 0, res.status,
                    res.ok ? 1 : 0);
        std::printf("final_url=%s\n", res.final_url.c_str());
        std::printf("content_type=%s\n", res.content_type.c_str());
        std::printf("error=%s\n", res.error.c_str());
        std::printf("bytes=%zu\n", res.html.size());
        // 可选第三个参数：把正文写到文件，便于离线检查真实站点返回的内容。
        if (argc > 3 && *argv[3] && !res.html.empty()) {
            FILE* f = std::fopen(argv[3], "wb");
            if (f) {
                std::fwrite(res.html.data(), 1, res.html.size(), f);
                std::fclose(f);
                std::printf("saved=%s\n", argv[3]);
            }
        }
        return ok ? 0 : 1;
    }

    // 两次带 Cookie 的请求验证 jar：第一次吸收 Set-Cookie，
    // 第二次把 Cookie 发回给 echo 接口并打印。
    bool cookie_test = argc > 1 && std::strcmp(argv[1], "--cookie-test") == 0;
    if (cookie_test && argc >= 4) {
        zb::FetchResult first;
        bool ok1 = zb::FetchUrlWithCookies(argv[2], &first, 20000);
        zb::FetchResult second;
        bool ok2 = zb::FetchUrlWithCookies(argv[3], &second, 20000);
        std::printf("set_ok=%d set_status=%d\n", ok1 ? 1 : 0, first.status);
        std::printf("echo_ok=%d echo_status=%d\n", ok2 ? 1 : 0, second.status);
        std::printf("echo_body=%s\n", second.html.c_str());
        auto jar = zb::CookieJarDump();
        std::printf("jar_size=%zu\n", jar.size());
        for (const auto& line : jar) std::printf("cookie\t%s\n", line.c_str());
        return (ok1 && ok2) ? 0 : 1;
    }

    zb::BrowserApp app(GetModuleHandleA(nullptr));
    if (!app.CreateMainWindow()) return 1;

    std::string url = "browser://home";
    if (argc > 1 && argv[1] && *argv[1]) {
        url = argv[1];
        for (int i = 2; i < argc; ++i) {
            url += " ";
            url += argv[i];
        }
    }
    app.NavigateTo(url, true);
    return app.Run();
}
