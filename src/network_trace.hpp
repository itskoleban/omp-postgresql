#pragma once

// Diagnostic-only observation. The normal component has no trace calls or state.
#ifdef PG_NETWORK_DIAGNOSTICS
#include "pg/types.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <libpq-fe.h>
#include <sched.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace pg::network_trace {
inline constexpr std::size_t capacity = 65536;
struct Record {
	const char *phase;
	std::int64_t at, utc, deadline;
	std::uintptr_t job_ptr, lane_ptr, conn_ptr;
	std::uint64_t scenario, command, data;
	Id job, connection, transaction;
	int lane, backend, socket, tx, rc, tid, cpu;
};
inline Record records[capacity];
inline std::atomic<std::size_t> count{0};
inline std::atomic<std::uint64_t> scenario{0}, commands{0};
inline std::atomic<bool> active{false};
inline thread_local std::uint64_t command = 0;
inline std::int64_t ns(Clock::time_point value) noexcept {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(value.time_since_epoch()).count();
}
inline std::uint64_t hash(const std::string &text) noexcept {
	std::uint64_t value = 14695981039346656037ull;
	for (unsigned char c : text) {
		value = (value ^ c) * 1099511628211ull;
	}
	return value;
}
inline void record(const char *phase, const Job *job = nullptr, const void *lane = nullptr,
                   PGconn *conn = nullptr, Clock::time_point deadline = {}, int rc = 0,
                   std::uint64_t data = 0, Clock::time_point when = {}) noexcept {
	if (!active.load(std::memory_order_relaxed)) {
		return;
	}
	const int saved = errno;
	const auto at = when == Clock::time_point{} ? Clock::now() : when;
	timespec utc{};
	clock_gettime(CLOCK_REALTIME, &utc);
	if (std::strcmp(phase, "deadline") == 0) {
		command = commands.fetch_add(1) + 1;
	}
	const auto index = count.fetch_add(1, std::memory_order_relaxed);
	if (index < capacity) {
		records[index] = {phase,
		                  ns(at),
		                  utc.tv_sec * 1000000000ll + utc.tv_nsec,
		                  ns(deadline),
		                  reinterpret_cast<std::uintptr_t>(job),
		                  reinterpret_cast<std::uintptr_t>(lane),
		                  reinterpret_cast<std::uintptr_t>(conn),
		                  scenario.load(std::memory_order_relaxed),
		                  command,
		                  data,
		                  job ? job->id : 0,
		                  job ? job->connection : 0,
		                  job ? job->transaction : 0,
		                  job ? job->lane : -1,
		                  conn ? PQbackendPID(conn) : 0,
		                  conn ? PQsocket(conn) : -1,
		                  conn ? int(PQtransactionStatus(conn)) : -1,
		                  rc,
		                  int(syscall(SYS_gettid)),
		                  sched_getcpu()};
	}
	errno = saved;
}
inline void mark_scenario(const std::string &name) noexcept {
	scenario.store(hash(name), std::memory_order_relaxed);
	record("scenario");
}

// Workers record into distinct fixed slots. Flush happens after all test Engines
// have been destroyed, including exception unwinding; no trace I/O is on a query path.
class Capture {
	int fd_ = -1;
	bool finished_ = false, ok_ = true;
	bool write_all(const char *text, std::size_t size) noexcept {
		while (size) {
			const auto n = ::write(fd_, text, size);
			if (n < 0 && errno == EINTR) {
				continue;
			}
			if (n <= 0) {
				return false;
			}
			text += n;
			size -= static_cast<std::size_t>(n);
		}
		return true;
	}

public:
	Capture() noexcept {
		const char *path = std::getenv("OMP_PG_NETWORK_TRACE");
		if (!path) {
			ok_ = false;
			return;
		}
		fd_ = ::open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		if (fd_ < 0) {
			ok_ = false;
			return;
		}
		char line[256];
		const int size = std::snprintf(
		    line, sizeof(line),
		    "{\"type\":\"header\",\"schema\":1,\"pid\":%d,\"query_ms\":1500,\"capacity\":%zu}\n",
		    int(getpid()), capacity);
		ok_ = size > 0 && write_all(line, static_cast<std::size_t>(size));
		active.store(ok_, std::memory_order_relaxed);
	}
	bool valid() const noexcept {
		return ok_;
	}
	bool finish() noexcept {
		if (finished_) {
			return ok_;
		}
		finished_ = true;
		active.store(false, std::memory_order_relaxed);
		if (fd_ < 0) {
			return false;
		}
		const auto total = count.load();
		for (std::size_t i = 0; i < std::min(total, capacity) && ok_; ++i) {
			const auto &r = records[i];
			char line[1024];
			const int size = std::snprintf(
			    line, sizeof(line),
			    "{\"type\":\"event\",\"seq\":%zu,\"phase\":\"%s\",\"mono_ns\":%lld,"
			    "\"utc_ns\":%lld,\"deadline_ns\":%lld,\"job_ptr\":%llu,\"lane_ptr\":%llu,"
			    "\"conn_ptr\":%llu,\"scenario\":%llu,\"command\":%llu,\"data\":%llu,"
			    "\"job\":%d,\"connection\":%d,\"transaction\":%d,\"lane\":%d,"
			    "\"backend\":%d,\"socket\":%d,\"tx\":%d,\"rc\":%d,\"tid\":%d,\"cpu\":%d}\n",
			    i, r.phase, static_cast<long long>(r.at), static_cast<long long>(r.utc),
			    static_cast<long long>(r.deadline), static_cast<unsigned long long>(r.job_ptr),
			    static_cast<unsigned long long>(r.lane_ptr),
			    static_cast<unsigned long long>(r.conn_ptr),
			    static_cast<unsigned long long>(r.scenario),
			    static_cast<unsigned long long>(r.command), static_cast<unsigned long long>(r.data),
			    int(r.job), int(r.connection), int(r.transaction), r.lane, r.backend, r.socket,
			    r.tx, r.rc, r.tid, r.cpu);
			ok_ = size > 0 && static_cast<std::size_t>(size) < sizeof(line) &&
			      write_all(line, static_cast<std::size_t>(size));
		}
		char footer[256];
		const int size =
		    std::snprintf(footer, sizeof(footer),
		                  "{\"type\":\"footer\",\"records\":%zu,\"dropped\":%zu,\"write_ok\":%s}\n",
		                  total, total > capacity ? total - capacity : 0, ok_ ? "true" : "false");
		ok_ = ok_ && size > 0 && write_all(footer, static_cast<std::size_t>(size));
		ok_ = (::close(fd_) == 0) && ok_ && total <= capacity;
		return ok_;
	}
	~Capture() {
		finish();
	}
};
} // namespace pg::network_trace
#define PG_NETWORK_TRACE(...) ::pg::network_trace::record(__VA_ARGS__)
#define PG_NETWORK_SCENARIO(name) ::pg::network_trace::mark_scenario(name)
#else
#define PG_NETWORK_TRACE(...) ((void)0)
#define PG_NETWORK_SCENARIO(name) ((void)0)
namespace pg::network_trace {
struct Capture {
	bool valid() const noexcept {
		return true;
	}
	bool finish() const noexcept {
		return true;
	}
};
} // namespace pg::network_trace
#endif
