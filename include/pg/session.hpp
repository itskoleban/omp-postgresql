#pragma once

#include "types.hpp"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <libpq-fe.h>
#include <mutex>
#include <thread>

namespace pg {
struct CompletionQueue {
	std::mutex mutex;
	std::deque<Id> ids;

	void push(Id id) {
		std::lock_guard lock(mutex);
		ids.push_back(id);
	}

	std::deque<Id> drain() {
		std::lock_guard lock(mutex);
		std::deque<Id> out;
		out.swap(ids);
		return out;
	}
};

// Independent atomic samples are approximate while work is running.
struct LaneStats {
	std::uint64_t queued_jobs = 0, running_jobs = 0, completed_jobs = 0;
	std::uint64_t confirmed_jobs = 0, failed_jobs = 0, uncertain_jobs = 0, cancelled_jobs = 0;
	std::uint64_t connection_attempts = 0, reconnects = 0, sql_commands = 0;
	std::uint64_t rows_received = 0, duration_us = 0;
};

class Lane {
public:
	Lane(std::string service, Limits limits, std::filesystem::path sql_root,
	     CompletionQueue &completed);
	~Lane();
	Lane(const Lane &) = delete;
	Lane &operator=(const Lane &) = delete;
	void enqueue(std::shared_ptr<Job> job);
	void close();
	std::string diagnostic();
	LaneStats snapshot() const;
	std::atomic<State> state{State::connecting};
	std::atomic<bool> stopped{false};

private:
	struct ConnDelete {
		void operator()(PGconn *p) const {
			if (p) {
				PQfinish(p);
			}
		}
	};

	using Conn = std::unique_ptr<PGconn, ConnDelete>;
	std::string service_;
	Limits limits_;
	std::filesystem::path sql_root_;
	CompletionQueue &completed_;
	Conn conn_;
	std::string prepared_sql_;
	std::vector<Oid> prepared_types_;
	std::atomic<bool> stop_{false};
	std::mutex mutex_;
	std::string diagnostic_;
	std::condition_variable cv_;
	std::deque<std::shared_ptr<Job>> queue_;
	Id transaction_ = 0;
	bool transaction_failed_ = false;
	bool query_dispatched_ = false;
	bool sql_confirmed_ = false;
	bool connected_once_ = false;
	Clock::time_point transaction_deadline_;
	std::atomic<std::uint64_t> queued_jobs_{0}, running_jobs_{0}, completed_jobs_{0};
	std::atomic<std::uint64_t> confirmed_jobs_{0}, failed_jobs_{0}, uncertain_jobs_{0};
	std::atomic<std::uint64_t> cancelled_jobs_{0}, connection_attempts_{0}, reconnects_{0};
	std::atomic<std::uint64_t> sql_commands_{0}, rows_received_{0}, duration_us_{0};
	std::thread thread_;
	void record_completion(const Job &job, Status status);
	void run();
	bool connect();
	bool wait_socket(bool writing, Clock::time_point deadline, const Job *job);
	Result receive(Job &job, Clock::time_point deadline, std::size_t &bytes,
	               bool stream_rows = false);
	Result command(Job &job, const std::string &sql, const Params &params, bool prepared,
	               std::size_t &bytes);
	void execute(Job &job);
	void drop();
};
} // namespace pg
