#include "pg/engine.hpp"
#include "pg/parameters.hpp"
#include "pg/stream.hpp"
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
constexpr pg::Owner owner = 71;
int checks = 0;

[[noreturn]] void fail(const std::string &message) {
	// A stalled destructor must not hide the error that initiated unwinding.
	std::cerr << "FAIL: " << message << std::endl;
	throw std::runtime_error(message);
}

void check(bool condition, const char *message) {
	++checks;
	if (!condition) {
		fail(message);
	}
}

template <class F> void rejects(F action) {
	bool rejected = false;
	try {
		action();
	} catch (const pg::Failure &) {
		rejected = true;
	}
	check(rejected, "Expected local rejection");
}

void ready(pg::Engine &engine, pg::Id connection) {
	auto deadline = pg::Clock::now() + std::chrono::seconds(15);
	while (engine.connection(owner, connection).state() != pg::State::connected) {
		engine.poll();
		if (pg::Clock::now() >= deadline) {
			fail("Connection deadline");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

std::shared_ptr<pg::Job> wait(pg::Engine &engine, pg::Id id) {
	auto job = engine.job(owner, id);
	auto deadline = pg::Clock::now() + std::chrono::seconds(15);
	while (!pg::terminal(job->status.load()) || !job->delivered) {
		engine.poll();
		if (pg::Clock::now() >= deadline) {
			fail("Job deadline");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return job;
}

std::shared_ptr<pg::Job> success(pg::Engine &engine, pg::Id id) {
	auto job = wait(engine, id);
	if (job->status.load() != pg::Status::confirmed) {
		fail("Unexpected job failure: " + job->message);
	}
	++checks;
	return job;
}

pg::Id query(pg::Engine &engine, pg::Id connection, std::string sql, pg::Id tx = 0, int lane = 0) {
	return engine.query(owner, connection, std::move(sql), lane, tx, 0, false);
}

void batches(const char *service) {
	pg::Engine engine;
	auto connection = engine.connect(owner, service, 2);
	ready(engine, connection);
	auto quick = success(engine, query(engine, connection, "SELECT 5 AS quick"));
	check(quick->results[0].rows[0][0] == "5", "Simple query uses retained results");
	check(quick->results[0].column_types == std::vector<std::uint32_t>{23}, "Column OID");
	check(quick->items_status == std::vector{pg::ItemStatus::confirmed}, "Simple item status");
	engine.close_job(owner, quick->id);
	check(engine.connection(owner, connection).lanes[0]->snapshot().confirmed_jobs >= 1,
	      "Cumulative session statistics");

	auto batch = engine.create_batch(owner, connection);
	auto integer_statement = engine.prepare(owner, connection, "SELECT $1 AS number", 1);
	auto string_statement = engine.prepare(owner, connection, "SELECT $1 AS label, $2 AS flag", 2);
	for (int number : {11, 12}) {
		engine.statement(owner, integer_statement).bind(1, pg::integer(number), engine.limits());
		engine.statement(owner, integer_statement).add(engine.limits());
	}
	engine.add_batch(owner, batch, integer_statement);
	check(engine.statement(owner, integer_statement).batch.empty(), "Transfer consumes snapshots");
	engine.statement(owner, string_statement).bind(1, pg::text("quoted ' value"), engine.limits());
	engine.statement(owner, string_statement).bind(2, pg::boolean(true), engine.limits());
	engine.statement(owner, string_statement).add(engine.limits());
	engine.add_batch(owner, batch, string_statement);
	rejects([&] {
		engine.add_batch(owner + 1, batch, string_statement);
	});
	rejects([&] {
		engine.add_batch(owner, batch, string_statement);
	});
	engine.close_statement(owner, integer_statement);
	engine.close_statement(owner, string_statement);
	auto id = engine.execute_batch(owner, batch, 0, 0, 0, false);
	engine.close_batch(owner, batch);
	auto result = success(engine, id);
	check(result->results.size() == 3 && result->completed == 3, "Flattened result correspondence");
	check(result->results[0].rows[0][0] == "11" && result->results[1].rows[0][0] == "12" &&
	          result->results[2].rows[0][0] == "quoted ' value",
	      "Different SQL and parameter types retain order and snapshots");
	check(result->results[2].column_types == std::vector<std::uint32_t>({25, 16}),
	      "Heterogeneous result types");
	check(result->error_index == -1, "Successful batch has no error item");
	check(result->delivery_us >= result->duration_us && result->queue_us >= 0,
	      "Timing separates queue, execution and dispatch");
	engine.close_job(owner, id);

	batch = engine.create_batch(owner, connection);
	for (const auto *sql : {"SELECT 1", "SELECT 1/0", "SELECT 3"}) {
		auto statement = engine.prepare(owner, connection, sql, 0);
		engine.statement(owner, statement).add(engine.limits());
		engine.add_batch(owner, batch, statement);
		engine.close_statement(owner, statement);
	}
	auto failed = wait(engine, engine.execute_batch(owner, batch, 0, 0, 0, false));
	check(failed->status.load() == pg::Status::failed && failed->completed == 1 &&
	          failed->error_index == 1 && failed->results.size() == 2,
	      "Partial heterogeneous failure identifies the failing item");
	check(failed->items_status == std::vector({pg::ItemStatus::confirmed, pg::ItemStatus::failed,
	                                           pg::ItemStatus::not_executed}),
	      "Items after failure never execute");
	check(failed->results[1].sqlstate == "22012", "Per-item SQLSTATE preserved");
	rejects([&] {
		engine.execute_batch(owner, batch, 0, 0, 0, false);
	});
	engine.close_job(owner, failed->id);
	engine.close_batch(owner, batch);

	auto blocker = query(engine, connection, "SELECT pg_sleep(0.08)");
	auto queued = query(engine, connection, "SELECT 2");
	success(engine, blocker);
	auto delayed = success(engine, queued);
	check(delayed->queue_us > 10000, "Queue wait is measured separately");
	engine.close_job(owner, blocker);
	engine.close_job(owner, queued);

	blocker = query(engine, connection, "SELECT pg_sleep(0.08)");
	queued = query(engine, connection, "SELECT 99");
	engine.job(owner, queued)->cancel.store(true);
	success(engine, blocker);
	auto cancelled = wait(engine, queued);
	check(cancelled->status.load() == pg::Status::cancelled &&
	          cancelled->items_status == std::vector{pg::ItemStatus::cancelled} &&
	          cancelled->error_index == -1 && cancelled->completed == 0,
	      "Queued cancellation is reported per item without SQL dispatch");
	auto counters = engine.connection(owner, connection).lanes[0]->snapshot();
	check(counters.queued_jobs == 0 && counters.running_jobs == 0 && counters.cancelled_jobs == 1 &&
	          counters.completed_jobs == counters.confirmed_jobs + counters.failed_jobs +
	                                         counters.uncertain_jobs + counters.cancelled_jobs,
	      "Quiescent worker statistics account for each terminal job");
	engine.close_job(owner, blocker);
	engine.close_job(owner, queued);
}

void rejection(const char *service) {
	pg::Limits limits;
	limits.jobs = 1;
	limits.batch_count = 2;
	pg::Engine engine(limits);
	auto connection = engine.connect(owner, service, 1);
	ready(engine, connection);
	auto statement = engine.prepare(owner, connection, "SELECT $1", 1);
	auto batch = engine.create_batch(owner, connection);
	for (int number : {1, 2}) {
		engine.statement(owner, statement).bind(1, pg::integer(number), limits);
		engine.statement(owner, statement).add(limits);
	}
	engine.add_batch(owner, batch, statement);
	engine.statement(owner, statement).bind(1, pg::integer(3), limits);
	engine.statement(owner, statement).add(limits);
	rejects([&] {
		engine.add_batch(owner, batch, statement);
	});
	check(engine.statement(owner, statement).batch.size() == 1,
	      "Rejected transfer preserves source");
	auto retained = success(engine, query(engine, connection, "SELECT 0"));
	rejects([&] {
		engine.execute_batch(owner, batch, 0, 0, 0, false);
	});
	engine.close_job(owner, retained->id);
	auto restored = success(engine, engine.execute_batch(owner, batch, 0, 0, 0, false));
	check(restored->completed == 2 && restored->results[1].rows[0][0] == "2",
	      "Rejected enqueue preserves destination snapshots");
	engine.close_job(owner, restored->id);
	auto original = success(engine, engine.execute(owner, statement, 0, 0, 0, false));
	check(original->results[0].rows[0][0] == "3", "Original pg_add semantics preserved");
	engine.close_job(owner, original->id);
	engine.close_batch(owner, batch);
	engine.close_statement(owner, statement);
}

void transactions(const char *service) {
	pg::Engine engine;
	auto connection = engine.connect(owner, service, 2);
	ready(engine, connection);
	auto setup =
	    success(engine, query(engine, connection, "CREATE TEMP TABLE extension_tx(value int)"));
	engine.close_job(owner, setup->id);
	auto tx = success(engine, engine.begin(owner, connection, 0, 0, false));
	auto save = success(
	    engine, engine.savepoint(owner, tx->id, "safe_point", pg::Kind::savepoint, 0, false));
	engine.close_job(owner, save->id);
	auto failed = wait(engine, query(engine, connection, "SELECT 1/0", tx->id));
	check(failed->status.load() == pg::Status::failed, "Statement aborts transaction");
	engine.close_job(owner, failed->id);
	auto restored = success(
	    engine, engine.savepoint(owner, tx->id, "safe_point", pg::Kind::rollback_to, 0, false));
	engine.close_job(owner, restored->id);
	auto batch = engine.create_batch(owner, connection);
	for (const auto *sql :
	     {"INSERT INTO extension_tx VALUES (7)", "SELECT count(*) FROM extension_tx"}) {
		auto statement = engine.prepare(owner, connection, sql, 0);
		engine.statement(owner, statement).add(engine.limits());
		engine.add_batch(owner, batch, statement);
		engine.close_statement(owner, statement);
	}
	auto grouped = success(engine, engine.execute_batch(owner, batch, 1, tx->id, 0, false));
	check(grouped->lane == 0 && grouped->results[1].rows[0][0] == "1",
	      "Transaction batch stays on reserved lane");
	engine.close_job(owner, grouped->id);
	engine.close_batch(owner, batch);
	auto released = success(engine, engine.savepoint(owner, tx->id, "safe_point",
	                                                 pg::Kind::release_savepoint, 0, false));
	engine.close_job(owner, released->id);
	rejects([&] {
		engine.savepoint(owner, tx->id, "bad\"name", pg::Kind::savepoint, 0, false);
	});
	auto committed = success(engine, engine.finish(owner, tx->id, true, 0, false));
	engine.close_job(owner, committed->id);
	engine.close_job(owner, tx->id);

	tx = success(engine,
	             engine.begin(owner, connection, 0, 0, false, pg::Isolation::serializable, true));
	auto options = success(engine, query(engine, connection,
	                                     "SELECT current_setting('transaction_isolation'), "
	                                     "current_setting('transaction_read_only')",
	                                     tx->id));
	check(options->results[0].rows[0][0] == "serializable" &&
	          options->results[0].rows[0][1] == "on",
	      "Explicit isolation and read-only options reach PostgreSQL");
	engine.close_job(owner, options->id);
	auto rollback = success(engine, engine.finish(owner, tx->id, false, 0, false));
	engine.close_job(owner, rollback->id);
	engine.close_job(owner, tx->id);

	auto defaults = success(
	    engine, query(engine, connection,
		              "SELECT set_config('default_transaction_isolation','repeatable read',false), "
		              "set_config('default_transaction_read_only','on',false)"));
	engine.close_job(owner, defaults->id);
	tx = success(engine, engine.begin(owner, connection, 0, 0, false));
	options = success(engine, query(engine, connection,
	                                "SELECT current_setting('transaction_isolation'), "
	                                "current_setting('transaction_read_only')",
	                                tx->id));
	check(options->results[0].rows[0][0] == "repeatable read" &&
	          options->results[0].rows[0][1] == "on",
	      "Original BEGIN inherits the configured PostgreSQL transaction defaults");
	engine.close_job(owner, options->id);
	rollback = success(engine, engine.finish(owner, tx->id, false, 0, false));
	engine.close_job(owner, rollback->id);
	engine.close_job(owner, tx->id);

	tx = success(
	    engine, engine.begin(owner, connection, 0, 0, false, pg::Isolation::read_committed, false));
	options = success(engine, query(engine, connection,
	                                "SELECT current_setting('transaction_isolation'), "
	                                "current_setting('transaction_read_only')",
	                                tx->id));
	check(options->results[0].rows[0][0] == "read committed" &&
	          options->results[0].rows[0][1] == "off",
	      "Explicit BEGIN options override session defaults");
	engine.close_job(owner, options->id);
	rollback = success(engine, engine.finish(owner, tx->id, false, 0, false));
	engine.close_job(owner, rollback->id);
	engine.close_job(owner, tx->id);
}

void streaming(const char *service) {
	pg::Limits limits;
	limits.result_rows = 2;
	limits.result_bytes = 16384;
	pg::Engine engine(limits);
	auto connection = engine.connect(owner, service, 1);
	ready(engine, connection);
	auto statement =
	    engine.prepare(owner, connection, "SELECT i FROM generate_series(1,10000) i", 0);
	engine.statement(owner, statement).add(limits);
	auto id = engine.execute_stream(owner, statement, 64, 0, 0, 0, true);
	auto job = engine.job(owner, id);
	engine.close_statement(owner, statement);
	std::int64_t sum = 0;
	std::size_t rows = 0, events = 0, max_rows = 0;
	auto deadline = pg::Clock::now() + std::chrono::seconds(15);
	for (;;) {
		events += engine.poll().streams.size();
		if (job->stream->next()) {
			auto block = job->stream->current();
			max_rows = std::max(max_rows, block->rows.size());
			check(block->column_types[0] == 23, "Stream metadata retained per block");
			for (const auto &row : block->rows) {
				sum += std::stoll(*row[0]);
				++rows;
			}
			job->stream->release();
		}
		if (pg::terminal(job->status.load()) && !job->stream->available()) {
			break;
		}
		if (pg::Clock::now() >= deadline) {
			fail("Stream drain deadline");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	success(engine, id);
	check(rows == 10000 && sum == 50005000 && max_rows <= 64,
	      "Large stream is complete and chunked");
	check(events > 0 && job->results[0].rows.empty(),
	      "Streaming dispatch does not retain all rows");
	engine.close_job(owner, id);

	statement = engine.prepare(owner, connection, "SELECT FROM generate_series(1,1500)", 0);
	engine.statement(owner, statement).add(limits);
	id = engine.execute_stream(owner, statement, 4, 0, 0, 0, false);
	job = engine.job(owner, id);
	rows = 0;
	deadline = pg::Clock::now() + std::chrono::seconds(15);
	while (!pg::terminal(job->status.load()) || job->stream->available()) {
		engine.poll();
		if (job->stream->next()) {
			auto block = job->stream->current();
			check(block->columns.empty() && block->column_types.empty(),
			      "Zero-column rows have valid empty metadata");
			rows += block->rows.size();
			job->stream->release();
		}
		if (pg::Clock::now() >= deadline) {
			fail("Zero-column stream deadline");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	success(engine, id);
	check(rows == 1500, "Streaming zero-column tuples resets the per-block byte budget");
	engine.close_job(owner, id);
	engine.close_statement(owner, statement);

	statement =
	    engine.prepare(owner, connection, "SELECT 10/(15-i) FROM generate_series(1,30) i", 0);
	engine.statement(owner, statement).add(limits);
	id = engine.execute_stream(owner, statement, 2, 0, 0, 0, false);
	job = engine.job(owner, id);
	rows = 0;
	deadline = pg::Clock::now() + std::chrono::seconds(15);
	while (!pg::terminal(job->status.load()) || job->stream->available()) {
		engine.poll();
		if (job->stream->next()) {
			rows += job->stream->current()->rows.size();
			job->stream->release();
		}
		if (pg::Clock::now() >= deadline) {
			fail("Failed stream deadline");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	wait(engine, id);
	check(rows > 0 && job->status.load() == pg::Status::failed && job->completed == 0,
	      "Stream rows remain provisional when a later row fails");
	engine.close_job(owner, id);
	engine.close_statement(owner, statement);

	limits.query_ms = 100;
	pg::Engine slow(limits);
	auto slow_connection = slow.connect(owner, service, 1);
	ready(slow, slow_connection);
	statement = slow.prepare(owner, slow_connection, "SELECT i FROM generate_series(1,10000) i", 0);
	slow.statement(owner, statement).add(limits);
	id = slow.execute_stream(owner, statement, 1, 0, 0, 0, false);
	job = wait(slow, id); // Deliberately never consume a block.
	check(job->status.load() == pg::Status::uncertain && job->error == pg::Error::timeout,
	      "A stalled consumer cannot retain a worker indefinitely");
	check(job->stream->next() && job->stream->current()->rows.size() == 1 && !job->stream->next(),
	      "Only one unconsumed block is retained");
	slow.close_job(owner, id);
	slow.close_statement(owner, statement);

	ready(slow, slow_connection);
	const auto reconnects = slow.connection(owner, slow_connection).lanes[0]->snapshot().reconnects;
	statement = slow.prepare(owner, slow_connection, "SELECT i FROM generate_series(1,3) i", 0);
	slow.statement(owner, statement).add(limits);
	id = slow.execute_stream(owner, statement, 2, 0, 0, 0, false);
	job = wait(slow, id);
	check(job->status.load() == pg::Status::failed && job->error == pg::Error::timeout &&
	          job->completed == 1 && job->items_status == std::vector{pg::ItemStatus::confirmed} &&
	          job->error_index == 0,
	      "A final delivery timeout preserves definitive SQL confirmation");
	check(job->results.size() == 1 && job->results[0].rows.empty() &&
	          job->results[0].affected == "3" &&
	          job->results[0].column_types == std::vector<std::uint32_t>{23},
	      "Incomplete final delivery retains terminal SQL metadata");
	check(job->message.find("SQL completed") != std::string::npos &&
	          slow.connection(owner, slow_connection).ready() == 1 &&
	          slow.connection(owner, slow_connection).lanes[0]->snapshot().reconnects == reconnects,
	      "Known SQL completion is reported without resetting the session");
	check(job->stream->next() && job->stream->current()->rows.size() == 2 && !job->stream->next(),
	      "The unavailable trailing row does not turn incomplete delivery into success");
	slow.close_job(owner, id);
	slow.close_statement(owner, statement);
	auto reusable = success(slow, query(slow, slow_connection, "SELECT 73"));
	check(reusable->results[0].rows[0][0] == "73",
	      "Session remains usable after a delivery-only failure");
	slow.close_job(owner, reusable->id);
}

void stream_lifecycle(const char *service) {
	pg::Limits limits;
	limits.query_ms = 2000;
	pg::Engine engine(limits);
	auto connection = engine.connect(owner, service, 1);
	ready(engine, connection);
	auto statement =
	    engine.prepare(owner, connection, "SELECT i FROM generate_series(1,10000) i", 0);
	auto stalled_stream = [&] {
		engine.statement(owner, statement).add(limits);
		auto id = engine.execute_stream(owner, statement, 1, 0, 0, 0, true);
		auto job = engine.job(owner, id);
		auto deadline = pg::Clock::now() + std::chrono::seconds(15);
		while (!job->stream->available()) {
			engine.poll();
			if (pg::terminal(job->status.load()) || pg::Clock::now() >= deadline) {
				fail("Stream did not reach consumer backpressure");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return job;
	};

	auto cancelled = stalled_stream();
	cancelled->cancel.store(true);
	wait(engine, cancelled->id);
	check(cancelled->status.load() == pg::Status::uncertain &&
	          cancelled->error == pg::Error::cancelled && cancelled->completed == 0 &&
	          cancelled->items_status == std::vector{pg::ItemStatus::uncertain},
	      "Cancelling a stalled stream preserves an unknown SQL outcome");
	check(cancelled->stream->next() && cancelled->stream->current()->rows.size() == 1 &&
	          !cancelled->stream->next(),
	      "Cancellation retains only the already published block");
	engine.close_job(owner, cancelled->id);
	ready(engine, connection);

	auto unloaded = stalled_stream();
	engine.unload(owner);
	auto deadline = pg::Clock::now() + std::chrono::seconds(15);
	bool callbacks_suppressed = true;
	for (;;) {
		auto dispatch = engine.poll();
		callbacks_suppressed &= dispatch.jobs.empty() && dispatch.streams.empty();
		if (pg::terminal(unloaded->status.load())) {
			break;
		}
		if (pg::Clock::now() >= deadline) {
			fail("Unloaded stream did not release its worker");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	engine.poll();
	check(callbacks_suppressed, "Unloading a stream suppresses its job and block callbacks");
	check(unloaded->status.load() == pg::Status::uncertain &&
	          unloaded->error == pg::Error::cancelled,
	      "Unloading a stalled stream releases the producer without claiming confirmation");
	rejects([&] {
		engine.job(owner, unloaded->id);
	});
}
} // namespace

int main() {
	const auto service = std::getenv("OMP_PG_TEST_SERVICE");
	if (!service) {
		std::cout << "Set OMP_PG_TEST_SERVICE to run extension integration checks\n";
		return 77;
	}
	try {
		std::cout << "extension phase: batches" << std::endl;
		batches(service);
		std::cout << "extension phase: rejection" << std::endl;
		rejection(service);
		std::cout << "extension phase: transactions" << std::endl;
		transactions(service);
		std::cout << "extension phase: streaming" << std::endl;
		streaming(service);
		std::cout << "extension phase: stream lifecycle" << std::endl;
		stream_lifecycle(service);
		std::cout << checks << " extension checks passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
