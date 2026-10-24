// Persistent C++ allocation failure in real Lane workers after PostgreSQL confirms dispatch.
#include "pg/engine.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#ifdef __linux__
#include <execinfo.h>
#include <unistd.h>
#endif

#ifndef PG_TEST_ALLOCATIONS
#define PG_TEST_ALLOCATIONS 1
#endif
namespace {
std::atomic<unsigned> denied_allocations{0};
const auto main_thread = std::this_thread::get_id();
std::atomic<bool> deny_worker{false};
}
#if PG_TEST_ALLOCATIONS
void *operator new(std::size_t n) {
    if (deny_worker.load() && std::this_thread::get_id() != main_thread) {
        if (denied_allocations.fetch_add(1) < 8) {
            std::fprintf(stderr, "worker allocation denied bytes=%zu\n", n);
#ifdef __linux__
            void *frames[32];
            auto count = ::backtrace(frames, 32);
            ::backtrace_symbols_fd(frames, count, STDERR_FILENO);
#endif
        }
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
constexpr pg::Owner owner = 908;
void check(bool ok, const char *text) { if (!ok) throw std::runtime_error(text); }
template<class F> void until(pg::Engine &e, F condition) {
    auto deadline = pg::Clock::now() + std::chrono::seconds(10);
    while (!condition()) {
        e.poll();
        check(pg::Clock::now() < deadline, "worker allocation deadline");
        std::this_thread::yield();
    }
}
void run(const char *service, bool shutdown) {
    pg::Engine e;
    auto c = e.connect(owner, service, 1);
    until(e, [&]{ return e.connection(owner,c).state() == pg::State::connected; });
    auto pid_id = e.query(owner,c,"SELECT pg_backend_pid()",0,0,0,false);
    auto pid_job = e.job(owner,pid_id);
    until(e,[&]{return pid_job->delivered;});
    auto pid = pid_job->results.at(0).rows.at(0).at(0).value();
    e.close_job(owner,pid_id);
    const char *keys[] = {"service",nullptr};
    const char *values[] = {service,nullptr};
    PGconn *observer = PQconnectdbParams(keys,values,0);
    check(observer && PQstatus(observer)==CONNECTION_OK,"observer connect failed");
    auto id=e.query(owner,c,"SELECT pg_sleep(60) /* allocation probe */",0,0,0,true);
    auto active=e.job(owner,id);
    auto query="SELECT count(*) FROM pg_stat_activity WHERE pid="+pid+
        " AND state='active' AND wait_event='PgSleep' AND query="
        "'SELECT pg_sleep(60) /* allocation probe */'";
    const auto deadline=pg::Clock::now()+std::chrono::seconds(10);
    for (;;) {
        PGresult *r=PQexec(observer,query.c_str());
        bool reached=r && PQresultStatus(r)==PGRES_TUPLES_OK && PQgetvalue(r,0,0)[0]=='1';
        PQclear(r);
        if(reached) break;
        check(pg::Clock::now()<deadline,"exact SQL dispatch was not observed");
        std::this_thread::yield();
    }
    PQfinish(observer);
    auto queued_id=e.query(owner,c,"SELECT 42",0,0,0,true);
    auto queued=e.job(owner,queued_id);
    // No C++ allocations are permitted in any worker until shutdown completes.
    deny_worker.store(true);
    if(shutdown) e.disconnect(owner,c);
    else active->cancel.store(true);
    unsigned active_deliveries=0, queued_deliveries=0;
    auto terminal_deadline=pg::Clock::now()+std::chrono::seconds(10);
    while(!active->delivered || !queued->delivered) {
        auto d=e.poll();
        for(const auto &job:d.jobs) {
            if(job->id==id) ++active_deliveries;
            if(job->id==queued_id) ++queued_deliveries;
        }
        check(pg::Clock::now()<terminal_deadline,"accepted job was stranded during worker OOM");
        std::this_thread::yield();
    }
    check(active->status.load()==pg::Status::uncertain && active->completed==0,
          "OOM/cancel after dispatch falsely confirmed SQL");
    check(active->items_status == std::vector{pg::ItemStatus::uncertain},
          "OOM after dispatch lost uncertain item metadata");
    check(queued->status.load() == (shutdown ? pg::Status::cancelled : pg::Status::failed) &&
          queued->error == (shutdown ? pg::Error::cancelled : pg::Error::connection),
          "Queued job lost pre-dispatch cause or state");
    check(queued->status.load()!=(pg::Status::confirmed) && queued->completed==0,
          "Queued SQL was dispatched or replayed after the interrupted session");
    check(active_deliveries==1 && queued_deliveries==1,"missing or duplicate terminal callback");
    check(e.connection(owner,c).lanes.at(0)->snapshot().sql_commands==2,
          "Queued SQL was replayed during allocation failure");
    if(!shutdown) {
        deny_worker.store(false);
        until(e,[&]{return e.connection(owner,c).state()==pg::State::connected;});
        auto recovery=e.job(owner,e.query(owner,c,"SELECT 42",0,0,0,false));
        until(e,[&]{return recovery->delivered;});
        check(recovery->status.load()==pg::Status::confirmed &&
              recovery->results.at(0).rows.at(0).at(0)=="42",
              "Explicit new SQL did not recover after allocator restoration");
        e.close_job(owner,recovery->id);
        deny_worker.store(true);
        e.disconnect(owner,c);
    }
    auto stopped_deadline=pg::Clock::now()+std::chrono::seconds(10);
    while(!e.connection(owner,c).lanes.at(0)->stopped.load()) {
        check(pg::Clock::now()<stopped_deadline,"worker shutdown under OOM stalled");
        std::this_thread::yield();
    }
    deny_worker.store(false);
    check(e.poll().jobs.empty(),"late duplicated completion");
    std::printf("worker_recovery PASS allocation_injection=%s mode=%s active_status=%d queued_status=%d callbacks=2\n",
                PG_TEST_ALLOCATIONS ? "ON" : "OFF(TSan)",
                shutdown?"shutdown":"recover",int(active->status.load()),int(queued->status.load()));
}
}
int main() {
#ifdef __linux__
    // Warm the unwinder before denying allocations; trace emission uses no C++ allocation.
    void *frames[1];
    ::backtrace(frames, 1);
#endif
    auto service=std::getenv("OMP_PG_TEST_SERVICE");
    if(!service) return 77;
    try {
        run(service,false);run(service,true);
        check(!PG_TEST_ALLOCATIONS || denied_allocations.load()>0,
              "Allocator denial was configured but never exercised");
        std::printf("worker denied_allocations=%u\n",denied_allocations.load());
        return 0;
    }
    catch(const std::exception &e) {
        deny_worker.store(false);
        std::fprintf(stderr,"worker_allocation FAIL %s\n",e.what());
        return 1;
    }
}
