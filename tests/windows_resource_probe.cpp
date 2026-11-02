// Explicit diagnostic modes for a disposable database; global handle deltas are
// observations, never leak-freedom assertions or a platform-independent threshold.
#include <winsock2.h>
#include "pg/engine.hpp"
#include "windows_resource_handles.hpp"
#include <tlhelp32.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using pg::resource_probe::handle_snapshot;
void modules(const char *point) {
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 3; ++attempt) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
    }
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("Module snapshot failed");
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        throw std::runtime_error("Module enumeration failed");
    }
    do {
        std::printf("module point=%s base=%p bytes=%lu path=%ls\n", point,
                    entry.modBaseAddr, entry.modBaseSize, entry.szExePath);
    } while (Module32NextW(snapshot, &entry));
    const DWORD error = GetLastError();
    CloseHandle(snapshot);
    if (error != ERROR_NO_MORE_FILES) throw std::runtime_error("Module enumeration incomplete");
    std::fflush(stdout);
}
void snapshot(const char *point) {
    handle_snapshot(point);
    DWORD count = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &count))
        throw std::runtime_error("Handle count failed");
    std::printf("control_count point=%s pid=%lu count=%lu\n", point, GetCurrentProcessId(), count);
    modules(point);
}
using Conn = std::unique_ptr<PGconn, decltype(&PQfinish)>;
using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;
void shell_path() {
    using GetFolder = HRESULT (WINAPI *)(HWND, int, HANDLE, DWORD, LPSTR);
    const auto shell = GetModuleHandleW(L"shell32.dll");
    const auto folder = shell ? reinterpret_cast<GetFolder>(GetProcAddress(shell, "SHGetFolderPathA")) : nullptr;
    if (!folder) throw std::runtime_error("Linked shell folder API unavailable");
    char path[MAX_PATH]{};
    // Match libpq's Windows pqGetHomeDirectory call without printing personal data.
    if (FAILED(folder(nullptr, 0x001a, nullptr, 0, path)))
        throw std::runtime_error("Shell AppData lookup failed");
}
void winsock(bool inspect) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("Winsock startup failed");
    try {
        if (inspect) snapshot("first_active");
    } catch (...) { WSACleanup(); throw; }
    if (WSACleanup()) throw std::runtime_error("Winsock cleanup failed");
}
Conn direct(const char *service, bool tls) {
    const char *keys[] = {"service", "sslmode", "gssencmode", "connect_timeout", nullptr};
    const char *values[] = {service, tls ? "verify-full" : "disable", "disable", "3", nullptr};
    Conn connection(PQconnectdbParams(keys, values, 0), &PQfinish);
    if (!connection || PQstatus(connection.get()) != CONNECTION_OK)
        throw std::runtime_error("Control connection failed");
    if (bool(PQsslInUse(connection.get())) != tls)
        throw std::runtime_error("Control transport does not match requested TLS mode");
    Result result(PQexec(connection.get(), "SELECT 42"), &PQclear);
    if (!result || PQresultStatus(result.get()) != PGRES_TUPLES_OK ||
        PQntuples(result.get()) != 1 || std::string(PQgetvalue(result.get(), 0, 0)) != "42")
        throw std::runtime_error("Direct control SQL failed");
    return connection;
}
template<class Condition> void wait(pg::Engine &engine, Condition condition) {
    const auto deadline = pg::Clock::now() + std::chrono::seconds(5);
    while (!condition()) {
        engine.poll();
        if (pg::Clock::now() >= deadline) throw std::runtime_error("Engine control deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
std::unique_ptr<pg::Engine> connected_engine(const char *service) {
    auto engine = std::make_unique<pg::Engine>();
    const auto connection = engine->connect(1, service, 4);
    wait(*engine, [&] { return engine->connection(1, connection).ready() == 4; });
    for (int lane = 0; lane < 4; ++lane) {
        const auto id = engine->query(1, connection, "SELECT 42", lane, 0, 0, false);
        const auto job = engine->job(1, id);
        wait(*engine, [&] { return job->delivered; });
        if (job->status.load() != pg::Status::confirmed || job->results.size() != 1 ||
            job->results[0].rows.size() != 1 || job->results[0].rows[0][0] != "42")
            throw std::runtime_error("Engine control SQL failed");
        engine->close_job(1, id);
    }
    return engine;
}
void threads(bool inspect) {
    std::atomic<bool> release{false};
    std::array<std::thread, 4> workers;
    try {
        for (auto &worker : workers) worker = std::thread([&] {
            while (!release.load()) std::this_thread::yield();
        });
        if (inspect) snapshot("first_active");
    } catch (...) {
        release.store(true);
        for (auto &worker : workers) if (worker.joinable()) worker.join();
        throw;
    }
    release.store(true);
    for (auto &worker : workers) worker.join();
}
void direct_thread(const char *service, bool inspect) {
    std::exception_ptr failure;
    std::atomic<bool> connected{false}, release{false};
    std::thread worker([&] {
        try {
            auto connection = direct(service, true);
            connected.store(true);
            while (!release.load()) std::this_thread::yield();
        } catch (...) { failure = std::current_exception(); connected.store(true); }
    });
    while (!connected.load()) std::this_thread::yield();
    try {
        if (inspect) snapshot("first_active");
    } catch (...) { release.store(true); worker.join(); throw; }
    release.store(true);
    worker.join();
    if (failure) std::rethrow_exception(failure);
}
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    const char *service = std::getenv("OMP_PG_TEST_SERVICE");
    if (!service) return 2;
    try {
        std::printf("control mode=%s pointer_bytes=%zu libpq=%d pid=%lu\n", mode.c_str(),
                    sizeof(void *), PQlibVersion(), GetCurrentProcessId());
        snapshot("startup");
        snapshot("observer_warmup1");
        snapshot("observer_warmup2");
        snapshot("warmed_baseline");
        if (mode == "observer") {
            snapshot("first_active");
            snapshot("first_destroyed");
            for (int i = 0; i < 10; ++i) handle_snapshot("observer_repeat");
        } else if (mode == "empty-engine") {
            auto engine = std::make_unique<pg::Engine>();
            snapshot("first_active");
            engine.reset();
            snapshot("first_destroyed");
            for (int i = 0; i < 10; ++i) { pg::Engine next; }
        } else if (mode == "thread") {
            threads(true);
            snapshot("first_destroyed");
            for (int i = 0; i < 10; ++i) threads(false);
        } else if (mode == "shell") {
            shell_path();
            snapshot("first_active");
            snapshot("first_destroyed");
            for (int i = 0; i < 10; ++i) shell_path();
        } else if (mode == "winsock") {
            winsock(true);
            snapshot("first_destroyed");
            for (int i = 0; i < 10; ++i) winsock(false);
        } else if (mode == "libpq-thread") {
            direct_thread(service, true);
            snapshot("first_destroyed");
            for (int i = 0; i < 10; ++i) direct_thread(service, false);
        } else if (mode == "libpq-plain" || mode == "libpq-tls") {
            auto connection = direct(service, mode == "libpq-tls");
            snapshot("first_active");
            connection.reset();
            snapshot("first_destroyed");
            for (int i = 0; i < 10; ++i) { auto next = direct(service, mode == "libpq-tls"); }
        } else if (mode == "engine") {
            auto engine = connected_engine(service);
            snapshot("first_active");
            engine.reset();
            snapshot("first_destroyed");
            for (int i = 0; i < 10; ++i) { auto next = connected_engine(service); }
        } else throw std::runtime_error("Unknown control mode");
        snapshot("repeated10_destroyed");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        snapshot("settled_destroyed");
        std::printf("control PASS mode=%s\n", mode.c_str());
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "control FAIL mode=%s message=%s\n", mode.c_str(), error.what());
        return 1;
    }
}
