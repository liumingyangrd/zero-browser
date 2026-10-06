// 解释器探针：不依赖 DOM，跑一段脚本并把结果打成可 grep 的行。
//
//   js_probe.exe <script.js>
//   js_probe.exe <script.js> --quiet    只打印错误与统计
//   js_probe.exe --robust               跑内置健壮性用例（死循环/爆栈/异常隔离）
//
// 自测脚本（tools/js_selftest.js）就是靠它做回归：脚本里用 assert 风格，
// 失败会打出 FAIL 行，整体统计在最后一行 SUMMARY。

#include "../src/js.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// 健壮性用例：这些脚本必须"失败得体面"——报错并继续，而不是崩溃、爆栈或卡死。
// 每条用例单独一个 Interp（等价于页面里一段独立脚本）。
struct RobustCase {
    const char* name;
    const char* src;
    bool expect_fail;
};

int RunRobust() {
    const RobustCase cases[] = {
        {"infinite-while", "while (true) {}", true},
        {"infinite-for", "for (;;) {}", true},
        {"runaway-recursion", "function f() { return f(); } f();", true},
        {"deep-finite-recursion",
         "function g(n) { return n <= 0 ? 0 : 1 + g(n - 1); } g(40);", false},
        {"mutual-recursion",
         "function a(n) { return n <= 0 ? 'a' : b(n - 1); }"
         "function b(n) { return n <= 0 ? 'b' : a(n - 1); } a(20);", false},
        {"uncaught-throw", "throw new Error('boom');", true},
        {"throw-string", "throw 'plain';", true},
        {"syntax-error", "function ( {", true},
        {"null-property", "var a = null; a.b;", true},
        {"undefined-call", "var x = 5; x();", true},
        {"catch-recovers",
         "var r = ''; try { null.x; } catch (e) { r = 'ok'; } r;", false},
        {"big-loop-finite",
         "var s = 0; for (var i = 0; i < 20000; i++) { s += i; } s;", false},
        {"normal", "var ok = 1 + 1;", false},
    };
    int pass = 0;
    int fail = 0;
    int total = (int)(sizeof(cases) / sizeof(cases[0]));
    for (const RobustCase& c : cases) {
        zb::Interp interp(nullptr);
        // 用例里故意有死循环：把步数上限压小，跑得快，也顺带验证预算机制本身。
        interp.SetStepLimit(300000);
        bool ok = interp.RunScript(c.src, c.name);
        bool good = (ok != c.expect_fail);
        if (good) {
            ++pass;
            std::printf("ROBUST PASS %-22s ok=%d steps=%lld\n", c.name, ok ? 1 : 0,
                        interp.stats().steps);
        } else {
            ++fail;
            std::printf("ROBUST FAIL %-22s ok=%d err=%s\n", c.name, ok ? 1 : 0,
                        interp.error().c_str());
        }
        std::fflush(stdout);
    }
    std::printf("ROBUST total=%d pass=%d fail=%d\n", total, pass, fail);
    return fail == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    // 不缓冲：崩溃时也要能看到已经跑到哪一条用例（块缓冲会把输出一起丢掉）。
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::printf("用法: js_probe.exe <script.js> [--quiet]\n");
        std::printf("      js_probe.exe --robust\n");
        return 2;
    }
    if (std::strcmp(argv[1], "--robust") == 0) return RunRobust();
    bool quiet = (argc > 2 && std::strcmp(argv[2], "--quiet") == 0);
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::printf("probe: 无法打开 %s\n", argv[1]);
        return 2;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string code = ss.str();

    zb::Interp interp(nullptr);
    bool ok = interp.RunScript(code, argv[1]);
    if (!quiet || !ok) {
        std::printf("probe: script=%s ok=%d steps=%lld scripts=%d failed=%d\n",
                    argv[1], ok ? 1 : 0, interp.stats().steps,
                    interp.stats().scripts, interp.stats().failed);
    }
    if (!ok) {
        std::printf("probe: error=%s\n", interp.error().c_str());
        return 1;
    }
    return 0;
}
