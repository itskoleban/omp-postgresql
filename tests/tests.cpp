#include "pg/engine.hpp"
#include "pg/parameters.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {

int checks = 0;

void check(bool condition, const char *message) {
	++checks;
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <class F> void rejects(F f) {
	bool rejected = false;
	try {
		f();
	} catch (const pg::Failure &) {
		rejected = true;
	}
	check(rejected, "Expected rejection");
}

constexpr pg::Owner owner = 1;

void ready(pg::Engine &e, pg::Id c) {
	auto deadline = pg::Clock::now() + std::chrono::seconds(20);
	while (e.connection(owner, c).state() != pg::State::connected) {
		e.poll();
		if (pg::Clock::now() > deadline || e.connection(owner, c).state() == pg::State::failed) {
			throw std::runtime_error("Connection did not become ready");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}

std::shared_ptr<pg::Job> wait(pg::Engine &e, pg::Id id) {
	auto j = e.job(owner, id);
	auto deadline = pg::Clock::now() + std::chrono::seconds(40);
	while (!pg::terminal(j->status.load()) || !j->delivered) {
		e.poll();
		if (pg::Clock::now() > deadline) {
			throw std::runtime_error("Job deadline");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	e.poll();
	return j;
}

pg::Id query(pg::Engine &e, pg::Id c, const std::string &sql, pg::Id tx = 0, int lane = 0,
             bool notify = false) {
	auto s = e.prepare(owner, c, sql, 0, false, false);
	e.statement(owner, s).add(e.limits());
	auto j = e.execute(owner, s, lane, tx, 0, notify);
	e.close_statement(owner, s);
	return j;
}

std::shared_ptr<pg::Job> success(pg::Engine &e, pg::Id id) {
	auto j = wait(e, id);
	if (j->status.load() != pg::Status::confirmed) {
		for (auto &r : j->results) {
			std::cerr << r.sqlstate << ": " << r.message << '\n';
		}
		throw std::runtime_error(j->message);
	}
	++checks;
	return j;
}

void unit() {
	check(pg::integer64("9223372036854775807").value == "9223372036854775807", "BIGINT maximum");
	rejects([] {
		pg::integer64("9223372036854775808");
	});
	rejects([] {
		pg::integer64("12x");
	});
	rejects([] {
		pg::text(std::string("a\0b", 3));
	});
	check(pg::utf8("Espa\xc3\xb1ol"), "UTF-8");
	check(!pg::utf8("\xc0\xaf"), "Overlong UTF-8");
	std::vector<std::string> strings{"NULL", "a\"b", "a\\b", "", "x,y"};
	check(pg::string_array(strings).value == "{\"NULL\",\"a\\\"b\",\"a\\\\b\",\"\",\"x,y\"}",
	      "Array escaping");
	pg::Statement s{owner, 1,  "SELECT $1", false, true, std::vector<std::optional<pg::Param>>(1),
	                {},    {}, 0,           {}};
	pg::Limits limits;
	rejects([&] {
		s.add(limits);
	});
	s.bind(1, pg::integer(7), limits);
	s.add(limits);
	s.bind(1, pg::integer(8), limits);
	s.add(limits);
	check(s.batch[0][0].value == "7" && s.batch[1][0].value == "8", "Immutable snapshot");
	rejects([&] {
		s.add(limits);
	});
	s.bind(1, pg::text("x"), limits);
	rejects([&] {
		s.add(limits);
	});
	limits.batch_count = 2;
	s.bind(1, pg::integer(9), limits);
	rejects([&] {
		s.add(limits);
	});
	check(s.batch.size() == 2 && s.bindings[0]->value == "9", "Backpressure preserves staging");
	limits.parameter_bytes = 2;
	rejects([&] {
		s.bind(1, pg::text("long"), limits);
	});
	rejects([] {
		pg::validate_sql("/* a /* b */ c */ COMMIT");
	});
	rejects([] {
		pg::validate_sql("-- comment\nBEGIN");
	});
	rejects([] {
		pg::validate_sql("-- comment\rCOMMIT");
	});
	rejects([] {
		pg::validate_sql(";COMMIT");
	});
	rejects([] {
		pg::validate_sql("/*x*/ ; BEGIN");
	});
	// Reject a new snapshot without consuming either existing data or its bindings.
	pg::Statement bounded{
	    owner, 1,  "SELECT $1", false, true, std::vector<std::optional<pg::Param>>(1),
	    {},    {}, 0,           {}};
	pg::Limits byte_limits;
	byte_limits.batch_bytes = sizeof(pg::Param) + 3;
	bounded.bind(1, pg::text("abc"), byte_limits);
	bounded.add(byte_limits);
	bounded.bind(1, pg::text("x"), byte_limits);
	rejects([&] {
		bounded.add(byte_limits);
	});
	check(bounded.batch.size() == 1 && bounded.batch[0][0].value == "abc" &&
	          bounded.bindings[0]->value == "x" && bounded.bytes == byte_limits.batch_bytes,
	      "Batch byte pressure preserves the captured batch and staging");
	pg::validate_sql("SELECT ';'::text");
	auto root = std::filesystem::temp_directory_path() /
	            ("omp-pg-test-" + std::to_string(pg::Clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(root);
	{
		std::ofstream f(root / "ok.sql");
		f << "SELECT 1";
	}
	check(pg::read_sql(root, "ok.sql", 128) == "SELECT 1", "SQL file");
	rejects([&] {
		pg::read_sql(root, "ok.sql", 2);
	});
	rejects([&] {
		pg::read_sql(root, "missing.sql", 128);
	});
	rejects([&] {
		pg::read_sql(root, "../outside.sql", 128);
	});
	{
		std::ofstream f(root / "invalid.sql", std::ios::binary);
		f << "\xc0\xaf";
	}
	rejects([&] {
		pg::read_sql(root, "invalid.sql", 128);
	});
	std::filesystem::remove(root / "invalid.sql");
	std::filesystem::remove(root / "ok.sql");
	std::filesystem::remove(root);
}

void integration_limits(const char *profile) {
	{
		pg::Limits limits;
		limits.jobs = 1;
		pg::Engine e(limits);
		auto c = e.connect(owner, profile, 1);
		ready(e, c);
		rejects([&] {
			e.begin(owner, c, 0, 0, false);
		});
		check(e.connection(owner, c).reservations[0] == 0,
		      "Insufficient finalization capacity rejects BEGIN without reserving a channel");
		auto available = success(e, query(e, c, "SELECT 1"));
		e.close_job(owner, available->id);
	}
	{
		pg::Limits limits;
		limits.jobs = 3;
		pg::Engine e(limits);
		auto c = e.connect(owner, profile, 2);
		ready(e, c);
		auto t = e.begin(owner, c, 0, 0, false);
		success(e, t);
		rejects([&] {
			e.begin(owner, c, 1, 0, false);
		});
		auto required = success(e, query(e, c, "SELECT 1", t));
		auto s = e.prepare(owner, c, "SELECT $1::integer", 1);
		auto &statement = e.statement(owner, s);
		statement.bind(1, pg::integer(9), limits);
		statement.add(limits);
		rejects([&] {
			e.execute(owner, s, 1, 0, 0, false);
		});
		check(statement.batch.size() == 1 && statement.batch[0][0].value == "9",
		      "Independent work cannot consume the final transaction slot");
		auto finish = success(e, e.finish(owner, t, true, 0, false));
		check(finish->completed == 1,
		      "Transaction can finish while its other results fill capacity");
		e.close_job(owner, required->id);
		e.close_job(owner, finish->id);
		e.close_job(owner, t);
		auto resumed = success(e, e.execute(owner, s, 1, 0, 0, false));
		check(resumed->results[0].rows[0][0] == "9", "Finalization releases its reserved capacity");
		e.close_job(owner, resumed->id);
		e.close_statement(owner, s);
	}
	{
		pg::Limits limits;
		limits.jobs = 2;
		limits.batch_count = 256;
		pg::Engine e(limits);
		auto c = e.connect(owner, profile, 1);
		ready(e, c);
		auto blocked = query(e, c, "SELECT pg_sleep(0.1)");
		auto s = e.prepare(owner, c, "SELECT $1::integer", 1);
		auto &statement = e.statement(owner, s);
		for (int i = 0; i < 256; ++i) {
			statement.bind(1, pg::integer(i), limits);
			statement.add(limits);
		}
		statement.bind(1, pg::integer(256), limits);
		rejects([&] {
			statement.add(limits);
		});
		check(statement.batch.size() == 256 && statement.bindings[0]->value == "256",
		      "Full batch rejects one more item without data loss");
		auto large = e.execute(owner, s, 0, 0, 0, false);
		check(statement.batch.empty() && statement.bytes == 0,
		      "Accepted execution consumes the captured batch");
		statement.add(limits);
		auto staged_bytes = statement.bytes;
		rejects([&] {
			e.execute(owner, s, 0, 0, 0, false);
		});
		check(statement.batch.size() == 1 && statement.batch[0][0].value == "256" &&
		          statement.bytes == staged_bytes,
		      "Job pressure preserves a batch for explicit resubmission");
		success(e, blocked);
		auto large_job = success(e, large);
		check(large_job->completed == 256 && large_job->results.size() == 256 &&
		          large_job->results.front().rows[0][0] == "0" &&
		          large_job->results.back().rows[0][0] == "255",
		      "Maximum-size batch executes each independent statement in order");
		// Completed results remain owned by the caller and still consume handle capacity.
		rejects([&] {
			e.execute(owner, s, 0, 0, 0, false);
		});
		e.close_job(owner, blocked);
		auto retry = success(e, e.execute(owner, s, 0, 0, 0, false));
		check(retry->completed == 1 && retry->results[0].rows[0][0] == "256",
		      "Rejected batch is submitted exactly once after releasing capacity");
		e.close_job(owner, retry->id);
		e.close_job(owner, large);
		e.close_statement(owner, s);
	}
	{
		pg::Limits limits;
		limits.result_rows = 2;
		limits.result_bytes = 4096;
		pg::Engine e(limits);
		auto c = e.connect(owner, profile, 1);
		ready(e, c);
		auto rows = wait(e, query(e, c, "SELECT generate_series(1,3)"));
		check(rows->status.load() == pg::Status::uncertain && rows->error == pg::Error::limit &&
		          rows->completed == 0 && rows->results.empty() &&
		          rows->message.find("row limit") != std::string::npos,
		      "Oversized row result has no false confirmation or partial usable result");
		ready(e, c);
		e.close_job(owner, rows->id);
		auto bytes = wait(e, query(e, c, "SELECT repeat('x',8192)"));
		check(bytes->status.load() == pg::Status::uncertain && bytes->error == pg::Error::limit &&
		          bytes->completed == 0 && bytes->results.empty() &&
		          bytes->message.find("memory limit") != std::string::npos,
		      "Oversized value respects result memory limit");
		ready(e, c);
		e.close_job(owner, bytes->id);
		auto available = success(e, query(e, c, "SELECT 1"));
		e.close_job(owner, available->id);
	}
	{
		pg::Limits limits;
		limits.transaction_ms = 120;
		pg::Engine e(limits);
		auto c = e.connect(owner, profile, 1);
		ready(e, c);
		auto t = e.begin(owner, c, 0, 0, false);
		success(e, t);
		auto session = success(e, query(e, c, "SELECT pg_backend_pid()", t));
		auto previous_backend = session->results[0].rows[0][0];
		e.close_job(owner, session->id);
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		ready(e, c);
		auto finish = wait(e, e.finish(owner, t, true, 0, false));
		check(finish->status.load() == pg::Status::failed &&
		          finish->error == pg::Error::transaction && finish->completed == 0,
		      "Expired transaction cannot commit on its replacement session");
		e.close_job(owner, finish->id);
		e.close_job(owner, t);
		auto replacement = success(e, query(e, c, "SELECT pg_backend_pid()"));
		check(replacement->results[0].rows[0][0] != previous_backend,
		      "Idle transaction deadline closes its physical session and releases reservation");
		e.close_job(owner, replacement->id);
	}
	{
		pg::Engine e;
		auto c = e.connect(owner, profile, 1);
		ready(e, c);
		auto running = query(e, c, "SELECT pg_sleep(30)");
		auto queued = query(e, c, "SELECT 99");
		auto j = e.job(owner, running);
		auto deadline = pg::Clock::now() + std::chrono::seconds(2);
		while (j->status.load() == pg::Status::queued && pg::Clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		check(j->status.load() == pg::Status::running, "Closing test has active work");
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
		auto start = pg::Clock::now();
		e.disconnect(owner, c);
		check(pg::Clock::now() - start < std::chrono::milliseconds(100),
		      "Connection close returns without waiting for network work");
		auto s = e.prepare(owner, c, "SELECT 1", 0);
		e.statement(owner, s).add(e.limits());
		rejects([&] {
			e.execute(owner, s, 0, 0, 0, false);
		});
		check(e.statement(owner, s).batch.size() == 1,
		      "Closing rejects newly submitted work while preserving its batch");
		e.close_statement(owner, s);
		auto queued_observer = e.job(owner, queued);
		bool disconnected = false;
		deadline = pg::Clock::now() + std::chrono::seconds(2);
		while ((!j->delivered || !queued_observer->delivered || !disconnected) &&
		       pg::Clock::now() < deadline) {
			auto dispatched = e.poll();
			for (auto &event : dispatched.connections) {
				if (event.connection == c && event.state == pg::State::disconnected) {
					check(!disconnected, "Final disconnected state is dispatched once");
					disconnected = true;
					check(e.connection(owner, c).state() == pg::State::disconnected,
					      "Connection handle remains valid during its final state callback");
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		check(disconnected && j->delivered && queued_observer->delivered,
		      "Closing dispatches final state and both outstanding completions");
		e.poll();
		rejects([&] {
			e.connection(owner, c);
		});
		check(j->status.load() == pg::Status::uncertain,
		      "Closing dispatched work reports uncertain outcome");
		check(queued_observer->status.load() == pg::Status::cancelled,
		      "Closing cancels queued work before dispatch");
		e.close_job(owner, running);
		e.close_job(owner, queued);
	}
	{
		// Keep observations alive after engine destruction to check shutdown ownership.
		std::shared_ptr<pg::Job> running, queued;
		auto start = pg::Clock::now();
		{
			pg::Engine e;
			auto c = e.connect(owner, profile, 1);
			ready(e, c);
			running = e.job(owner, query(e, c, "SELECT pg_sleep(30)"));
			queued = e.job(owner, query(e, c, "SELECT 99"));
			start = pg::Clock::now();
		}
		check(pg::Clock::now() - start < std::chrono::seconds(2),
		      "Server destruction joins workers promptly with outstanding work");
		check(pg::terminal(running->status.load()) &&
		          queued->status.load() == pg::Status::cancelled,
		      "Server destruction leaves no queued or running orphan job");
	}
	if (auto restart_request = std::getenv("OMP_PG_TEST_RESTART_REQUEST")) {
		pg::Limits limits;
		limits.connect_ms = 1000;
		limits.query_ms = 10000;
		limits.reconnect_attempts = 10;
		limits.reconnect_base_ms = 100;
		pg::Engine e(limits);
		auto c = e.connect(owner, profile, 1);
		ready(e, c);
		auto running = query(e, c, "SELECT pg_sleep(5)");
		auto active = e.job(owner, running);
		auto deadline = pg::Clock::now() + std::chrono::seconds(2);
		while (active->status.load() == pg::Status::queued && pg::Clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		check(active->status.load() == pg::Status::running, "Restart test has active work");
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		auto queued = query(e, c, "SELECT 999");
		{
			std::ofstream request(restart_request);
			check(static_cast<bool>(request), "PostgreSQL restart marker can be created");
		}
		check(wait(e, running)->status.load() == pg::Status::uncertain,
		      "PostgreSQL restart makes an in-flight result uncertain");
		auto not_sent = wait(e, queued);
		check(not_sent->status.load() == pg::Status::failed &&
		          not_sent->error == pg::Error::connection && not_sent->completed == 0 &&
		          not_sent->results.empty(),
		      "PostgreSQL restart fails queued work without replaying it in a new session");
		ready(e, c);
		auto recovered = success(e, query(e, c, "SELECT 42"));
		check(recovered->results[0].rows[0][0] == "42",
		      "PostgreSQL restart recovery accepts explicitly submitted new work");
		for (auto id : {running, queued, recovered->id}) {
			e.close_job(owner, id);
		}
	}
}

void integration(const char *profile) {
	pg::Limits limits;
	limits.query_ms = 2000;
	limits.transaction_ms = 5000;
	pg::Engine e(limits);
	auto c = e.connect(owner, profile, 2);
	ready(e, c);
	check(e.connection(owner, c).ready() == 2, "Pool ready");
	rejects([&] {
		e.connection(2, c);
	});
	// Another session invalidates the prepared result shape. Only an explicit new job may
	// re-prepare it; the job receiving 0A000 must keep its original failure.
	auto plan_table = "omp_pg_plan_" + std::to_string(pg::Clock::now().time_since_epoch().count());
	auto plan_create = success(e, query(e, c, "CREATE TABLE " + plan_table + "(id integer)", 0, 1));
	e.close_job(owner, plan_create->id);
	auto plan_statement = e.prepare(owner, c, "SELECT * FROM " + plan_table, 0);
	e.statement(owner, plan_statement).add(limits);
	auto plan_before = success(e, e.execute(owner, plan_statement, 0, 0, 0, false));
	check(plan_before->results[0].columns.size() == 1, "Prepared plan starts with one column");
	e.close_job(owner, plan_before->id);
	auto plan_alter =
	    success(e, query(e, c, "ALTER TABLE " + plan_table + " ADD COLUMN extra text", 0, 1));
	e.close_job(owner, plan_alter->id);
	e.statement(owner, plan_statement).add(limits);
	auto invalid_plan = wait(e, e.execute(owner, plan_statement, 0, 0, 0, false));
	check(invalid_plan->status.load() == pg::Status::failed && invalid_plan->completed == 0 &&
	          invalid_plan->results.size() == 1 && invalid_plan->results[0].sqlstate == "0A000",
	      "Schema change reports cached-plan failure without automatically retrying");
	e.close_job(owner, invalid_plan->id);
	e.statement(owner, plan_statement).add(limits);
	auto plan_after = success(e, e.execute(owner, plan_statement, 0, 0, 0, false));
	check(plan_after->results[0].columns == std::vector<std::string>{"id", "extra"},
	      "Explicit next execution re-prepares the invalidated plan");
	e.close_job(owner, plan_after->id);
	// A failed unnamed Parse also removes the previous server preparation.
	auto invalid_parse_statement = e.prepare(owner, c, "SELECT FROM", 0);
	e.statement(owner, invalid_parse_statement).add(limits);
	auto invalid_parse = wait(e, e.execute(owner, invalid_parse_statement, 0, 0, 0, false));
	check(invalid_parse->status.load() == pg::Status::failed && invalid_parse->completed == 0 &&
	          invalid_parse->results.size() == 1 && invalid_parse->results[0].sqlstate == "42601",
	      "Invalid prepared SQL reports its original syntax error");
	e.close_job(owner, invalid_parse->id);
	e.close_statement(owner, invalid_parse_statement);
	e.statement(owner, plan_statement).add(limits);
	auto after_parse_failure = success(e, e.execute(owner, plan_statement, 0, 0, 0, false));
	check(after_parse_failure->results[0].columns == std::vector<std::string>{"id", "extra"},
	      "Failed unnamed Parse does not leave a stale client preparation cache");
	e.close_job(owner, after_parse_failure->id);
	e.close_statement(owner, plan_statement);
	auto plan_drop = success(e, query(e, c, "DROP TABLE " + plan_table, 0, 1));
	e.close_job(owner, plan_drop->id);
	auto setup = success(
	    e,
	    query(e, c, "CREATE TEMP TABLE pg_test(id integer PRIMARY KEY, money integer, name text)"));
	e.close_job(owner, setup->id);
	auto s = e.prepare(owner, c, "INSERT INTO pg_test VALUES($1,$2,$3) RETURNING id", 3);
	auto &statement = e.statement(owner, s);
	rejects([&] {
		e.execute(owner, s, 0, 0, 0, false);
	});
	for (int i = 1; i <= 20; ++i) {
		statement.bind(1, pg::integer(i), limits);
		statement.bind(2, pg::integer(i * 10), limits);
		statement.bind(3, pg::text("O'Reilly \\ \xc3\xb1"), limits);
		statement.add(limits);
	}
	auto insert = success(e, e.execute(owner, s, 0, 0, 0, false));
	check(insert->completed == 20 && insert->results.size() == 20, "20 independent executions");
	check(insert->results[19].rows[0][0] == "20", "RETURNING");
	e.close_job(owner, insert->id);
	e.close_statement(owner, s);
	auto bulk = e.prepare(owner, c,
	                      "UPDATE pg_test AS a SET money=v.money FROM unnest($1::int[], $2::int[]) "
	                      "AS v(id,money) WHERE a.id=v.id RETURNING a.id",
	                      2);
	std::vector<std::int32_t> ids{1, 2}, money{111, 222};
	auto &b = e.statement(owner, bulk);
	b.bind(1, pg::int_array(ids), limits);
	b.bind(2, pg::int_array(money), limits);
	b.add(limits);
	auto updated = success(e, e.execute(owner, bulk, 0, 0, 0, false));
	check(updated->completed == 1 && updated->results[0].affected == "2",
	      "UNNEST one execution / two rows");
	e.close_job(owner, updated->id);
	auto t = e.begin(owner, c, 0, 0, false);
	success(e, t);
	rejects([&] {
		e.prepare(owner, c, ";COMMIT", 0);
	});
	rejects([&] {
		e.prepare(owner, c, "/*x*/ ; BEGIN", 0);
	});
	rejects([&] {
		e.prepare(owner, c, "-- comment\rCOMMIT", 0);
	});
	rejects([&] {
		query(e, c, "SELECT 1");
	});
	auto in_tx = success(e, query(e, c, "UPDATE pg_test SET money=999 WHERE id=1", t));
	e.close_job(owner, in_tx->id);
	auto rb = success(e, e.finish(owner, t, false, 0, false));
	e.close_job(owner, rb->id);
	e.close_job(owner, t);
	auto after = success(e, query(e, c, "SELECT money FROM pg_test WHERE id=1"));
	check(after->results[0].rows[0][0] == "111", "Rollback");
	e.close_job(owner, after->id);
	t = e.begin(owner, c, 0, 0, false);
	success(e, t);
	auto bad = wait(e, query(e, c, "SELECT 1/0", t));
	check(bad->status.load() == pg::Status::failed && bad->results[0].sqlstate == "22012",
	      "SQLSTATE");
	auto cm = wait(e, e.finish(owner, t, true, 0, false));
	check(cm->status.load() == pg::Status::failed && cm->error == pg::Error::transaction,
	      "No commit after failed statement");
	e.close_job(owner, bad->id);
	e.close_job(owner, cm->id);
	e.close_job(owner, t);
	auto types = e.prepare(
	    owner, c,
	    "SELECT $1::bigint,$2::text,$3::text[],$4::bool,$5::float4,$6::uuid,$7::jsonb,$8::bytea",
	    8);
	auto &typed = e.statement(owner, types);
	typed.bind(1, pg::integer64("-9223372036854775808"), limits);
	typed.bind(2, pg::null(25), limits);
	std::vector<std::string> values{"NULL", "", "x\"\\,"};
	typed.bind(3, pg::string_array(values), limits);
	typed.bind(4, pg::boolean(true), limits);
	typed.bind(5, pg::floating(1.25f), limits);
	typed.bind(6, pg::text("00112233-4455-6677-8899-aabbccddeeff", 2950), limits);
	typed.bind(7, pg::text("{\"x\":1}", 3802), limits);
	typed.bind(8, pg::text("\\x00ff", 17), limits);
	typed.add(limits);
	auto values_job = success(e, e.execute(owner, types, 1, 0, 0, false));
	check(values_job->results[0].rows[0][0] == "-9223372036854775808" &&
	          !values_job->results[0].rows[0][1],
	      "BIGINT and NULL");
	auto &typed_row = values_job->results[0].rows[0];
	check(typed_row[2] == pg::string_array(values).value && typed_row[3] == "t" &&
	          typed_row[4] == "1.25" && typed_row[5] == "00112233-4455-6677-8899-aabbccddeeff" &&
	          typed_row[6] == "{\"x\": 1}" && typed_row[7] == "\\x00ff",
	      "Escaped array, bool, float, UUID, JSONB and BYTEA round-trip");
	e.close_job(owner, values_job->id);
	auto reusable = e.prepare(owner, c, "SELECT $1::integer, pg_backend_pid()", 1);
	auto &before_reconnect = e.statement(owner, reusable);
	before_reconnect.bind(1, pg::integer(-1), limits);
	before_reconnect.add(limits);
	auto prepared_before = success(e, e.execute(owner, reusable, 1, 0, 0, false));
	auto previous_backend = prepared_before->results[0].rows[0][1];
	check(prepared_before->results[0].rows[0][0] == "-1", "Prepared statement before reconnect");
	e.close_job(owner, prepared_before->id);
	auto sleep = query(e, c, "SELECT pg_sleep(5)", 0, 1);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	e.job(owner, sleep)->cancel.store(true);
	auto cancelled = wait(e, sleep);
	check(cancelled->status.load() == pg::Status::uncertain, "In-flight cancellation uncertain");
	ready(e, c);
	auto alive = success(e, query(e, c, "SELECT 42", 0, 1));
	check(alive->results[0].rows[0][0] == "42", "Reconnect");
	e.close_job(owner, alive->id);
	e.close_job(owner, cancelled->id);
	// Reuse a prepared statement after session replacement, and close it in flight.
	for (int i = 0; i < 3; ++i) {
		auto &reuse = e.statement(owner, reusable);
		reuse.bind(1, pg::integer(i), limits);
		reuse.add(limits);
		auto j = success(e, e.execute(owner, reusable, 1, 0, 0, false));
		check(j->results[0].rows[0][0] == std::to_string(i), "Statement reuse");
		check(j->results[0].rows[0][1] != previous_backend,
		      "Same logical statement re-prepared on the replacement backend");
		e.close_job(owner, j->id);
	}
	auto &reuse = e.statement(owner, reusable);
	reuse.bind(1, pg::integer(55), limits);
	reuse.add(limits);
	auto pending_id = e.execute(owner, reusable, 1, 0, 0, false);
	e.close_statement(owner, reusable);
	auto pending = success(e, pending_id);
	check(pending->results[0].rows[0][0] == "55", "Statement lifetime independent of job");
	e.close_job(owner, pending_id);
	// FIFO over a record and simultaneous independent channels.
	auto a = query(e, c, "UPDATE pg_test SET money=301 WHERE id=1");
	auto other = query(e, c, "SELECT pg_sleep(0.1)", 0, 1);
	auto z = query(e, c, "UPDATE pg_test SET money=302 WHERE id=1");
	success(e, a);
	success(e, z);
	success(e, other);
	auto latest = success(e, query(e, c, "SELECT money FROM pg_test WHERE id=1"));
	check(latest->results[0].rows[0][0] == "302", "FIFO record updates");
	for (auto id : {a, z, other, latest->id}) {
		e.close_job(owner, id);
	}
	// Partial batch failure stops before item 3, retaining item 1's result.
	auto partial = e.prepare(owner, c, "SELECT 100/$1::integer", 1);
	for (auto divisor : {2, 0, 5}) {
		auto &p = e.statement(owner, partial);
		p.bind(1, pg::integer(divisor), limits);
		p.add(limits);
	}
	auto partial_job = wait(e, e.execute(owner, partial, 0, 0, 0, false));
	check(partial_job->completed == 1 && partial_job->results.size() == 2 &&
	          partial_job->status.load() == pg::Status::failed,
	      "Partial failure");
	e.close_job(owner, partial_job->id);
	t = e.begin(owner, c, 0, 0, false);
	success(e, t);
	auto change = success(e, query(e, c, "UPDATE pg_test SET money=400 WHERE id=1", t));
	e.close_job(owner, change->id);
	auto commit = success(e, e.finish(owner, t, true, 0, false));
	e.close_job(owner, commit->id);
	e.close_job(owner, t);
	auto committed = success(e, query(e, c, "SELECT money FROM pg_test WHERE id=1"));
	check(committed->results[0].rows[0][0] == "400", "Commit persisted");
	e.close_job(owner, committed->id);
	auto empty = success(e, query(e, c, "SELECT id FROM pg_test WHERE false"));
	check(empty->results[0].rows.empty() && empty->results[0].columns.size() == 1,
	      "Zero rows still has columns");
	e.close_job(owner, empty->id);
	auto invalid_file = e.prepare(owner, c, "missing.sql", 0, true);
	e.statement(owner, invalid_file).add(limits);
	auto file_job = wait(e, e.execute(owner, invalid_file, 0, 0, 0, false));
	check(file_job->error == pg::Error::file, "Deferred file diagnostic");
	e.close_job(owner, file_job->id);
	auto stacked = wait(e, query(e, c, "SELECT 1; SELECT 2"));
	if (stacked->status.load() != pg::Status::failed) {
		std::cerr << "stacked status=" << static_cast<int>(stacked->status.load())
		          << " error=" << static_cast<int>(stacked->error)
		          << " message=" << stacked->message << '\n';
	}
	check(stacked->status.load() == pg::Status::failed,
	      "Multiple statements rejected by PostgreSQL");
	e.close_job(owner, stacked->id);
	auto blocked = query(e, c, "SELECT pg_sleep(0.1)", 0, 1);
	auto queued = query(e, c, "SELECT 99", 0, 1);
	e.job(owner, queued)->cancel.store(true);
	success(e, blocked);
	auto skipped = wait(e, queued);
	check(skipped->status.load() == pg::Status::cancelled, "Queued cancellation not sent");
	e.close_job(owner, blocked);
	e.close_job(owner, queued);
	t = e.begin(owner, c, 0, 0, false);
	success(e, t);
	auto slow_tx = query(e, c, "SELECT pg_sleep(0.15)", t);
	auto cancelled_tx = query(e, c, "UPDATE pg_test SET money=999 WHERE id=1", t);
	e.job(owner, cancelled_tx)->cancel.store(true);
	auto commit_cancelled = e.finish(owner, t, true, 0, false);
	success(e, slow_tx);
	check(wait(e, cancelled_tx)->status.load() == pg::Status::cancelled,
	      "Transaction item cancelled before send");
	check(wait(e, commit_cancelled)->error == pg::Error::transaction,
	      "Cancelled required item blocks commit");
	for (auto id : {slow_tx, cancelled_tx, commit_cancelled, t}) {
		e.close_job(owner, id);
	}
	auto untouched = success(e, query(e, c, "SELECT money FROM pg_test WHERE id=1"));
	check(untouched->results[0].rows[0][0] == "400", "Cancelled transaction rolled back");
	e.close_job(owner, untouched->id);
	// Server terminates its own backend: protocol must never report a confirmation.
	auto terminate = wait(e, query(e, c, "SELECT pg_terminate_backend(pg_backend_pid())", 0, 1));
	check(terminate->status.load() == pg::Status::uncertain, "Backend termination uncertain");
	ready(e, c);
	e.close_job(owner, terminate->id);
	auto timeout = wait(e, query(e, c, "SELECT pg_sleep(10)", 0, 1));
	check(timeout->status.load() == pg::Status::uncertain, "Query timeout uncertain");
	ready(e, c);
	e.close_job(owner, timeout->id);
	if (auto lossy_profile = std::getenv("OMP_PG_TEST_COMMIT_LOSS")) {
		auto table =
		    "omp_commit_loss_" + std::to_string(pg::Clock::now().time_since_epoch().count());
		auto create = success(e, query(e, c, "CREATE TABLE " + table + "(value integer)"));
		e.close_job(owner, create->id);
		auto loss_c = e.connect(owner, lossy_profile, 1);
		ready(e, loss_c);
		auto loss_t = e.begin(owner, loss_c, 0, 0, false);
		success(e, loss_t);
		auto insert_loss =
		    success(e, query(e, loss_c, "INSERT INTO " + table + " VALUES(1)", loss_t));
		e.close_job(owner, insert_loss->id);
		auto loss_commit = wait(e, e.finish(owner, loss_t, true, 0, false));
		check(loss_commit->status.load() == pg::Status::uncertain,
		      "Lost COMMIT response must be uncertain");
		auto count = success(e, query(e, c, "SELECT count(*) FROM " + table));
		check(count->results[0].rows[0][0] == "1",
		      "Server committed exactly once despite missing response");
		e.close_job(owner, count->id);
		e.close_job(owner, loss_commit->id);
		e.close_job(owner, loss_t);
		e.disconnect(owner, loss_c);
		auto drop = success(e, query(e, c, "DROP TABLE " + table));
		e.close_job(owner, drop->id);
	}
	// Outstanding work at owner unload must not call back into a dead VM.
	auto unload_active = query(e, c, "SELECT pg_sleep(10)", 0, 1, true);
	auto unload_queued = query(e, c, "SELECT 99", 0, 1, true);
	auto active_observer = e.job(owner, unload_active);
	auto queued_observer = e.job(owner, unload_queued);
	e.unload(owner);
	bool callback_after_unload = false;
	for (int i = 0; i < 100; ++i) {
		callback_after_unload = !e.poll().jobs.empty() || callback_after_unload;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	check(!callback_after_unload, "Owner unload suppresses outstanding callbacks");
	check(pg::terminal(active_observer->status.load()) &&
	          queued_observer->status.load() == pg::Status::cancelled,
	      "Owner unload settles both active and queued work");
	rejects([&] {
		e.job(owner, unload_active);
	});
	rejects([&] {
		e.job(owner, unload_queued);
	});
	if (auto cert_profile = std::getenv("OMP_PG_TEST_CERT")) {
		pg::Engine cert_engine;
		auto cert_c = cert_engine.connect(owner, cert_profile, 1);
		ready(cert_engine, cert_c);
		auto cert_job = success(cert_engine, query(cert_engine, cert_c, "SELECT current_user"));
		check(cert_job->results[0].rows[0][0] == "omp_cert",
		      "TLS client certificate authenticates its configured PostgreSQL role");
		cert_engine.close_job(owner, cert_job->id);
	}
	for (auto setting : {"OMP_PG_TEST_BAD_TLS", "OMP_PG_TEST_BAD_AUTH", "OMP_PG_TEST_BAD_CONNECT",
	                     "OMP_PG_TEST_BAD_CERT"}) {
		if (auto bad_profile = std::getenv(setting)) {
			pg::Limits short_limits;
			short_limits.connect_ms = 1000;
			short_limits.reconnect_attempts = 0;
			pg::Engine bad_engine(short_limits);
			auto bad_c = bad_engine.connect(owner, bad_profile, 1);
			auto deadline = pg::Clock::now() + std::chrono::seconds(5);
			while (bad_engine.connection(owner, bad_c).state() != pg::State::failed &&
			       pg::Clock::now() < deadline) {
				bad_engine.poll();
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			check(bad_engine.connection(owner, bad_c).state() == pg::State::failed,
			      "Invalid connection must fail");
			check(!bad_engine.connection(owner, bad_c).lanes[0]->diagnostic().empty(),
			      "Connection diagnostic");
		}
	}
}

} // namespace

int main(int argc, char **) {
	try {
		if (argc > 1) {
			auto profile = std::getenv("OMP_PG_TEST_SERVICE");
			if (!profile) {
				std::cout << "SKIP: set OMP_PG_TEST_SERVICE\n";
				return 77;
			}
			integration(profile);
			integration_limits(profile);
		} else {
			unit();
		}
		std::cout << checks << " checks passed\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
