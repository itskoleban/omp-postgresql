// Owner teardown must remain effective when the main-thread allocator is unavailable.
#include "pg/engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string_view>
#include <thread>

#ifndef PG_TEST_ALLOCATIONS
#define PG_TEST_ALLOCATIONS 1
#endif
namespace {
thread_local bool deny_allocation = false;
thread_local unsigned denied_allocations = 0;
}
#if PG_TEST_ALLOCATIONS == 1
void *operator new(std::size_t n) {
    if (deny_allocation) {
        ++denied_allocations;
        throw std::bad_alloc();
    }
    if (void *p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
#endif

namespace {
constexpr pg::Owner owner = 1011, other_owner = 1012;
void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
template<class Predicate> void until(Predicate predicate) {
    const auto deadline = pg::Clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        check(pg::Clock::now() < deadline, "Owner unload deadline");
        std::this_thread::yield();
    }
}
template<class Function> void invalid(Function function) {
    try {
        function();
    } catch (const pg::Failure &error) {
        check(error.code == pg::Error::invalid, "Retired resource returned wrong error");
        return;
    }
    throw std::runtime_error("Unloaded owner retained a resource handle");
}
void unload(pg::Engine &engine, pg::Owner who) {
    const auto before = denied_allocations;
    bool escaped = false;
    deny_allocation = true;
    try {
        engine.unload(who);
    } catch (...) {
        escaped = true;
    }
    deny_allocation = false;
    std::printf("owner_unload owner=%llu allocator=%s denied=%u escaped=%d\n",
                static_cast<unsigned long long>(who),
                PG_TEST_ALLOCATIONS == 1 ? "DENIED" : "TSAN_RETAINED",
                denied_allocations - before, escaped);
    check(!escaped, "Owner unload escaped while allocation was unavailable");
    check(denied_allocations == before, "Owner unload attempted to allocate");
}
void unit() {
#if PG_TEST_ALLOCATIONS == 1
    bool rejected = false;
    deny_allocation = true;
    try {
        auto *memory = ::operator new(1);
        ::operator delete(memory);
    } catch (const std::bad_alloc &) {
        rejected = true;
    }
    deny_allocation = false;
    check(rejected, "Allocation denial control did not reject an allocation");
#endif
    pg::Engine engine;
    // The first unload previously allocated a tombstone even for an empty owner.
    // Repeated calls also verify that no accumulating owner registry is required.
    for (pg::Owner who = owner; who != owner + 32; ++who) {
        unload(engine, who);
        unload(engine, who);
    }
    check(engine.poll().jobs.empty(), "Empty owner unload fabricated a completion");
}
std::unique_ptr<pg::CallbackDescriptor> callback() {
    auto descriptor = std::make_unique<pg::CallbackDescriptor>();
    descriptor->function = "OnUnloadedOwner";
    descriptor->arguments.emplace_back(std::string(128, 'x'));
    return descriptor;
}
void integration(const char *service) {
    pg::Engine engine;
    const auto connection = engine.connect(owner, service, 1);
    const auto other_connection = engine.connect(other_owner, service, 1);
    until([&] {
        return engine.connection(owner, connection).state() == pg::State::connected &&
               engine.connection(other_owner, other_connection).state() == pg::State::connected;
    });
    const auto statement = engine.prepare(owner, connection, "SELECT $1", 1);
    const auto batch = engine.create_batch(owner, connection);
    const auto other_statement = engine.prepare(other_owner, other_connection, "SELECT 7", 0);
    const auto other_batch = engine.create_batch(other_owner, other_connection);
    auto retained = engine.job(owner, engine.query(owner, connection, "SELECT pg_backend_pid()",
                                                   0, 0, 0, true, callback()));
    until([&] { return pg::terminal(retained->status.load()); });
    check(retained->status.load() == pg::Status::confirmed, "Backend PID did not confirm");
    const auto pid = retained->results.at(0).rows.at(0).at(0).value();
    auto active = engine.job(owner, engine.query(owner, connection,
        "SELECT pg_sleep(60) /* owner unload allocation */", 0, 0, 0, true, callback()));
    const char *keys[] = {"service", nullptr};
    const char *values[] = {service, nullptr};
    std::unique_ptr<PGconn, decltype(&PQfinish)> observer(PQconnectdbParams(keys, values, 0), PQfinish);
    check(observer && PQstatus(observer.get()) == CONNECTION_OK, "Observer did not connect");
    const auto observed_sql = "SELECT count(*) FROM pg_stat_activity WHERE pid=" + pid +
        " AND state='active' AND wait_event='PgSleep' AND query="
        "'SELECT pg_sleep(60) /* owner unload allocation */'";
    until([&] {
        std::unique_ptr<PGresult, decltype(&PQclear)> result(PQexec(observer.get(), observed_sql.c_str()), PQclear);
        check(result && PQresultStatus(result.get()) == PGRES_TUPLES_OK, "Observer query failed");
        return PQgetvalue(result.get(), 0, 0)[0] == '1';
    });
    observer.reset();
    auto queued = engine.job(owner, engine.query(owner, connection, "SELECT 42", 0, 0, 0,
                                                 true, callback()));
    auto other = engine.job(other_owner, engine.query(other_owner, other_connection, "SELECT 7",
                                                       0, 0, 0, true, callback()));
    unload(engine, owner);
    check(!retained->notify && !active->notify && !queued->notify &&
              !retained->callback && !active->callback && !queued->callback,
          "Unload retained callbacks or enabled notification");
    check(retained->cancel.load() && active->cancel.load() && queued->cancel.load(),
          "Unload did not request cancellation for every accepted job");
    invalid([&] { engine.statement(owner, statement); });
    invalid([&] { engine.close_batch(owner, batch); });
    engine.statement(other_owner, other_statement);
    unsigned other_deliveries = 0;
    bool closed = false;
    std::uint64_t sql_commands = 0;
    until([&] {
        auto dispatch = engine.poll();
        for (const auto &job : dispatch.jobs) {
            check(job->owner != owner, "Unloaded owner received a completion");
            if (job == other) ++other_deliveries;
        }
        for (const auto &job : dispatch.streams) {
            check(job->owner != owner, "Unloaded owner received stream notification");
        }
        for (const auto &event : dispatch.connections) {
            if (event.connection == connection && event.state == pg::State::disconnected) {
                closed = true;
                sql_commands = engine.connection(owner, connection).lanes.front()->snapshot().sql_commands;
            }
        }
        return closed && pg::terminal(active->status.load()) && pg::terminal(queued->status.load()) &&
               other->delivered;
    });
    // Reap the final connection event and all terminal unloaded resources.
    auto final = engine.poll();
    check(final.jobs.empty() && final.streams.empty(), "Unload produced late duplicate notifications");
    invalid([&] { engine.job(owner, retained->id); });
    invalid([&] { engine.job(owner, active->id); });
    invalid([&] { engine.job(owner, queued->id); });
    invalid([&] { engine.connection(owner, connection); });
    check(active->status.load() == pg::Status::uncertain && active->completed == 0,
          "Dispatched SQL lost its uncertain outcome");
    check(queued->status.load() == pg::Status::cancelled && queued->completed == 0,
          "Queued SQL did not cancel before dispatch");
    check(sql_commands == 2, "Queued SQL was dispatched or uncertain SQL was replayed");
    check(retained->status.load() == pg::Status::confirmed && retained->completed == 1 &&
              retained->results.at(0).rows.at(0).at(0) == pid,
          "Unload changed an independently retained confirmed result");
    check(other_deliveries == 1 && other->status.load() == pg::Status::confirmed &&
              other->results.at(0).rows.at(0).at(0) == "7",
          "Other owner's accepted work changed during unload");
    engine.close_job(other_owner, other->id);
    engine.close_statement(other_owner, other_statement);
    engine.close_batch(other_owner, other_batch);
    unload(engine, owner);
    std::puts("owner_unload resources=retired callbacks=suppressed SQL=no_replay other_owner=preserved");
}
}

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--unit") {
            unit();
        } else {
            const auto service = std::getenv("OMP_PG_TEST_SERVICE");
            if (!service) return 77;
            integration(service);
        }
        std::puts("owner_unload PASS");
        return 0;
    } catch (const std::exception &error) {
        deny_allocation = false;
        std::fprintf(stderr, "owner_unload FAIL %s\n", error.what());
        return 1;
    }
}
