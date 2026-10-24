// Real Engine/libpq with disposable PostgreSQL and bounded loopback wire faults.
#include "pg/engine.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

namespace {
constexpr pg::Owner owner = 1802;
constexpr int context = 2817;
std::string scenario;
unsigned checks = 0;
void check(bool ok, const char *message) {
	++checks;
	if (!ok) {
		std::cerr << "FAIL " << scenario << ": " << message << std::endl;
		throw std::runtime_error(message);
	}
}
template <class F> void until(F condition) {
	const auto deadline = pg::Clock::now() + std::chrono::seconds(10);
	while (!condition()) {
		check(pg::Clock::now() < deadline, "Observed-event deadline");
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}
struct Test {
	pg::Engine engine;
	std::map<pg::Id, int> deliveries;
	pg::Id observer, writer;
	static pg::Limits limits() {
		pg::Limits value;
		value.query_ms = 1500;
		value.connect_ms = 2000;
		value.reconnect_attempts = 1;
		value.reconnect_base_ms = 50;
		return value;
	}
	explicit Test(const char *service, const std::string &profile) : engine(limits()) {
		observer = engine.connect(owner, service, 1);
		writer = engine.connect(owner, profile, 1);
		ready(observer);
		ready(writer);
	}
	void poll() {
		for (auto &job : engine.poll().jobs) {
			check(++deliveries[job->id] == 1, "Duplicate terminal callback publication");
		}
	}
	void ready(pg::Id connection) {
		until([&] {
			poll();
			return engine.connection(owner, connection).state() == pg::State::connected;
		});
	}
	std::shared_ptr<pg::Job> wait(pg::Id id) {
		auto job = engine.job(owner, id);
		until([&] {
			poll();
			return job->delivered;
		});
		check(pg::terminal(job->status.load()) && deliveries[id] == 1,
		      "Exactly one terminal delivery per accepted job");
		std::cout << "job=" << id << " status=" << int(job->status.load())
		          << " error=" << int(job->error) << " completed=" << job->completed
		          << " error_index=" << job->error_index << " items=";
		for (auto item : job->items_status) {
			std::cout << int(item) << ',';
		}
		std::cout << " message=" << job->message << std::endl;
		return job;
	}
	pg::Id query(pg::Id connection, const std::string &sql, pg::Id tx = 0) {
		std::cout << "SQL connection=" << connection << " tx=" << tx << " " << sql << std::endl;
		return engine.query(owner, connection, sql, 0, tx, context, true);
	}
	std::string sql(pg::Id connection, const std::string &text, pg::Id tx = 0) {
		auto job = wait(query(connection, text, tx));
		check(job->status.load() == pg::Status::confirmed, "Control SQL confirmation");
		std::string value;
		if (!job->results.at(0).rows.empty()) {
			value = job->results[0].rows[0][0].value_or("");
		}
		std::cout << "SQL result=" << value << " affected=" << job->results[0].affected
		          << std::endl;
		engine.close_job(owner, job->id);
		return value;
	}
	pg::Id begin() {
		const auto id = engine.begin(owner, writer, 0, context, true);
		check(wait(id)->status.load() == pg::Status::confirmed, "Control BEGIN");
		return id;
	}
	void uncertain(const std::shared_ptr<pg::Job> &job, pg::Error error) {
		check(job->status.load() == pg::Status::uncertain && job->error == error &&
		          job->completed == 0 && job->error_index == 0 &&
		          job->items_status == std::vector{pg::ItemStatus::uncertain},
		      "Dispatched transport fault retains uncertain item and cause");
		check(job->owner == owner && job->connection == writer && job->lane == 0 &&
		          job->context == context,
		      "Job identity survives fault");
	}
	void release(std::shared_ptr<pg::Job> &job) {
		const auto id = job->id;
		engine.close_job(owner, id);
		std::weak_ptr<pg::Job> weak = job;
		job.reset();
		until([&] {
			poll();
			return weak.expired();
		});
		bool invalid = false;
		try {
			engine.job(owner, id);
		} catch (const pg::Failure &e) {
			invalid = e.code == pg::Error::invalid;
		}
		check(invalid, "Released job handle is invalid");
	}
	void recovered(const std::string &pid) {
		ready(writer);
		check(sql(writer, "SELECT pg_backend_pid()") != pid, "Recovery replaces physical backend");
		check(sql(writer, "SELECT 42") == "42", "New explicit SQL succeeds after recovery");
	}
};

void response_loss(const char *service, const std::string &name) {
	scenario = name;
	std::cout << "SCENARIO " << scenario << " start" << std::endl;
	Test t(service, "wire_" + name);
	const auto pid = t.sql(t.writer, "SELECT pg_backend_pid()");
	const auto table = "phase2_" + name;
	t.sql(t.observer,
	      "CREATE TABLE " + table + "(id bigint GENERATED ALWAYS AS IDENTITY, n integer)");
	pg::Id tx = 0, id = 0;
	if (name == "begin") {
		id = t.engine.begin(owner, t.writer, 0, context, true);
	} else if (name == "savepoint" || name == "rollback_to" || name == "release") {
		tx = t.begin();
		t.sql(t.writer, "INSERT INTO " + table + "(n) VALUES(1)", tx);
		if (name != "savepoint") {
			auto point =
			    t.wait(t.engine.savepoint(owner, tx, "phase2", pg::Kind::savepoint, context, true));
			check(point->status.load() == pg::Status::confirmed, "Control SAVEPOINT");
			t.release(point);
		}
		id = t.engine.savepoint(owner, tx, "phase2",
		                        name == "savepoint"     ? pg::Kind::savepoint
		                        : name == "rollback_to" ? pg::Kind::rollback_to
		                                                : pg::Kind::release_savepoint,
		                        context, true);
	} else if (name == "commit" || name == "rollback") {
		tx = t.begin();
		t.sql(t.writer, "INSERT INTO " + table + "(n) VALUES(1)", tx);
		id = t.engine.finish(owner, tx, name == "commit", context, true);
	} else {
		id = t.query(t.writer, "INSERT INTO " + table + "(n) VALUES(1) /* phase2_fault */");
	}
	auto job = t.wait(id);
	t.uncertain(job, name == "hold" ? pg::Error::timeout : pg::Error::connection);
	check(job->transaction == tx, "Transaction identity retained");
	t.release(job);
	if (name == "savepoint" || name == "rollback_to" || name == "release") {
		// Transport recovery does not restore the application's logical transaction.
		t.ready(t.writer);
		auto finish = t.wait(t.engine.finish(owner, tx, false, context, true));
		check(finish->status.load() == pg::Status::failed &&
		          finish->error == pg::Error::transaction && finish->completed == 0 &&
		          finish->error_index == -1 &&
		          finish->items_status == std::vector{pg::ItemStatus::not_executed},
		      "Old transaction is rejected on replacement session");
		t.release(finish);
	}
	if (tx) {
		t.engine.close_job(owner, tx);
	}
	t.recovered(pid);
	// Wait for the original backend to vanish before checking its transaction rollback.
	until([&] {
		return t.sql(t.observer, "SELECT count(*) FROM pg_stat_activity WHERE pid=" + pid) == "0";
	});
	const auto expected = (name == "commit" || name == "insert" || name == "hold") ? "1" : "0";
	check(t.sql(t.observer, "SELECT count(*) FROM " + table) == expected,
	      "Independent PostgreSQL row count matches committed/aborted operation");
	if (name != "begin") {
		// Identity sequences are not transactional: replay attempts advance them even on rollback.
		check(t.sql(t.observer, "SELECT last_value FROM " + table + "_id_seq") == "1",
		      "Sequence proves one write attempt, including aborted transactions");
		if (name == "partial_parse") {
			check(t.sql(t.observer, "SELECT is_called FROM " + table + "_id_seq") == "f",
			      "Incomplete Parse never reached an INSERT attempt");
		}
	}
	const auto next = t.begin();
	auto finish = t.wait(t.engine.finish(owner, next, false, context, true));
	check(finish->status.load() == pg::Status::confirmed,
	      "New explicit logical transaction succeeds");
	t.release(finish);
	t.engine.close_job(owner, next);
	t.sql(t.observer, "DROP TABLE " + table);
	std::cout << "SCENARIO " << scenario << " PASS" << std::endl;
}

void batch_loss(const char *service) {
	scenario = "batch";
	std::cout << "SCENARIO batch start" << std::endl;
	Test t(service, "wire_batch");
	const auto pid = t.sql(t.writer, "SELECT pg_backend_pid()");
	t.sql(t.observer,
	      "CREATE TABLE phase2_batch(id bigint GENERATED ALWAYS AS IDENTITY, n integer)");
	const auto batch = t.engine.create_batch(owner, t.writer);
	for (const auto &sql : {"SELECT 17", "INSERT INTO phase2_batch(n) VALUES(1)",
	                        "INSERT INTO phase2_batch(n) VALUES(2)"}) {
		const auto statement = t.engine.prepare(owner, t.writer, sql, 0);
		t.engine.statement(owner, statement).add(t.engine.limits());
		t.engine.add_batch(owner, batch, statement);
		t.engine.close_statement(owner, statement);
	}
	auto job = t.wait(t.engine.execute_batch(owner, batch, 0, 0, context, true));
	check(job->status.load() == pg::Status::uncertain && job->error == pg::Error::connection &&
	          job->completed == 1 && job->error_index == 1 && job->results.size() == 1 &&
	          job->items_status == std::vector{pg::ItemStatus::confirmed, pg::ItemStatus::uncertain,
	                                           pg::ItemStatus::not_executed},
	      "Batch preserves confirmed prefix, uncertain item and unsent suffix");
	t.release(job);
	t.engine.close_batch(owner, batch);
	t.recovered(pid);
	check(t.sql(t.observer, "SELECT count(*) FROM phase2_batch WHERE n=1") == "1" &&
	          t.sql(t.observer, "SELECT count(*) FROM phase2_batch WHERE n=2") == "0" &&
	          t.sql(t.observer, "SELECT last_value FROM phase2_batch_id_seq") == "1",
	      "Batch writes exactly once; suffix and uncertain write are never replayed");
	t.sql(t.observer, "DROP TABLE phase2_batch");
	std::cout << "SCENARIO batch PASS" << std::endl;
}

void fragment(const char *service) {
	scenario = "fragment";
	std::cout << "SCENARIO fragment start" << std::endl;
	Test t(service, "wire_fragment");
	// Multiple frontend and backend protocol frames, not OS short-write injection.
	check(
	    t.sql(t.writer, "SELECT repeat('abcdefg',1000)") == std::string("") +
	                                                            [] {
		                                                            std::string value;
		                                                            for (int i = 0; i < 1000; ++i) {
			                                                            value += "abcdefg";
		                                                            }
		                                                            return value;
	                                                            }(),
	    "Seven-byte proxy writes preserve full result contents");
	const auto tx = t.begin();
	for (auto action : {pg::Kind::savepoint, pg::Kind::rollback_to, pg::Kind::release_savepoint}) {
		auto job = t.wait(t.engine.savepoint(owner, tx, "fragment", action, context, true));
		check(job->status.load() == pg::Status::confirmed, "Fragmented savepoint command");
		t.release(job);
	}
	auto finish = t.wait(t.engine.finish(owner, tx, true, context, true));
	check(finish->status.load() == pg::Status::confirmed, "Fragmented COMMIT");
	t.release(finish);
	t.engine.close_job(owner, tx);
	std::cout << "SCENARIO fragment PASS" << std::endl;
}

void tls_hold(const char *service, const std::filesystem::path &root, bool shutdown) {
	scenario = shutdown ? "tls_shutdown" : "tls_hold";
	std::cout << "SCENARIO " << scenario << " start" << std::endl;
	auto t = std::make_unique<Test>(service, "wire_" + scenario);
	const auto pid = t->sql(t->writer, "SELECT pg_backend_pid()");
	check(t->sql(t->observer, "SELECT ssl FROM pg_stat_ssl WHERE pid=" + pid) == "t",
	      "Independent observer confirms real end-to-end TLS");
	const auto table = "phase2_" + scenario + "_queued";
	t->sql(t->observer,
	       "CREATE TABLE " + table + "(id bigint GENERATED ALWAYS AS IDENTITY, n integer)");
	const auto text = "SELECT pg_sleep(60) /* phase2_tls_" + scenario + " */";
	auto active = t->engine.job(owner, t->query(t->writer, text));
	until([&] {
		return t->sql(t->observer, "SELECT count(*) FROM pg_stat_activity WHERE pid=" + pid +
		                               " AND state='active' AND wait_event='PgSleep' AND query='" +
		                               text + "'") == "1";
	});
	// PostgreSQL, rather than RUNNING, establishes receipt of this exact SQL.
	{
		std::ofstream arm(root / scenario / "arm");
		check(bool(arm), "Arm opaque TLS hold");
	}
	auto queued =
	    t->engine.job(owner, t->query(t->writer, "INSERT INTO " + table +
	                                                 "(n) VALUES(999) /* phase2_never_sent */"));
	check(t->sql(t->observer, "SELECT pg_cancel_backend(" + pid + ")") == "t",
	      "Cancel observed backend");
	until([&] {
		return std::filesystem::exists(root / scenario / "injected");
	});
	if (shutdown) {
		const auto start = pg::Clock::now();
		t.reset();
		check(pg::Clock::now() - start < std::chrono::seconds(5),
		      "Engine destruction interrupts TLS work");
		check(active->status.load() == pg::Status::uncertain &&
		          active->error == pg::Error::cancelled &&
		          active->items_status == std::vector{pg::ItemStatus::uncertain} &&
		          queued->status.load() == pg::Status::cancelled &&
		          queued->items_status == std::vector{pg::ItemStatus::cancelled},
		      "Shutdown distinguishes dispatched and queued work");
		Test verifier(service, service);
		until([&] {
			return verifier.sql(verifier.observer,
			                    "SELECT count(*) FROM pg_stat_activity WHERE pid=" + pid) == "0";
		});
		check(verifier.sql(verifier.observer, "SELECT count(*) FROM " + table) == "0" &&
		          verifier.sql(verifier.observer, "SELECT is_called FROM " + table + "_id_seq") ==
		              "f",
		      "Shutdown closes original backend and never dispatches queued write");
		verifier.sql(verifier.observer, "DROP TABLE " + table);
	} else {
		auto completed = t->wait(active->id);
		t->uncertain(completed, pg::Error::timeout);
		auto pending = t->wait(queued->id);
		check(pending->status.load() == pg::Status::failed &&
		          pending->error == pg::Error::connection && pending->completed == 0 &&
		          pending->error_index == -1 &&
		          pending->items_status == std::vector{pg::ItemStatus::not_executed},
		      "Queued SQL failed before dispatch, separate from uncertain SQL");
		active.reset();
		queued.reset();
		t->release(completed);
		t->release(pending);
		t->recovered(pid);
		check(t->sql(t->observer, "SELECT count(*) FROM " + table) == "0" &&
		          t->sql(t->observer, "SELECT is_called FROM " + table + "_id_seq") == "f",
		      "Independent SQL and sequence prove queued write never executed or replayed");
		t->sql(t->observer, "DROP TABLE " + table);
	}
	std::cout << "SCENARIO " << scenario << " PASS" << std::endl;
}
} // namespace

int main() {
	const auto service = std::getenv("OMP_PG_TEST_SERVICE");
	const auto root = std::getenv("OMP_PG_TEST_WIRE_ROOT");
	if (!service || !root) {
		std::cout << "Disposable wire fixture required\n";
		return 77;
	}
	try {
		std::cout << "network_recovery libpq=" << PQlibVersion()
		          << " pointer_bytes=" << sizeof(void *) << std::endl;
		for (const std::string name : {"begin", "savepoint", "rollback_to", "release", "commit",
		                               "rollback", "insert", "hold", "partial_parse"}) {
			response_loss(service, name);
		}
		batch_loss(service);
		fragment(service);
		tls_hold(service, root, false);
		tls_hold(service, root, true);
		std::cout << "network_recovery PASS checks=" << checks << std::endl;
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "network_recovery FAIL " << scenario << " " << e.what() << std::endl;
		return 1;
	}
}
