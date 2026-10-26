// Controlled lifecycle snapshots exercise the actual Engine::poll cleanup.
// These states model permitted publication/reaping orders, not a worker schedule.
#include "pg/engine.hpp"
#include <iostream>

namespace pg {
struct EngineTestAccess {
	static void retain(Engine &engine, const std::shared_ptr<Job> &job) {
		engine.jobs_.emplace(job->id, job);
		engine.ending_.emplace(job->transaction, true);
	}
	static void mark(Engine &engine, Id transaction) {
		engine.ending_.emplace(transaction, true);
	}
	static void publish(Engine &engine, const std::shared_ptr<Job> &job) {
		engine.completed_.push(job);
	}
	static bool marked(const Engine &engine, Id transaction) {
		return engine.ending_.contains(transaction);
	}
};
} // namespace pg

namespace {
constexpr pg::Owner owner = 37, other_owner = 38;
constexpr pg::Id finish_id = 2, transaction = 1, live_id = 4, live_transaction = 3;

void check(bool condition, const char *message) {
	if (!condition) throw std::runtime_error(message);
}

std::shared_ptr<pg::Job> finish(pg::Kind kind) {
	auto job = std::make_shared<pg::Job>();
	job->id = finish_id;
	job->transaction = transaction;
	job->connection = 99; // Its closing connection has already been reaped.
	job->owner = owner;
	job->kind = kind;
	job->items_status = {pg::ItemStatus::cancelled};
	job->error = pg::Error::cancelled;
	job->accepted_at = job->completed_at = pg::Clock::now();
	job->status.store(pg::Status::cancelled);
	return job;
}

void check_retired(pg::Engine &engine, const std::shared_ptr<pg::Job> &job,
                   const std::shared_ptr<pg::Job> &live) {
	check(!pg::EngineTestAccess::marked(engine, transaction), "Retired finish marker leaked");
	check(pg::EngineTestAccess::marked(engine, live_transaction) &&
	          engine.job(other_owner, live_id) == live,
	      "Retirement removed another live transaction");
	check(job->status.load() == pg::Status::cancelled && job->error == pg::Error::cancelled &&
	          job->completed == 0 && job->results.empty() &&
	          job->items_status == std::vector{pg::ItemStatus::cancelled},
	      "Retirement changed terminal SQL metadata");
}

void scenario(pg::Kind kind, int state) {
	pg::Engine engine;
	auto live = std::make_shared<pg::Job>();
	live->id = live_id;
	live->owner = other_owner;
	live->transaction = live_transaction;
	live->kind = pg::Kind::commit;
	live->status.store(pg::Status::running);
	pg::EngineTestAccess::retain(engine, live);
	auto job = finish(kind);
	if (state == 1) {
		// The unloaded terminal job was removed before its queue entry was consumed.
		job->owner_unloaded = true;
		job->notify = false;
		pg::EngineTestAccess::mark(engine, transaction);
	} else {
		job->owner_unloaded = state == 2;
		job->notify = !job->owner_unloaded;
		pg::EngineTestAccess::retain(engine, job);
	}
	if (state != 2) pg::EngineTestAccess::publish(engine, job);
	const auto dispatch = engine.poll();
	check_retired(engine, job, live);
	check(dispatch.connections.empty() && dispatch.streams.empty(), "Unexpected lifecycle event");
	if (state == 0) {
		check(job->delivered && dispatch.jobs == std::vector{job},
		      "Retained finish completion was not delivered exactly once");
		engine.close_job(owner, finish_id);
	} else {
		check(dispatch.jobs.empty() && !job->delivered, "Unloaded finish notified its owner");
		bool absent = false;
		try { engine.job(owner, finish_id); }
		catch (const pg::Failure &error) { absent = error.code == pg::Error::invalid; }
		check(absent, "Unloaded terminal job remained registered");
	}
	if (state == 2) {
		// A worker may publish after the main thread observes its terminal status.
		pg::EngineTestAccess::publish(engine, job);
	}
	const auto repeated = engine.poll();
	check_retired(engine, job, live);
	check(repeated.jobs.empty() && repeated.connections.empty() && repeated.streams.empty(),
	      "Late publication or repeated polling duplicated a completion");
}
} // namespace

int main() {
	unsigned failures = 0;
	for (const auto kind : {pg::Kind::commit, pg::Kind::rollback}) {
		for (int state = 0; state != 3; ++state) {
			try { scenario(kind, state); }
			catch (const std::exception &error) {
				++failures;
				std::cerr << "transaction_retirement FAIL kind=" << int(kind)
				          << " state=" << state << " " << error.what() << '\n';
			}
		}
	}
	if (failures) return 1;
	std::cout << "transaction_retirement PASS scenarios=6 unrelated_markers=preserved\n";
}
