#pragma once

#include "diagnostics.hpp"
#include <condition_variable>
#include <filesystem>
#include <thread>

namespace pg {

// Internal numeric handoff. No filesystem operation runs under mutex_.
class LogFileWriter {
public:
	static constexpr std::size_t file_limit = 1024 * 1024;
	// Internal fault injection for qualification, never called by numeric producers.
	using WriteGate = bool (*)(void *);
	explicit LogFileWriter(std::filesystem::path base, WriteGate gate = nullptr,
	                       void *context = nullptr);
	~LogFileWriter();
	void configure(std::string_view path);
	bool enqueue(LogRecord record) noexcept;
	std::uint64_t dropped() const noexcept {
		return dropped_.load();
	}
	std::uint64_t failures() const noexcept {
		return failures_.load();
	}
	bool unavailable() const noexcept {
		return unavailable_.load();
	}

private:
	std::filesystem::path base_;
	std::mutex mutex_;
	std::condition_variable wake_;
	std::array<LogRecord, Diagnostics::capacity> records_{};
	std::array<char, 128> path_{};
	std::size_t head_ = 0, size_ = 0;
	std::uint64_t generation_ = 0;
	bool stop_ = false, dead_ = false;
	WriteGate gate_;
	void *gate_context_;
	std::atomic<std::uint64_t> dropped_{0}, failures_{0};
	std::atomic<bool> unavailable_{false};
	std::thread thread_;
	void run() noexcept;
	void discard_queued() noexcept; // mutex_ held; summaries aren't lost producer events.
};

// No filesystem accesses. Empty is console or the default file, depending on destination.
std::string log_path(LogDestination destination, std::string_view path);
int format_log_record(char (&text)[512], const LogRecord &record) noexcept;

} // namespace pg
