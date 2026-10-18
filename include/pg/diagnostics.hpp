#pragma once

#include "types.hpp"
#include <array>
#include <mutex>
#include <string_view>

namespace pg {

enum class LogLevel { off, error, warn, info, debug, trace };
enum class LogEvent {
	connection_created,
	connection_closing,
	lane_connecting,
	lane_ready,
	lane_retry,
	lane_failed,
	lane_stopped,
	job_accepted,
	job_running,
	job_terminal,
	completion_polled,
	notification_suppressed,
	callback_begin,
	callback_end,
	callback_missing,
	callback_failed,
	owner_missing,
	admission_rejected,
	job_released,
	owner_unloaded,
	transaction_expired,
	worker_failed,
	stream_rows,
	count
};

// Deliberately no text/pointers: neither SQL, paths, diagnostics nor user data can enter.
struct LogRecord {
	LogEvent event{};
	Owner owner = 0;
	Id connection = 0, job = 0, transaction = 0;
	int lane = -1;
	Kind kind = Kind::execute;
	SqlSourceKind source = SqlSourceKind::inline_sql;
	Status status = Status::queued;
	Error error = Error::none;
	std::int64_t queue_us = -1, execution_us = -1, completion_wait_us = -1;
	std::uint64_t value = 0; // Retry ordinal, count or callback error code; never a Pawn value.
};

LogLevel parse_log_level(std::string_view text);
LogLevel event_level(LogEvent event) noexcept;
LogLevel record_level(const LogRecord &record) noexcept;
LogRecord job_record(LogEvent event, const Job &job) noexcept;

class Diagnostics {
public:
	static constexpr std::size_t capacity = 256, drain_budget = 64;
	using Sink = void (*)(void *, std::string_view);
	explicit Diagnostics(LogLevel level = LogLevel::off) : level_(level) {}
	bool enabled(LogLevel level) const noexcept {
		return level != LogLevel::off && level <= level_;
	}
	void emit(LogRecord record) noexcept;
	// Main-thread output only, outside all engine/lane/logger locks. Recursive drains no-op.
	void drain(Sink sink, void *context) noexcept;
	std::uint64_t dropped() const noexcept {
		return dropped_.load(std::memory_order_relaxed);
	}
	std::uint64_t sink_failures() const noexcept {
		return sink_failures_.load(std::memory_order_relaxed);
	}
	LogLevel level() const noexcept {
		return level_;
	}

private:
	const LogLevel level_;
	std::array<LogRecord, capacity> records_{};
	std::mutex mutex_;
	std::size_t head_ = 0, size_ = 0;
	std::atomic<std::uint64_t> dropped_{0}, pending_drops_{0}, sink_failures_{0};
	std::atomic_flag draining_ = ATOMIC_FLAG_INIT;
	void drop() noexcept;
};

} // namespace pg
