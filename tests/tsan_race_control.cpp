// Independent detector control: only a real TSan race report qualifies.
#include <atomic>
#include <cstdio>
#include <thread>

#ifndef PG_TSAN_ORDERED_CONTROL
#define PG_TSAN_ORDERED_CONTROL 0
#endif

int main() {
    // Keep scheduling atomics out of the conflicting access's shadow cell.
    alignas(64) volatile int value = 0;
    alignas(64) std::atomic<unsigned> phase{0};
    alignas(64) std::atomic<unsigned> ready{0};
    alignas(64) std::atomic<bool> start{false};
    const auto entered = [&] {
        ready.fetch_add(1, std::memory_order_relaxed);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    };
    std::thread first([&] {
        entered();
        value = 1;
        // Relaxed scheduling lets this write's TSan callback finish before the
        // second callback starts. It creates no synchronizes-with edge.
        phase.store(1, PG_TSAN_ORDERED_CONTROL ? std::memory_order_release
                                             : std::memory_order_relaxed);
    });
    std::thread second([&] {
        entered();
        while (phase.load(PG_TSAN_ORDERED_CONTROL ? std::memory_order_acquire
                                                : std::memory_order_relaxed) != 1)
            std::this_thread::yield();
        value = 2;
    });
    while (ready.load(std::memory_order_relaxed) != 2) std::this_thread::yield();
    std::puts("TSan race control workers_ready=2");
    std::fflush(stdout);
    start.store(true, std::memory_order_release);
    first.join();
    second.join();
    std::puts("TSan race control workers_completed=2");
    return value == 2 ? 0 : 1;
}
