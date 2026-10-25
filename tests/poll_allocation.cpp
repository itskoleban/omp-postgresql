// Exercise main-thread Dispatch allocation failure with real accepted Lane jobs.
#include "pg/engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>

#ifndef PG_TEST_ALLOCATIONS
#define PG_TEST_ALLOCATIONS 1
#endif
namespace {
thread_local int allocation_countdown = -1;
thread_local unsigned denied_allocations = 0;
void allocation_checkpoint() {
    if (allocation_countdown >= 0 && allocation_countdown-- == 0) {
        ++denied_allocations;
        throw std::bad_alloc();
    }
}
}
#if PG_TEST_ALLOCATIONS == 1
void *operator new(std::size_t n) {
    allocation_checkpoint();
    if (void *p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
#else
// Keep TSan's allocator interceptors. Only the actual preflight reserve sites throw.
#define PG_POLL_ALLOCATION_CHECKPOINT() allocation_checkpoint()
#include "../src/engine.cpp"
#undef PG_POLL_ALLOCATION_CHECKPOINT
#endif

namespace {
constexpr pg::Owner owner = 909;
void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
template<class Predicate> void until(Predicate predicate) {
    const auto deadline = pg::Clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        check(pg::Clock::now() < deadline, "poll allocation setup deadline");
        std::this_thread::yield();
    }
}
void run(const char *service, int failed_reserve) {
    pg::Engine engine;
    const auto connection = engine.connect(owner, service, 1);
    until([&] { return engine.connection(owner, connection).state() == pg::State::connected; });
    const auto first = engine.job(owner, engine.query(owner, connection, "SELECT 1", 0, 0, 0, true));
    const auto second = engine.job(owner, engine.query(owner, connection, "SELECT 2", 0, 0, 0, true));
    until([&] { return pg::terminal(first->status.load()) && pg::terminal(second->status.load()); });
    check(first->status.load() == pg::Status::confirmed && second->status.load() == pg::Status::confirmed,
          "poll allocation setup SQL was not confirmed");
    engine.disconnect(owner, connection);
    // stopped is published after both terminal queue pushes: no timing assumption.
    until([&] { return engine.connection(owner, connection).lanes.front()->stopped.load(); });

    const auto before = denied_allocations;
    allocation_countdown = failed_reserve;
    bool threw = false;
    pg::Dispatch failed;
    try { failed = engine.poll(); }
    catch (const std::bad_alloc &) { threw = true; }
    allocation_countdown = -1;
    const bool unchanged = !first->delivered && !second->delivered;
    auto recovered = engine.poll();
    unsigned first_deliveries = 0, second_deliveries = 0;
    for (const auto &job : recovered.jobs) {
        first_deliveries += job == first;
        second_deliveries += job == second;
    }
    std::printf("poll_allocation reserve=%d injection=%s denied=%u threw=%d unchanged=%d "
                "recovered_jobs=%zu recovered_connections=%zu\n", failed_reserve,
                PG_TEST_ALLOCATIONS == 2 ? "SITE(TSan)" : "MAIN_THREAD", denied_allocations - before,
                threw, unchanged, recovered.jobs.size(), recovered.connections.size());
    check(denied_allocations - before == 1, "poll allocation fault was not exercised independently");
    check(!threw, "Dispatch allocation failure escaped Engine::poll");
    check(unchanged && failed.jobs.empty() && failed.streams.empty() && failed.connections.empty(),
          "Failed Dispatch allocation consumed observable state");
    check(first_deliveries == 1 && second_deliveries == 1 && recovered.jobs.size() == 2,
          "Restored allocation did not deliver each completion exactly once");
    check(recovered.jobs[0] == first && recovered.jobs[1] == second,
          "Restored allocation reordered terminal completions");
    check(recovered.connections.size() == 1 &&
              recovered.connections.front().state == pg::State::disconnected,
          "Failed Dispatch allocation lost the final connection event");
    auto next = engine.poll();
    check(next.jobs.empty() && next.connections.empty(), "Late duplicate completion or connection event");
}
}

int main() {
    const auto service = std::getenv("OMP_PG_TEST_SERVICE");
    if (!service) return 77;
    try {
        for (int reserve = 0; reserve != 3; ++reserve) run(service, reserve);
        std::puts("poll_allocation PASS independent_reserves=3 terminal_FIFO=preserved connection_event=preserved");
        return 0;
    } catch (const std::exception &error) {
        allocation_countdown = -1;
        std::fprintf(stderr, "poll_allocation FAIL %s\n", error.what());
        return 1;
    }
}
