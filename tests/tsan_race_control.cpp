// Intentionally racy independent control. Success means TSan rejects this binary.
#include <atomic>
#include <thread>
int main() {
    int value = 0;
    std::atomic<unsigned> ready{0};
    auto writer = [&] {
        ready.fetch_add(1);
        while (ready.load() != 2) std::this_thread::yield();
        for (int i = 0; i < 10000; ++i) value = i;
    };
    std::thread first(writer), second(writer);
    first.join(); second.join();
    return value == 9999 ? 0 : 1;
}
