// Allocation-failure probe for the actual terminal queue, before and after R1.
// Global replacement only denies allocations in this thread's publication window.
#include "pg/session.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
#include <thread>
#include <array>

thread_local bool deny_allocation = false;
void *operator new(std::size_t n) {
    if (deny_allocation) throw std::bad_alloc();
    if (void *p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }

template<class Queue>
void publish(Queue &queue, const std::shared_ptr<pg::Job> &job) {
    if constexpr (requires { queue.push(job); }) queue.push(job);
    else queue.push(job->id);
}
template<class Queue>
bool consume(Queue &queue, std::size_t count) {
    if constexpr (requires { queue.pop(); }) {
        for (std::size_t i = 0; i < count; ++i) {
            auto job = queue.pop();
            if (!job || job->id != static_cast<pg::Id>(i + 1)) return false;
        }
        return !queue.pop();
    } else {
        auto ids = queue.drain();
        if (ids.size() != count) return false;
        for (std::size_t i = 0; i < count; ++i)
            if (ids[i] != static_cast<pg::Id>(i + 1)) return false;
        return true;
    }
}
int main() {
    constexpr std::size_t count = 4096;
    pg::CompletionQueue queue;
    std::vector<std::shared_ptr<pg::Job>> jobs;
    jobs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto job = std::make_shared<pg::Job>();
        job->id = static_cast<pg::Id>(i + 1);
        job->status.store(pg::Status::confirmed);
        jobs.push_back(std::move(job));
    }
    std::size_t published = 0;
    deny_allocation = true;
    try {
        for (const auto &job : jobs) {
            publish(queue, job);
            ++published;
        }
    } catch (const std::bad_alloc &) {
        deny_allocation = false;
        std::printf("R1 FAIL: terminal publication threw bad_alloc after %zu of %zu jobs\n",
                    published, count);
        return 1;
    }
    deny_allocation = false;
    if (!consume(queue, count)) {
        std::puts("R1 FAIL: missing, duplicated or reordered terminal jobs");
        return 2;
    }
    // Drop external ownership before destruction: queue must keep and release jobs.
    std::weak_ptr<pg::Job> weak = jobs.front();
    {
        pg::CompletionQueue abandoned;
        for (const auto &job : jobs) publish(abandoned, job);
        jobs.clear();
        if (weak.expired()) return 3;
    }
    if (!weak.expired()) return 4;
    // Concurrent producers, with allocation denial in each publishing thread and
    // in the consumer. FIFO is local to each producer; no global order is assumed.
    constexpr std::size_t producers = 4, per_producer = 1024;
    std::vector<std::shared_ptr<pg::Job>> concurrent;
    for (std::size_t i = 0; i < producers * per_producer; ++i) {
        auto job = std::make_shared<pg::Job>();
        job->id = static_cast<pg::Id>(i);
        concurrent.push_back(std::move(job));
    }
    std::array<std::thread, producers> threads;
    std::atomic<bool> failed{false};
    for (std::size_t p = 0; p < producers; ++p) {
        threads[p] = std::thread([&, p] {
            deny_allocation = true;
            try {
                for (std::size_t i = 0; i < per_producer; ++i)
                    publish(queue, concurrent[p * per_producer + i]);
            } catch (...) { failed.store(true); }
            deny_allocation = false;
        });
    }
    bool ordered = true;
    std::array<std::size_t, producers> next{};
    std::size_t received = 0;
    auto deadline = pg::Clock::now() + std::chrono::seconds(10);
    // This phase uses the fixed queue interface; the historical build has already
    // returned 1 at its first failed allocation above.
    auto concurrent_check = [&]<class Queue>(Queue &q) {
        if constexpr (requires { q.pop(); }) {
            deny_allocation = true;
            while (received < concurrent.size() && pg::Clock::now() < deadline) {
                if (auto job = q.pop()) {
                    auto p = static_cast<std::size_t>(job->id) / per_producer;
                    auto i = static_cast<std::size_t>(job->id) % per_producer;
                    if (p >= producers || i != next[p]++) ordered = false;
                    ++received;
                } else std::this_thread::yield();
            }
            deny_allocation = false;
        }
    };
    concurrent_check(queue);
    for (auto &thread : threads) thread.join();
    if (failed.load() || !ordered || received != concurrent.size()) return 5;
    std::printf("R1 PASS: %zu terminal publications without allocation; FIFO and ownership checked\n", count);
}
