#include "pg/diagnostics.hpp"
#include <algorithm>
#include <cstdio>

namespace pg {

namespace {
constexpr const char *levels[] = {"OFF", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
constexpr const char *events[] = {
    "connection_created",  "connection_closing", "lane_connecting",   "lane_ready",
    "lane_retry",          "lane_failed",        "lane_stopped",      "job_accepted",
    "job_running",         "job_terminal",       "completion_polled", "notification_suppressed",
    "callback_begin",      "callback_end",       "callback_missing",  "callback_failed",
    "owner_missing",       "admission_rejected", "job_released",      "owner_unloaded",
    "transaction_expired", "worker_failed",      "stream_rows"};
static_assert(std::size(events) == static_cast<std::size_t>(LogEvent::count));
} // namespace

LogLevel parse_log_level(std::string_view text) {
	for (std::size_t i = 0; i < std::size(levels); ++i) {
		if (text == levels[i]) {
			return static_cast<LogLevel>(i);
		}
	}
	throw Failure(Error::invalid,
	              "Invalid setting: OMP_PG_LOG_LEVEL (OFF/ERROR/WARN/INFO/DEBUG/TRACE)");
}

LogLevel event_level(LogEvent event) noexcept {
	switch (event) {
	case LogEvent::lane_failed:
	case LogEvent::worker_failed:
		return LogLevel::error;
	case LogEvent::lane_retry:
	case LogEvent::callback_missing:
	case LogEvent::callback_failed:
	case LogEvent::owner_missing:
	case LogEvent::admission_rejected:
	case LogEvent::transaction_expired:
		return LogLevel::warn;
	case LogEvent::connection_created:
	case LogEvent::connection_closing:
	case LogEvent::lane_connecting:
	case LogEvent::lane_ready:
	case LogEvent::lane_stopped:
	case LogEvent::owner_unloaded:
		return LogLevel::info;
	case LogEvent::notification_suppressed:
	case LogEvent::job_released:
	case LogEvent::stream_rows:
		return LogLevel::trace;
	default:
		return LogLevel::debug;
	}
}

LogRecord job_record(LogEvent event, const Job &j) noexcept {
	LogRecord r;
	r.event = event;
	r.owner = j.owner;
	r.connection = j.connection;
	r.job = j.id;
	r.transaction = j.transaction;
	r.lane = j.lane;
	r.kind = j.kind;
	r.source = j.source;
	// Worker-owned result/error/timing fields must be copied explicitly by their owner
	// or after acquiring terminal status. This helper only reads immutable metadata.
	return r;
}

LogLevel record_level(const LogRecord &r) noexcept {
	if (r.event == LogEvent::callback_missing && r.value == 0) {
		return LogLevel::debug;
	}
	if (r.event == LogEvent::job_terminal && r.status != Status::confirmed) {
		return r.error == Error::internal || r.status == Status::uncertain ? LogLevel::error
		                                                                   : LogLevel::warn;
	}
	return event_level(r.event);
}

void Diagnostics::drop() noexcept {
	dropped_.fetch_add(1, std::memory_order_relaxed);
	pending_drops_.fetch_add(1, std::memory_order_relaxed);
}

void Diagnostics::emit(LogRecord record) noexcept {
	if (!enabled(record_level(record))) {
		return;
	}
	try {
		std::unique_lock lock(mutex_, std::try_to_lock);
		if (!lock.owns_lock() || size_ == capacity) {
			drop();
			return;
		}
		records_[(head_ + size_) % capacity] = record;
		++size_;
	} catch (...) {
		drop();
	}
}

void Diagnostics::drain(Sink sink, void *context) noexcept {
	if (level_ == LogLevel::off || !sink || draining_.test_and_set()) {
		return;
	}
	// No producer can be held behind a slow/throwing/reentrant output sink.
	std::array<LogRecord, drain_budget - 1> batch;
	std::size_t count = 0;
	try {
		std::unique_lock lock(mutex_, std::try_to_lock);
		if (lock.owns_lock()) {
			count = std::min(size_, batch.size());
			for (std::size_t i = 0; i < count; ++i) {
				batch[i] = records_[(head_ + i) % capacity];
			}
			head_ = (head_ + count) % capacity;
			size_ -= count;
		}
	} catch (...) { /* Logging cannot affect original diagnostics. */
	}
	auto output = [&](const char *text, int length) {
		if (length <= 0) {
			return;
		}
		try {
			sink(context, std::string_view(text, std::min<std::size_t>(length, 511)));
		} catch (...) {
			sink_failures_.fetch_add(1, std::memory_order_relaxed);
		}
	};
	char text[512];
	auto lost = pending_drops_.exchange(0, std::memory_order_relaxed);
	auto failed = sink_failures();
	if (lost || failed != reported_sink_failures_) {
		reported_sink_failures_ = failed;
		// ERROR-only output must not gain a WARN line when its ring overflows.
		const char *severity = level_ == LogLevel::error ? "ERROR" : "WARN";
		output(text, std::snprintf(text, sizeof text,
		                           "[omp-pg][%s] log_dropped count=%llu sink_failures=%llu",
		                           severity, static_cast<unsigned long long>(lost),
		                           static_cast<unsigned long long>(failed)));
	}
	for (std::size_t i = 0; i < count; ++i) {
		const auto &r = batch[i];
		auto event = static_cast<std::size_t>(r.event);
		const char *name = event < std::size(events) ? events[event] : "unknown";
		LogLevel severity = record_level(r);
		output(text, std::snprintf(text, sizeof text,
		                           "[omp-pg][%s] %s owner=%llu conn=%d lane=%d job=%d tx=%d "
		                           "kind=%d source=%d status=%d error=%d queue_us=%lld "
		                           "execution_us=%lld completion_wait_us=%lld value=%llu",
		                           levels[static_cast<int>(severity)], name,
		                           static_cast<unsigned long long>(r.owner), r.connection, r.lane,
		                           r.job, r.transaction, static_cast<int>(r.kind),
		                           static_cast<int>(r.source), static_cast<int>(r.status),
		                           static_cast<int>(r.error), static_cast<long long>(r.queue_us),
		                           static_cast<long long>(r.execution_us),
		                           static_cast<long long>(r.completion_wait_us),
		                           static_cast<unsigned long long>(r.value)));
	}
	draining_.clear();
}

} // namespace pg
