// atomic_double_check: minimal reproduction of std::atomic<double> reliability at -O2
// under 32-bit MinGW.
//
// Background: zero-browser's MediaPlayer uses std::atomic<double> to hold the playback
// position and seek requests. Under -O2 + 32-bit MinGW we observed that after a write
// another thread still read the old value, which showed up as "the progress-bar seek
// does not take effect".
//
// This program protects both the "expected value" and the "mismatch counter" with the
// same mutex, so only the std::atomic<double> under test is an atomic variable.
// Therefore:
//   if some round reads != the expected value, the only possible cause is that the
//   atomic<double> read/write did not take effect correctly.
// Expected output ok=1; any mismatch means atomic<double> cannot be relied upon with
// this toolchain.
//
// Usage: atomic_double_check [rounds]

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

int main(int argc, char** argv) {
    int rounds = argc > 1 ? std::atoi(argv[1]) : 2000;

    struct Shared {
        std::atomic<double> value{0.0};  // object under test
        std::mutex m;                    // guards expected / mismatches / handshake
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
            shared.value = e;  // the atomic write under test
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
