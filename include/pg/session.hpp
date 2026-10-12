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
	Clock::time_point transaction_deadline_;
	std::thread thread_;
	void run();
	bool connect();
	bool wait_socket(bool writing, Clock::time_point deadline, const Job *job);
	Result receive(Job &job, Clock::time_point deadline, std::size_t &bytes);
	Result command(Job &job, const std::string &sql, const Params &params, bool prepared,
	               std::size_t &bytes);
	void execute(Job &job);
	void drop();
};
} // namespace pg
