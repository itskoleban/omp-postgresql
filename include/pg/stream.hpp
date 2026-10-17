#pragma once

#include "types.hpp"
#include <condition_variable>
#include <mutex>

namespace pg {

// One pending block, one main-thread block, and one worker block bound retention.
// libpq may still allocate a complete incoming row before our byte limit applies.
class Stream {
public:
	explicit Stream(std::size_t rows) : chunk_rows(rows) {}

	const std::size_t chunk_rows;

	bool publish(std::shared_ptr<const Result> block, const std::atomic<bool> &cancel,
	             const std::atomic<bool> &stop, Clock::time_point deadline) {
		std::unique_lock lock(mutex_);
		while (pending_) {
			if (cancel.load() || stop.load() || Clock::now() >= deadline) {
				return false;
			}
			space_.wait_for(lock, std::chrono::milliseconds(10));
		}
		if (cancel.load() || stop.load() || Clock::now() >= deadline) {
			return false;
		}
		pending_ = std::move(block);
		available_.store(true);
		return true;
	}

	bool available() const {
		return available_.load();
	}

	// Main-thread operations never wait for the producer's mutex. A false return
	// preserves the current block and can be retried on the next tick.
	bool next() {
		std::unique_lock lock(mutex_, std::try_to_lock);
		if (!lock.owns_lock() || !pending_) {
			return false;
		}
		current_ = std::move(pending_);
		available_.store(false);
		space_.notify_one();
		return true;
	}

	void release() {
		current_.reset();
	}

	const std::shared_ptr<const Result> &current() const {
		return current_;
	}

private:
	std::mutex mutex_;
	std::condition_variable space_;
	std::shared_ptr<const Result> pending_;
	std::shared_ptr<const Result> current_; // Accessed only by the main thread.
	std::atomic<bool> available_{false};
};

} // namespace pg
