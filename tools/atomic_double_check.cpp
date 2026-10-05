// atomic_double_check: 最小复现 32 位 MinGW 下 std::atomic<double> 在 -O2 的可靠性。
//
// 背景：zero-browser 的 MediaPlayer 用 std::atomic<double> 保存播放位置与跳转请求。
// 在 -O2 + 32 位 MinGW 下观察到：写入后另一线程读到旧值，表现为“进度条跳转不生效”。
//
// 本程序把“期望值”和“失配计数”都放在同一把互斥量里保护，只有被测试的
// std::atomic<double> 本身是原子变量。因此：
//   若某轮读到 != 期望值，只可能是 atomic<double> 的读写没有正确生效。
// 期望输出 ok=1；出现 mismatch 说明该工具链下不能依赖 atomic<double>。
//
// 用法: atomic_double_check [轮数]

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

int main(int argc, char** argv) {
    int rounds = argc > 1 ? std::atoi(argv[1]) : 2000;

    struct Shared {
        std::atomic<double> value{0.0};  // 被测试对象
        std::mutex m;                    // 保护 expected / mismatches / 握手
        std::condition_variable cv;
        double expected = 0.0;
        long long mismatches = 0;
        bool go = false;
        bool done = false;
    } shared;

    std::thread reader([&shared] {
        for (;;) {
            double v = 0.0;
            double want = 0.0;
            {
                std::unique_lock<std::mutex> lock(shared.m);
                shared.cv.wait(lock, [&shared] { return shared.go; });
                if (shared.done) return;
                v = shared.value.load();
                want = shared.expected;
                if (v != want) shared.mismatches++;
                shared.go = false;
            }
            shared.cv.notify_all();
        }
    });

    for (int i = 0; i < rounds; ++i) {
        double e = 100.0 + i * 0.5;
        {
            std::lock_guard<std::mutex> lock(shared.m);
            shared.expected = e;
            shared.value = e;  // 被测试的原子写入
            shared.go = true;
        }
        shared.cv.notify_all();
        {
            std::unique_lock<std::mutex> lock(shared.m);
            shared.cv.wait(lock, [&shared] { return !shared.go; });
        }
    }

    long long bad = 0;
    {
        std::lock_guard<std::mutex> lock(shared.m);
        shared.done = true;
        shared.go = true;
        bad = shared.mismatches;
    }
    shared.cv.notify_all();
    reader.join();

    std::printf("rounds=%d mismatches=%lld ok=%d\n", rounds, bad,
                bad == 0 ? 1 : 0);
    return bad == 0 ? 0 : 1;
}
