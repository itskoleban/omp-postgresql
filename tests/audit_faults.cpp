// Real Engine/libpq tests. Only use the disposable run_postgres.py fixture.
#include "pg/engine.hpp"
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
constexpr pg::Owner owner = 803;
void check(bool ok, const char *why) {
	if (!ok) {
		throw std::runtime_error(why);
	}
}
template <class F> void until(pg::Engine &e, F f) {
	const auto deadline = pg::Clock::now() + std::chrono::seconds(10);
	while (!f()) {
		e.poll();
		check(pg::Clock::now() < deadline, "Fault test deadline");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
void ready(pg::Engine &e, pg::Id c) {
	until(e, [&] {
		return e.connection(owner, c).state() == pg::State::connected;
	});
}
std::shared_ptr<pg::Job> wait(pg::Engine &e, pg::Id id) {
	auto j = e.job(owner, id);
	until(e, [&] {
		return j->delivered;
	});
	return j;
}
std::string sql(pg::Engine &e, pg::Id c, std::string query, int lane = 0, pg::Id tx = 0) {
	auto j = wait(e, e.query(owner, c, std::move(query), lane, tx, 0, false));
	if (j->status.load() != pg::Status::confirmed) {
		throw std::runtime_error(j->message);
	}
	std::string value;
	if (!j->results.at(0).rows.empty()) {
		value = j->results[0].rows[0][0].value_or("");
	}
	e.close_job(owner, j->id);
	return value;
}
pg::Id begin(pg::Engine &e, pg::Id c) {
	auto id = e.begin(owner, c, 0, 0, false);
	check(wait(e, id)->status.load() == pg::Status::confirmed, "BEGIN failed");
	return id;
}
void finish_loss(const char *service, const char *loss_service) {
	pg::Engine e;
	auto observer = e.connect(owner, service, 1);
	auto writer = e.connect(owner, loss_service, 1);
	ready(e, observer);
	ready(e, writer);
	const auto table =
	    "audit_rollback_" + std::to_string(pg::Clock::now().time_since_epoch().count());
	sql(e, observer, "CREATE TABLE " + table + "(n integer PRIMARY KEY)");
	auto tx = begin(e, writer);
	sql(e, writer, "INSERT INTO " + table + " VALUES(1)", 0, tx);
	auto finish = wait(e, e.finish(owner, tx, false, 0, false));
	check(finish->status.load() == pg::Status::uncertain && finish->error == pg::Error::connection,
	      "Lost ROLLBACK response must not be reported as confirmed");
	check(finish->completed == 0 && finish->items_status == std::vector{pg::ItemStatus::uncertain},
	      "Lost ROLLBACK item metadata incorrectly claims confirmation");
	e.close_job(owner, finish->id);
	e.close_job(owner, tx);
	ready(e, writer);
	check(sql(e, observer, "SELECT count(*) FROM " + table) == "0",
	      "Rollback loss replayed a write");
	check(sql(e, writer, "SELECT 42") == "42", "Lane not reusable after rollback loss");
	sql(e, observer, "DROP TABLE " + table);
}
void during_commit(const char *service) {
	pg::Engine e;
	auto writer = e.connect(owner, service, 1);
	auto observer = e.connect(owner, service, 1);
	ready(e, writer);
	ready(e, observer);
	const auto suffix = std::to_string(pg::Clock::now().time_since_epoch().count());
	const auto table = "audit_commit_" + suffix;
	const auto function = "audit_deferred_" + suffix;
	const auto pid = sql(e, writer, "SELECT pg_backend_pid()");
	// A backend-local advisory lock blocks a deferred trigger exactly inside COMMIT.
	// The observer never terminates any PID except the one returned by our own writer.
	const auto key = pid;
	sql(e, observer, "SELECT pg_advisory_lock(" + key + ")");
	sql(e, observer, "CREATE TABLE " + table + "(n integer)");
	sql(e, observer,
	    "CREATE FUNCTION " + function +
	        "() RETURNS trigger LANGUAGE plpgsql AS $$"
	        "BEGIN PERFORM pg_advisory_xact_lock(" +
	        key + "); RETURN NEW; END $$");
	sql(e, observer,
	    "CREATE CONSTRAINT TRIGGER audit_wait AFTER INSERT ON " + table +
	        " DEFERRABLE INITIALLY DEFERRED FOR EACH ROW EXECUTE FUNCTION " + function + "()");
	auto tx = begin(e, writer);
	sql(e, writer, "INSERT INTO " + table + " VALUES(1)", 0, tx);
	auto finish_id = e.finish(owner, tx, true, 0, false);
	const auto deadline = pg::Clock::now() + std::chrono::seconds(10);
	while (sql(e, observer,
	           "SELECT count(*) FROM pg_stat_activity WHERE pid=" + pid +
	               " AND query='COMMIT' AND wait_event_type='Lock' AND wait_event='advisory'") !=
	       "1") {
		check(pg::Clock::now() < deadline, "COMMIT never reached deferred-trigger lock");
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	check(sql(e, observer, "SELECT pg_terminate_backend(" + pid + ")") == "t",
	      "Could not terminate the isolated writer during COMMIT");
	auto finish = wait(e, finish_id);
	check(finish->status.load() == pg::Status::uncertain && finish->completed == 0 &&
	          finish->items_status == std::vector{pg::ItemStatus::uncertain},
	      "Interrupted COMMIT must preserve uncertain status/item metadata");
	e.close_job(owner, finish_id);
	e.close_job(owner, tx);
	sql(e, observer, "SELECT pg_advisory_unlock(" + key + ")");
	ready(e, writer);
	check(sql(e, observer, "SELECT count(*) FROM " + table) == "0",
	      "Aborted COMMIT wrote or replayed");
	check(sql(e, writer, "SELECT pg_backend_pid()") != pid, "Dead backend reused");
	check(sql(e, writer, "SELECT 42") == "42", "Recovery after interrupted COMMIT failed");
	sql(e, observer, "DROP TABLE " + table);
	sql(e, observer, "DROP FUNCTION " + function + "()");
}

// Block an exact statement on a PostgreSQL lock, then terminate only our writer PID.
// This proves dispatch, unlike observing the Engine's pre-dispatch RUNNING state.
void terminate_statement(const char *service, bool update) {
    pg::Engine e;
    auto writer = e.connect(owner, service, 1);
    auto observer = e.connect(owner, service, 1);
    ready(e, writer);
    ready(e, observer);
    const auto table = "audit_transport_" + std::to_string(pg::Clock::now().time_since_epoch().count());
    sql(e, observer, "CREATE TABLE " + table + "(n integer)");
    sql(e, observer, "INSERT INTO " + table + " VALUES(1)");
    const auto pid = sql(e, writer, "SELECT pg_backend_pid()");
    auto lock = begin(e, observer);
    // Row/unique-index locks block execution while permitting the unnamed Parse.
    // A table ACCESS EXCLUSIVE lock could block PREPARE and would not prove dispatch.
    sql(e, observer, update ? "SELECT n FROM " + table + " WHERE n=1 FOR UPDATE"
                           : "INSERT INTO " + table + " VALUES(2)", 0, lock);
    const auto command = update ? "UPDATE " + table + " SET n=2"
                                : "INSERT INTO " + table + " VALUES(2)";
    auto statement = e.prepare(owner, writer, command, 0);
    e.statement(owner, statement).add(e.limits());
    auto id = e.execute(owner, statement, 0, 0, 0, true);
    auto pending = e.job(owner, id);
    until(e, [&] {
        // The observer's transaction sees a fresh activity snapshot after each query.
        sql(e, observer, "SELECT pg_stat_clear_snapshot()", 0, lock);
        return sql(e, observer, "SELECT count(*) FROM pg_stat_activity WHERE pid=" + pid +
                   " AND state='active' AND wait_event_type='Lock' AND wait_event='transactionid'"
                   " AND query='" + command + "'", 0, lock) == "1";
    });
    check(sql(e, observer, "SELECT pg_terminate_backend(" + pid + ")", 0, lock) == "t",
          "Could not terminate the exact blocked writer");
    wait(e, id);
    check(pending->status.load() == pg::Status::uncertain &&
          pending->error == pg::Error::connection && pending->completed == 0 &&
          pending->items_status == std::vector{pg::ItemStatus::uncertain},
          "Dispatched write transport loss lost uncertainty");
    e.close_job(owner, id);
    auto rollback = wait(e, e.finish(owner, lock, false, 0, false));
    check(rollback->status.load() == pg::Status::confirmed, "Observer rollback failed");
    e.close_job(owner, rollback->id);
    e.close_job(owner, lock);
    ready(e, writer);
    check(sql(e, writer, "SELECT pg_backend_pid()") != pid, "Writer session was not replaced");
    check(sql(e, observer, "SELECT count(*) FROM " + table + " WHERE n=1") == "1" &&
          sql(e, observer, "SELECT count(*) FROM " + table + " WHERE n=2") == "0",
          "Aborted write changed data or was replayed");
    // A prepared handle remains usable, but must prepare again in the fresh session.
    e.statement(owner, statement).add(e.limits());
    auto recovered = wait(e, e.execute(owner, statement, 0, 0, 0, false));
    check(recovered->status.load() == pg::Status::confirmed && recovered->completed == 1,
          "Prepared recovery failed");
    e.close_job(owner, recovered->id);
    check(sql(e, observer, "SELECT count(*) FROM " + table + " WHERE n=2") == "1",
          "Recovery write was missing or replayed");
    e.close_statement(owner, statement);
    sql(e, observer, "DROP TABLE " + table);
}

void cancellation_category(const char *service) {
	pg::Limits limits;
	limits.query_ms = 10000;
	pg::Engine e(limits);
	auto c = e.connect(owner, service, 1);
	auto observer = e.connect(owner, service, 1);
	ready(e, observer);
	for (int round = 0; round != 32; ++round) {
		ready(e, c);
		const auto pid = sql(e, c, "SELECT pg_backend_pid()");
		const auto query = "SELECT pg_sleep(60) /* audit_cancel_" + std::to_string(round) + " */";
		auto id = e.query(owner, c, query, 0, 0, round, false);
		auto job = e.job(owner, id);
		// RUNNING precedes dispatch. Observe this exact command executing in our own
		// backend before cancelling; the separate socket_wait unit forces the wait entry.
		until(e, [&] {
			return sql(e, observer,
			           "SELECT count(*) FROM pg_stat_activity WHERE pid=" + pid +
			               " AND state='active' AND wait_event='PgSleep' AND query='" + query +
			               "'") == "1";
		});
		job->cancel.store(true);
		wait(e, id);
		if (job->error != pg::Error::cancelled) {
			std::cerr << "cancel round=" << round << " status=" << int(job->status.load())
			          << " error=" << int(job->error) << " message=" << job->message << std::endl;
		}
		check(job->status.load() == pg::Status::uncertain && job->error == pg::Error::cancelled &&
		          job->completed == 0 &&
		          job->items_status == std::vector{pg::ItemStatus::uncertain},
		      "Observed cancellation lost its cancellation error category");
		e.close_job(owner, id);
	}
}

void timeout_category(const char *service) {
	pg::Limits limits;
	limits.query_ms = 30;
	pg::Engine e(limits);
	auto c = e.connect(owner, service, 1);
	for (int round = 0; round != 8; ++round) {
		ready(e, c);
		auto job = wait(e, e.query(owner, c, "SELECT pg_sleep(1)", 0, 0, round, false));
		if (job->error != pg::Error::timeout) {
			std::cerr << "timeout round=" << round << " status=" << int(job->status.load())
			          << " error=" << int(job->error) << " message=" << job->message << std::endl;
		}
		check(job->status.load() == pg::Status::uncertain && job->error == pg::Error::timeout &&
		          job->completed == 0 &&
		          job->items_status == std::vector{pg::ItemStatus::uncertain},
		      "Dispatched timeout lost cause or uncertain item metadata");
		e.close_job(owner, job->id);
	}
}
} // namespace
int main() {
	const auto service = std::getenv("OMP_PG_TEST_SERVICE");
	const auto rollback = std::getenv("OMP_PG_TEST_ROLLBACK_LOSS");
	if (!service || !rollback) {
		std::cout << "Disposable fault profiles required\n";
		return 77;
	}
	try {
		std::cout << "fault phase: ROLLBACK response loss" << std::endl;
		finish_loss(service, rollback);
		std::cout << "fault phase: terminate during deferred COMMIT" << std::endl;
		during_commit(service);
		std::cout << "fault phase: terminate exact blocked INSERT" << std::endl;
		terminate_statement(service, false);
		std::cout << "fault phase: terminate exact blocked UPDATE" << std::endl;
		terminate_statement(service, true);
		std::cout << "fault phase: cancellation category race rounds=32" << std::endl;
		cancellation_category(service);
		std::cout << "fault phase: timeout category rounds=8 query_ms=30" << std::endl;
		timeout_category(service);
		std::cout << "audit_faults PASS\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "audit_faults FAIL " << e.what() << '\n';
		return 1;
	}
}
