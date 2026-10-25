// Internal callback diagnostics must not consume later accepted deliveries.
#include "pawn_memory.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>

#ifndef PG_TEST_ALLOCATIONS
#define PG_TEST_ALLOCATIONS 1
#endif
namespace {
enum class Fault { none, error, lookup };
thread_local Fault selected_fault = Fault::none;
thread_local int allocation_countdown = -1;
thread_local unsigned denied_allocations = 0;
thread_local PawnMemory *active_vm = nullptr;
void allocation_checkpoint(Fault site) {
    if (selected_fault != site) return;
    selected_fault = Fault::none;
    if (active_vm) {
        active_vm->push_error_after = -1;
        active_vm->exec_error = 0;
    }
#if PG_TEST_ALLOCATIONS == 1
    allocation_countdown = 0;
#else
    ++denied_allocations;
    throw std::bad_alloc();
#endif
}
}
#if PG_TEST_ALLOCATIONS == 1
void *operator new(std::size_t n) {
    if (allocation_countdown >= 0 && allocation_countdown-- == 0) {
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
// The TSan variant throws at the same internal boundaries and retains its allocator.
#define PG_CALLBACK_ERROR_CHECKPOINT() allocation_checkpoint(Fault::error)
#define PG_CALLBACK_LOOKUP_CHECKPOINT() allocation_checkpoint(Fault::lookup)
#include "../src/component.cpp"
#undef PG_CALLBACK_ERROR_CHECKPOINT
#undef PG_CALLBACK_LOOKUP_CHECKPOINT

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template<class Predicate> void until(Predicate predicate) {
    auto deadline = pg::Clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        check(pg::Clock::now() < deadline, "Callback allocation test deadline");
        std::this_thread::yield();
    }
}
auto frame(const AMX &amx) {
    return std::array<cell, 9>{amx.stk, amx.hea, amx.cip, amx.frm, amx.paramcount,
                             amx.reset_stk, amx.reset_hea, amx.pri, amx.alt};
}
struct Fixture {
    pg::Component component{pg::LogLevel::off};
    PawnMemory vm;
    pg::Owner owner;
    pg::Id connection;
    Fixture(const char *service) {
        component.onAmxLoad(vm);
        vm.callbacks = true;
        vm.publics = {{"First", 1}, {"Middle", 2}, {"Done", 3}};
        owner = component.scripts.at(&vm.amx).owner;
        connection = component.engine.connect(owner, service, 1);
        until([&] { return component.engine.connection(owner, connection).state() == pg::State::connected; });
    }
    std::shared_ptr<pg::Job> query(const char *name) {
        auto callback = std::make_unique<pg::CallbackDescriptor>();
        callback->function = name;
        return component.engine.job(owner, component.engine.query(
            owner, connection, "SELECT 1", 0, 0, 0, true, std::move(callback)));
    }
    void stop() {
        component.engine.disconnect(owner, connection);
        until([&] { return component.engine.connection(owner, connection).lanes.front()->stopped.load(); });
    }
    unsigned calls(const char *name) {
        return static_cast<unsigned>(std::count_if(vm.calls.begin(), vm.calls.end(),
            [&](const auto &call) { return call.first == name; }));
    }
};

void error_storage(const char *service, int mode) {
    Fixture fixture(service);
    auto first = fixture.query("First"), second = fixture.query("Done");
    until([&] { return pg::terminal(first->status.load()) && pg::terminal(second->status.load()); });
    check(first->status == pg::Status::confirmed && second->status == pg::Status::confirmed,
          "Callback allocation SQL did not confirm");
    fixture.stop(); // All terminal queue pushes have completed before allocation testing.
    if (mode == 0) fixture.vm.publics.erase("First");
    if (mode == 1) fixture.vm.push_error_after = 0;
    if (mode == 2) fixture.vm.exec_error = AMX_ERR_NATIVE;
    std::string{}.swap(fixture.component.scripts.at(&fixture.vm.amx).error);
    const auto saved = frame(fixture.vm.amx);
    const auto before = denied_allocations;
    active_vm = &fixture.vm;
    selected_fault = Fault::error;
    bool escaped = false;
    try { fixture.component.onTick({}, {}); }
    catch (const std::bad_alloc &) { escaped = true; }
    allocation_countdown = -1;
    selected_fault = Fault::none;
    active_vm = nullptr;
    const auto count = fixture.vm.calls.size();
    fixture.component.onTick({}, {});
    std::printf("callback_allocation mode=%d injection=%s denied=%u escaped=%d later_calls=%u frame_restored=%d\n",
                mode, PG_TEST_ALLOCATIONS == 1 ? "MAIN_THREAD" : "SITE(TSan)",
                denied_allocations - before, escaped, fixture.calls("Done"), frame(fixture.vm.amx) == saved);
    check(denied_allocations - before == 1, "Callback allocation site was not exercised independently");
    check(!escaped, "Callback diagnostic allocation escaped onTick");
    check(fixture.calls("Done") == 1 && fixture.calls("First") == (mode == 2 ? 1u : 0u),
          "Callback allocation lost or duplicated accepted delivery");
    check(count == fixture.vm.calls.size(), "Callback allocation caused late duplicate delivery");
    check(frame(fixture.vm.amx) == saved, "Callback failure did not restore the complete VM frame");
    check(first->delivered && second->delivered && !first->callback && !second->callback,
          "Callback allocation retained consumed delivery metadata");
    check(first->status == pg::Status::confirmed && second->status == pg::Status::confirmed,
          "Callback allocation changed SQL confirmation");
    check(fixture.component.scripts.at(&fixture.vm.amx).error.empty(),
          "Unavailable callback diagnostic did not use the defined empty fallback");
}

void stale_handle(const char *service) {
    Fixture fixture(service);
    auto first = fixture.query("First"), middle = fixture.query("Middle"), last = fixture.query("Done");
    until([&] { return pg::terminal(first->status.load()) && pg::terminal(middle->status.load()) &&
                       pg::terminal(last->status.load()); });
    check(first->status == pg::Status::confirmed && middle->status == pg::Status::confirmed &&
          last->status == pg::Status::confirmed, "Stale-handle SQL did not confirm");
    fixture.stop();
    const auto saved = frame(fixture.vm.amx);
    bool released = false;
    fixture.vm.on_exec = [&] {
        if (released) return;
        released = true;
        fixture.component.engine.close_job(fixture.owner, middle->id);
        selected_fault = Fault::lookup;
    };
    const auto before = denied_allocations;
    bool escaped = false;
    try { fixture.component.onTick({}, {}); }
    catch (const std::bad_alloc &) { escaped = true; }
    allocation_countdown = -1;
    selected_fault = Fault::none;
    const auto count = fixture.vm.calls.size();
    fixture.component.onTick({}, {});
    std::printf("callback_allocation stale_handle injection=%s denied=%u escaped=%d later_calls=%u\n",
                PG_TEST_ALLOCATIONS == 1 ? "MAIN_THREAD" : "SITE(TSan)",
                denied_allocations - before, escaped, fixture.calls("Done"));
    check(denied_allocations - before == 1 && !escaped, "Stale-handle diagnostic allocation escaped");
    check(released && fixture.calls("First") == 1 && fixture.calls("Middle") == 0 && fixture.calls("Done") == 1,
          "Stale-handle failure prevented a later valid callback");
    check(count == fixture.vm.calls.size() && frame(fixture.vm.amx) == saved,
          "Stale-handle recovery duplicated delivery or corrupted the VM frame");
    check(last->delivered && !last->callback && last->status == pg::Status::confirmed,
          "Stale-handle recovery changed the later completion");
}
}

int main(int argc, char **argv) {
    const auto service = std::getenv("OMP_PG_TEST_SERVICE");
    if (!service) return 77;
    try {
        if (argc == 1) {
            for (int mode = 0; mode != 3; ++mode) error_storage(service, mode);
            stale_handle(service);
        } else if (argc == 3 && std::string_view(argv[1]) == "--scenario") {
            const auto scenario = std::string_view(argv[2]);
            if (scenario == "missing") error_storage(service, 0);
            else if (scenario == "push") error_storage(service, 1);
            else if (scenario == "exec") error_storage(service, 2);
            else if (scenario == "stale") stale_handle(service);
            else throw std::runtime_error("Unknown callback allocation scenario");
        } else {
            throw std::runtime_error("Expected no arguments or --scenario missing|push|exec|stale");
        }
        std::printf("callback_allocation PASS independent_sites=%d later_delivery=preserved VM_frame=restored\n",
                    argc == 1 ? 4 : 1);
        return 0;
    } catch (const std::exception &error) {
        allocation_countdown = -1;
        selected_fault = Fault::none;
        active_vm = nullptr;
        std::fprintf(stderr, "callback_allocation FAIL %s\n", error.what());
        return 1;
    }
}
