// Real Engine bookkeeping with a deterministic, non-network Lane admission model.
// Actual Lane acceptance/stop races remain covered by lane_admission_unit.
#include "pg/engine.hpp"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

#ifndef PG_TEST_ALLOCATIONS
#define PG_TEST_ALLOCATIONS 1
#endif

namespace identity_test {
int allocation_countdown = -1;
int reject_enqueue = 0;
unsigned accepted = 0;
} // namespace identity_test

#if PG_TEST_ALLOCATIONS
void *operator new(std::size_t bytes) {
	if (identity_test::allocation_countdown >= 0 && identity_test::allocation_countdown-- == 0) {
		identity_test::allocation_countdown = -1;
		throw std::bad_alloc();
	}
	if (auto result = std::malloc(bytes ? bytes : 1)) {
		return result;
	}
	throw std::bad_alloc();
}
void *operator new[](std::size_t bytes) {
	return ::operator new(bytes);
}
void operator delete(void *value) noexcept {
	std::free(value);
}
void operator delete[](void *value) noexcept {
	std::free(value);
}
void operator delete(void *value, std::size_t) noexcept {
	std::free(value);
}
void operator delete[](void *value, std::size_t) noexcept {
	std::free(value);
}
#endif

namespace pg {
// No worker is started, no libpq connection is attempted, and no SQL is sent.
Lane::Lane(std::string service, Limits limits, std::filesystem::path root,
           CompletionQueue &completed, Diagnostics *diagnostics, Owner owner, Id connection,
           int lane)
    : service_(std::move(service)), limits_(limits), sql_root_(std::move(root)),
      completed_(completed), diagnostics_(diagnostics), owner_(owner), connection_(connection),
      lane_(lane) {
	state.store(State::connected);
}
Lane::~Lane() {
	close();
}
void Lane::close() {
	state.store(State::disconnected);
	stopped.store(true);
}
void Lane::enqueue(std::shared_ptr<Job>) {
	if (identity_test::reject_enqueue == 1) {
		throw Failure(Error::not_ready, "Deterministic admission rejection");
	}
	if (identity_test::reject_enqueue == 2) {
		throw std::bad_alloc();
	}
	++identity_test::accepted;
}

struct EngineTestAccess {
	static void seed(Engine &engine, Id next) {
		engine.next_ = next;
	}
	static Id next(const Engine &engine) {
		return engine.next_;
	}
	static std::size_t credits(const Engine &engine) {
		return engine.finishing_slots();
	}
	static std::size_t jobs(const Engine &engine) {
		return engine.jobs_.size();
	}
	static bool ending(const Engine &engine) {
		return !engine.ending_.empty();
	}
	static void publish(Engine &engine, const std::shared_ptr<Job> &job, Status status) {
		const auto item = status == Status::confirmed   ? ItemStatus::confirmed
		                  : status == Status::failed    ? ItemStatus::failed
		                  : status == Status::uncertain ? ItemStatus::uncertain
		                                                : ItemStatus::cancelled;
		job->items_status = {item};
		job->error = status == Status::confirmed   ? Error::none
		             : status == Status::failed    ? Error::sql
		             : status == Status::uncertain ? Error::connection
		                                           : Error::cancelled;
		job->completed = status == Status::confirmed ? 1 : 0;
		job->results.emplace_back();
		job->results.back().sqlstate = status == Status::failed ? "23514" : "";
		job->completed_at = Clock::now();
		job->status.store(status);
		engine.completed_.push(job);
	}
};
} // namespace pg

namespace {
constexpr pg::Owner owner = 510, other_owner = 511;
constexpr auto maximum = std::numeric_limits<pg::Id>::max();
unsigned scenarios = 0;

void check(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}
template <class Action> void rejects(Action action, pg::Error expected = pg::Error::limit) {
	bool rejected = false;
	try {
		action();
	} catch (const pg::Failure &failure) {
		rejected = failure.code == expected;
	}
	check(rejected, "Expected deterministic local rejection");
}

void complete(pg::Engine &engine, pg::Id id, pg::Status status = pg::Status::confirmed) {
	auto job = engine.job(owner, id);
	pg::EngineTestAccess::publish(engine, job, status);
	const auto error = job->error;
	const auto items = job->items_status;
	const auto completed = job->completed;
	const auto sqlstate = job->results.front().sqlstate;
	const auto dispatched = engine.poll();
	check(job->delivered && dispatched.jobs == std::vector{job} && engine.poll().jobs.empty(),
	      "Completion lost or duplicated");
	check(job->status.load() == status && job->error == error && job->items_status == items &&
	          job->completed == completed && job->results.front().sqlstate == sqlstate,
	      "Identity cleanup changed terminal SQL metadata");
}

void one_remaining() {
	pg::Engine engine;
	const auto connection = engine.connect(owner, "identity_model", 1);
	pg::EngineTestAccess::seed(engine, maximum - 1);
	const auto accepted = identity_test::accepted;
	rejects([&] {
		engine.begin(owner, connection, 0, 0, true);
	});
	check(pg::EngineTestAccess::next(engine) == maximum - 1 &&
	          pg::EngineTestAccess::credits(engine) == 0 &&
	          pg::EngineTestAccess::jobs(engine) == 0 && identity_test::accepted == accepted,
	      "Unfinishable BEGIN consumed an identity or reserved a lane");
	const auto batch = engine.create_batch(owner, connection);
	check(batch == maximum - 1, "Last unreserved identity was unnecessarily withheld");
	engine.close_batch(owner, batch);
	rejects([&] {
		engine.create_batch(owner, connection);
	});
	++scenarios;
}

void boundary_finish(bool commit, pg::Status status) {
	pg::Engine engine;
	const auto connection = engine.connect(owner, "identity_model", 2);
	const auto statement = engine.prepare(owner, connection, "SELECT 1", 0);
	engine.statement(owner, statement).add(engine.limits());
	pg::EngineTestAccess::seed(engine, maximum - 2);
	const auto begin = engine.begin(owner, connection, 0, 0, true);
	check(begin == maximum - 2 && pg::EngineTestAccess::next(engine) == maximum - 1 &&
	          pg::EngineTestAccess::credits(engine) == 1,
	      "BEGIN did not protect exactly one finishing identity");
	complete(engine, begin);
	rejects([&] {
		engine.connect(owner, "identity_model", 1);
	});
	rejects([&] {
		engine.prepare(owner, connection, "SELECT 1", 0);
	});
	rejects([&] {
		engine.create_batch(owner, connection);
	});
	rejects([&] {
		engine.query(owner, connection, "SELECT 1", 1, 0, 0, true);
	});
	rejects([&] {
		engine.execute(owner, statement, 1, 0, 0, true);
	});
	rejects([&] {
		engine.savepoint(owner, begin, "s", pg::Kind::savepoint, 0, true);
	});
	check(engine.statement(owner, statement).batch.size() == 1 &&
	          pg::EngineTestAccess::next(engine) == maximum - 1 &&
	          pg::EngineTestAccess::credits(engine) == 1,
	      "Rejected ordinary allocation consumed the finish credit or batch");
	const auto finish = engine.finish(owner, begin, commit, 0, true);
	check(finish == maximum - 1 && pg::EngineTestAccess::next(engine) == maximum &&
	          pg::EngineTestAccess::credits(engine) == 0 && pg::EngineTestAccess::ending(engine),
	      "Finish did not consume exactly its reserved identity");
	rejects(
	    [&] {
		    engine.finish(owner, begin, !commit, 0, true);
	    },
	    pg::Error::transaction);
	complete(engine, finish, status);
	check(!pg::EngineTestAccess::ending(engine) &&
	          engine.connection(owner, connection).reservations[0] == 0,
	      "Finish terminal cleanup leaked its reservation");
	engine.close_job(owner, finish);
	engine.close_job(owner, begin);
	rejects([&] {
		engine.create_batch(owner, connection);
	});
	check(pg::EngineTestAccess::next(engine) == maximum, "Exhausted allocator wrapped");
	++scenarios;
}

void independent_credits() {
	pg::Engine engine;
	const auto first_connection = engine.connect(owner, "identity_model", 1);
	const auto second_connection = engine.connect(other_owner, "identity_model", 1);
	pg::EngineTestAccess::seed(engine, maximum - 4);
	const auto first = engine.begin(owner, first_connection, 0, 0, true);
	const auto second = engine.begin(other_owner, second_connection, 0, 0, true);
	complete(engine, first);
	auto second_job = engine.job(other_owner, second);
	pg::EngineTestAccess::publish(engine, second_job, pg::Status::confirmed);
	engine.poll();
	check(pg::EngineTestAccess::credits(engine) == 2, "Independent BEGIN credits lost");
	rejects([&] {
		engine.create_batch(owner, first_connection);
	});
	const auto commit = engine.finish(owner, first, true, 0, true);
	const auto rollback = engine.finish(other_owner, second, false, 0, true);
	check(commit == maximum - 2 && rollback == maximum - 1 && commit != rollback &&
	          pg::EngineTestAccess::next(engine) == maximum,
	      "One transaction consumed another transaction's credit");
	++scenarios;
}

void high_water_probe() {
	pg::Engine engine;
	const auto connection = engine.connect(owner, "identity_model", 1);
	pg::EngineTestAccess::seed(engine, maximum - 3);
	const auto begin = engine.begin(owner, connection, 0, 0, true);
	complete(engine, begin);
	const auto probe = engine.create_batch(owner, connection);
	engine.close_batch(owner, probe);
	check(probe == maximum - 2 && pg::EngineTestAccess::next(engine) == probe + 1 &&
	          pg::EngineTestAccess::credits(engine) == 1,
	      "High-water probe did not observe the exact cursor or consumed a finish credit");
	rejects([&] {
		engine.create_batch(owner, connection);
	});
	check(engine.finish(owner, begin, false, 0, true) == maximum - 1,
	      "A successful high-water probe stole the final reserved identity");
	++scenarios;
}

void rejected_admission(bool begin, int failure) {
	pg::Engine engine;
	const auto connection = engine.connect(owner, "identity_model", 1);
	pg::EngineTestAccess::seed(engine, maximum - 2);
	pg::Id transaction = 0;
	if (!begin) {
		transaction = engine.begin(owner, connection, 0, 0, true);
		complete(engine, transaction);
	}
	const auto before = pg::EngineTestAccess::next(engine);
	const auto accepted = identity_test::accepted;
	identity_test::reject_enqueue = failure;
	bool rejected = false;
	try {
		if (begin) {
			engine.begin(owner, connection, 0, 0, true);
		} else {
			engine.finish(owner, transaction, true, 0, true);
		}
	} catch (const pg::Failure &error) {
		rejected = failure == 1 && error.code == pg::Error::not_ready;
	} catch (const std::bad_alloc &) {
		rejected = failure == 2;
	}
	identity_test::reject_enqueue = 0;
	check(rejected && identity_test::accepted == accepted &&
	          !pg::EngineTestAccess::ending(engine) &&
	          pg::EngineTestAccess::jobs(engine) == (begin ? 0u : 1u),
	      "Rejected admission retained a job or ending marker");
	if (begin) {
		check(pg::EngineTestAccess::next(engine) == before + 1 &&
		          pg::EngineTestAccess::credits(engine) == 0,
		      "Rejected BEGIN reused an allocated identity or leaked a credit");
		check(engine.create_batch(owner, connection) == maximum - 1,
		      "Rejected BEGIN withheld a free, unallocated identity");
	} else {
		check(pg::EngineTestAccess::next(engine) == before &&
		          pg::EngineTestAccess::credits(engine) == 1,
		      "Rejected finish consumed an identity before admission");
		const auto finish = engine.finish(owner, transaction, false, 0, true);
		check(finish == maximum - 1 && pg::EngineTestAccess::next(engine) == maximum,
		      "Retry could not use its still-unallocated finishing identity");
		complete(engine, finish);
	}
	++scenarios;
}

void failed_begin(pg::Status status) {
	pg::Engine engine;
	const auto connection = engine.connect(owner, "identity_model", 1);
	pg::EngineTestAccess::seed(engine, maximum - 2);
	const auto begin = engine.begin(owner, connection, 0, 0, true);
	complete(engine, begin, status);
	check(pg::EngineTestAccess::credits(engine) == 0 &&
	          engine.create_batch(owner, connection) == maximum - 1,
	      "Failed BEGIN leaked its unused identity credit");
	++scenarios;
}

void shutdown(bool unload, bool finishing) {
	pg::Engine engine;
	const auto connection = engine.connect(owner, "identity_model", 1);
	pg::EngineTestAccess::seed(engine, maximum - 2);
	const auto begin = engine.begin(owner, connection, 0, 0, true);
	complete(engine, begin);
	std::shared_ptr<pg::Job> finish;
	if (finishing) {
		finish = engine.job(owner, engine.finish(owner, begin, false, 0, true));
	}
	if (unload) {
		engine.unload(owner);
	} else {
		engine.disconnect(owner, connection);
	}
	if (!finishing) {
		rejects(
		    [&] {
			    engine.finish(owner, begin, true, 0, true);
		    },
		    pg::Error::not_ready);
		check(pg::EngineTestAccess::next(engine) == maximum - 1 &&
		          pg::EngineTestAccess::credits(engine) == 1 &&
		          !pg::EngineTestAccess::ending(engine),
		      "Closing connection consumed an unused finishing identity");
	}
	engine.poll(); // Deliver disconnection, retain its handle through the callback.
	engine.poll(); // Reap the stopped connection and its capacity-only credits.
	check(pg::EngineTestAccess::credits(engine) == 0, "Connection reaping leaked an ID credit");
	if (finish) {
		pg::EngineTestAccess::publish(engine, finish, pg::Status::cancelled);
		const auto dispatch = engine.poll();
		check(!pg::EngineTestAccess::ending(engine) &&
		          finish->status.load() == pg::Status::cancelled,
		      "Late finish terminal leaked a marker or changed its outcome");
		check(unload ? dispatch.jobs.empty() : dispatch.jobs == std::vector{finish},
		      "Late finish notified an unloaded owner or lost a live callback");
		check(engine.poll().jobs.empty(), "Repeated cleanup duplicated a terminal");
	}
	++scenarios;
}

void allocation_sweep(bool begin) {
#if PG_TEST_ALLOCATIONS
	unsigned denied = 0;
	for (int countdown = 0; countdown != 32; ++countdown) {
		pg::Engine engine;
		const auto connection = engine.connect(owner, "identity_model", 1);
		pg::EngineTestAccess::seed(engine, maximum - 2);
		pg::Id transaction = 0;
		if (!begin) {
			transaction = engine.begin(owner, connection, 0, 0, true);
			complete(engine, transaction);
		}
		const auto before = pg::EngineTestAccess::next(engine);
		const auto accepted = identity_test::accepted;
		identity_test::allocation_countdown = countdown;
		bool failed = false;
		try {
			if (begin) {
				engine.begin(owner, connection, 0, 0, true);
			} else {
				engine.finish(owner, transaction, true, 0, true);
			}
		} catch (const std::bad_alloc &) {
			failed = true;
		}
		identity_test::allocation_countdown = -1;
		if (!failed) {
			// Allocator call counts depend on the STL/toolchain. Every preceding
			// countdown must reject, but no fixed number of calls is portable.
			check(identity_test::accepted == accepted + 1 && denied > 0 &&
			          denied == static_cast<unsigned>(countdown),
			      "Allocation sweep missed pre-admission allocations");
			std::cout << "allocation_sweep kind=" << (begin ? "BEGIN" : "FINISH")
			          << " denied_sites=" << denied << " PASS\n";
			++scenarios;
			return;
		}
		++denied;
		check(identity_test::accepted == accepted && !pg::EngineTestAccess::ending(engine) &&
		          pg::EngineTestAccess::jobs(engine) == (begin ? 0u : 1u) &&
		          pg::EngineTestAccess::credits(engine) == (begin ? 0u : 1u),
		      "Allocation failure leaked admission, registry state, or reservation");
		const auto after = pg::EngineTestAccess::next(engine);
		if (begin) {
			check(after == before || after == before + 1,
			      "Failed BEGIN wrapped or reused an identity");
		} else {
			check(after == before, "Failed finish allocation consumed its preview");
			check(engine.finish(owner, transaction, false, 0, true) == maximum - 1,
			      "Allocation rejection lost the last finishing identity");
		}
	}
	throw std::runtime_error("Allocation sweep did not reach successful admission");
#else
	(void)begin;
	std::cout << "allocation_sweep NOT RUN allocator interceptors retained\n";
#endif
}
} // namespace

int main() {
	try {
		one_remaining();
		for (const bool commit : {false, true}) {
			for (const auto status : {pg::Status::confirmed, pg::Status::failed,
			                          pg::Status::uncertain, pg::Status::cancelled}) {
				boundary_finish(commit, status);
			}
		}
		independent_credits();
		high_water_probe();
		for (const bool begin : {false, true}) {
			for (const int failure : {1, 2}) {
				rejected_admission(begin, failure);
			}
			allocation_sweep(begin);
		}
		for (const auto status :
		     {pg::Status::failed, pg::Status::uncertain, pg::Status::cancelled}) {
			failed_begin(status);
		}
		for (const bool unload : {false, true}) {
			for (const bool finishing : {false, true}) {
				shutdown(unload, finishing);
			}
		}
		std::cout << "transaction_identity PASS scenarios=" << scenarios
		          << " SQL=not_run network=not_run Lane=deterministic_admission_model\n";
	} catch (const std::exception &error) {
		identity_test::allocation_countdown = -1;
		identity_test::reject_enqueue = 0;
		std::cerr << "transaction_identity FAIL " << error.what() << '\n';
		return 1;
	}
}
