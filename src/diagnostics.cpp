#include "pg/diagnostics.hpp"
#include "pg/log_file.hpp"
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
    "transaction_expired", "worker_failed",      "stream_rows",       "log_summary"};
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

int format_log_record(char (&text)[512], const LogRecord &r) noexcept {
	if (r.event == LogEvent::log_summary) {
		return std::snprintf(
		    text, sizeof text, "[omp-pg][%s] log_dropped count=%llu sink_failures=%llu",
		    r.error == Error::none ? "WARN" : "ERROR", static_cast<unsigned long long>(r.value),
		    static_cast<unsigned long long>(r.owner));
	}
	auto event = static_cast<std::size_t>(r.event);
	const char *name = event < std::size(events) ? events[event] : "unknown";
	return std::snprintf(
	    text, sizeof text,
	    "[omp-pg][%s] %s owner=%llu conn=%d lane=%d job=%d tx=%d "
	    "kind=%d source=%d status=%d error=%d queue_us=%lld "
	    "execution_us=%lld completion_wait_us=%lld value=%llu",
	    levels[static_cast<int>(record_level(r))], name, static_cast<unsigned long long>(r.owner),
	    r.connection, r.lane, r.job, r.transaction, static_cast<int>(r.kind),
	    static_cast<int>(r.source), static_cast<int>(r.status), static_cast<int>(r.error),
	    static_cast<long long>(r.queue_us), static_cast<long long>(r.execution_us),
	    static_cast<long long>(r.completion_wait_us), static_cast<unsigned long long>(r.value));
}

Diagnostics::Diagnostics(LogLevel level, std::filesystem::path log_base)
    : level_(level), log_base_(std::move(log_base)) {}
Diagnostics::~Diagnostics() = default;

std::uint64_t Diagnostics::dropped() const noexcept {
	return dropped_.load(std::memory_order_relaxed) + (file_ ? file_->dropped() : 0);
}
std::uint64_t Diagnostics::sink_failures() const noexcept {
	return sink_failures_.load(std::memory_order_relaxed) + (file_ ? file_->failures() : 0);
}

void Diagnostics::configure(LogLevel level, LogDestination destination, std::string_view path) {
	if (level < LogLevel::off || level > LogLevel::trace) {
		throw Failure(Error::invalid, "Invalid log level");
	}
	auto name = log_path(destination, path); // All rejection happens before changing state.
	if (destination != LogDestination::console && !file_) {
		file_ = std::make_unique<LogFileWriter>(log_base_);
	}
	std::lock_guard lock(mutex_);
	if (file_) {
		file_->configure(level == LogLevel::off ? "" : name);
	}
	dropped_.fetch_add(size_, std::memory_order_relaxed);
	head_ = size_ = 0;
	destination_ = destination;
	++generation_;
	level_.store(level, std::memory_order_relaxed);
}

void Diagnostics::drop() noexcept {
	dropped_.fetch_add(1, std::memory_order_relaxed);
}

void Diagnostics::emit(LogRecord record) noexcept {
	if (!enabled(record_level(record))) {
		return;
	}
	try {
		std::unique_lock lock(mutex_, std::try_to_lock);
		if (!lock.owns_lock()) {
			drop();
			return;
		}
		// Configuration may have changed after the optimistic filter.
		if (!enabled(record_level(record))) {
			return;
		}
		if (size_ == capacity) {
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
	if (level() == LogLevel::off || !sink || draining_.test_and_set()) {
		return;
	}
	std::array<LogRecord, drain_budget - 1> batch;
	std::size_t count = 0;
	const auto generation = generation_;
	const auto destination = destination_;
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
	} catch (...) {
	}
	auto console = [&](const LogRecord &record) {
		char text[512];
		auto length = format_log_record(text, record);
		if (length <= 0) {
			return;
		}
		try {
			sink(context, std::string_view(text, std::min<std::size_t>(length, 511)));
		} catch (...) {
			sink_failures_.fetch_add(1, std::memory_order_relaxed);
		}
	};
	auto output = [&](const LogRecord &record) {
		// Console callbacks may reenter/configure; no old batch crosses the boundary.
		if (destination != LogDestination::console &&
		    (record.event != LogEvent::log_summary || !file_->unavailable())) {
			file_->enqueue(record);
		}
		if (destination != LogDestination::file) {
			console(record);
		}
	};
	const auto lost = dropped(), failed = sink_failures();
	const bool fallback = destination == LogDestination::file && file_->unavailable();
	const auto now = Clock::now();
	const bool report_fallback =
	    fallback && failed != reported_sink_failures_ &&
	    (last_fallback_ == Clock::time_point{} || now - last_fallback_ >= std::chrono::seconds(60));
	if (report_fallback ||
	    (!fallback && (lost != reported_drops_ || failed != reported_sink_failures_))) {
		LogRecord summary{LogEvent::log_summary};
		summary.value = lost - reported_drops_;
		summary.owner = failed;
		summary.error = level() == LogLevel::error ? Error::internal : Error::none;
		reported_drops_ = lost;
		reported_sink_failures_ = failed;
		if (report_fallback) {
			last_fallback_ = now;
			console(summary);
		} else {
			output(summary);
		}
	}
	for (std::size_t i = 0; i < count; ++i) {
		if (generation != generation_) {
			dropped_.fetch_add(count - i, std::memory_order_relaxed);
			break;
		}
		output(batch[i]);
	}
	draining_.clear();
}

} // namespace pg
