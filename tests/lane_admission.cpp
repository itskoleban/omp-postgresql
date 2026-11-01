// Compile the actual Engine and Lane with deterministic libpq/startup seams.
// No database, network timing, global allocator replacement or production hook.
#include "pg/engine.hpp"
#include "pg/sql_sections.hpp"
#include "pg/stream.hpp"
#include <algorithm>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

namespace admission_test {
bool fail_start = false;
bool fail_recovery = false;
bool stop_before_enqueue = false; // Main-thread-only checkpoint control.
std::atomic<unsigned> starts{0};
std::atomic<bool> receiving{false}, release_failure{false};

void check(bool condition, const char *message) {
	if (!condition) throw std::runtime_error(message);
}
template <class Predicate> void until(Predicate predicate) {
	const auto deadline = pg::Clock::now() + std::chrono::seconds(5);
	while (!predicate()) {
		check(pg::Clock::now() < deadline, "Worker admission watchdog");
		std::this_thread::yield();
	}
}
PGconn *start(const char *const *, const char *const *, int) {
	starts.fetch_add(1);
	// Real opaque storage and RAII deleter; no socket is opened.
	return PQconnectStart("invalid_admission_test_option=1");
}
PostgresPollingStatusType poll(PGconn *) {
	return fail_start || (fail_recovery && starts.load() != 1)
	           ? PGRES_POLLING_FAILED : PGRES_POLLING_OK;
}
void allocation_checkpoint(std::string_view site) {
	if (site == "connect_diagnostic") throw std::bad_alloc();
}
int nonblocking(PGconn *, int) { return 0; }
PQnoticeProcessor notice(PGconn *, PQnoticeProcessor, void *) { return nullptr; }
ConnStatusType status(const PGconn *) { return CONNECTION_OK; }
int send(PGconn *, const char *, int, const Oid *, const char *const *, const int *,
         const int *, int) { return 1; }
int single_row(PGconn *) { return 1; }
int flush(PGconn *) { return 0; }
int consume(PGconn *) {
	receiving.store(true);
	until([] { return release_failure.load(); });
	return 0;
}
void checkpoint(pg::Lane &lane) {
	if (!stop_before_enqueue) return;
	stop_before_enqueue = false;
	// The previously accepted job loses transport, then diagnostic_ allocation
	// during failed recovery throws. The owning Engine/main thread stays alive.
	release_failure.store(true);
	until([&] { return lane.stopped.load(); });
}
} // namespace admission_test

// Header guards restrict substitutions to the implementation bodies.
#define PQconnectStartParams admission_test::start
#define PQconnectPoll admission_test::poll
#define PQsetnonblocking admission_test::nonblocking
#define PQsetNoticeProcessor admission_test::notice
#define PQstatus admission_test::status
#define PQsendQueryParams admission_test::send
#define PQsetSingleRowMode admission_test::single_row
#define PQflush admission_test::flush
#define PQconsumeInput admission_test::consume
#define PG_ALLOCATION_CHECKPOINT(site) admission_test::allocation_checkpoint(site)
#include "../src/session.cpp"
#undef PG_ALLOCATION_CHECKPOINT
#undef PQconsumeInput
#undef PQflush
#undef PQsetSingleRowMode
#undef PQsendQueryParams
#undef PQstatus
#undef PQsetNoticeProcessor
#undef PQsetnonblocking
#undef PQconnectPoll
#undef PQconnectStartParams
#define PG_SUBMIT_QUEUE_CHECKPOINT(lane) admission_test::checkpoint(lane)
#include "../src/engine.cpp"
#undef PG_SUBMIT_QUEUE_CHECKPOINT

namespace pg {
struct EngineTestAccess {
	static bool retained(const Engine &engine, std::size_t count) {
		return engine.jobs_.size() == count && engine.ending_.empty();
	}
};
} // namespace pg

namespace {
using admission_test::check;
using admission_test::until;
constexpr pg::Owner owner = 431;

template <class Action> void rejects(Action action) {
	bool rejected = false;
	try { action(); }
	catch (const pg::Failure &error) { rejected = error.code == pg::Error::not_ready; }
	check(rejected, "Stopped worker accepted work after the Engine readiness snapshot");
}

void snapshot_race(int kind) {
	admission_test::starts.store(0);
	admission_test::receiving.store(false);
	admission_test::release_failure.store(false);
	admission_test::fail_recovery = true;
	pg::Engine engine;
	const auto connection = engine.connect(owner, "admission_stub", 1);
	auto &pool = engine.connection(owner, connection);
	auto &lane = *pool.lanes.front();
	until([&] { return pool.state() == pg::State::connected; });
	const auto first = engine.query(owner, connection, "SELECT 1", 0, 0, 0, true);
	auto accepted = engine.job(owner, first);
	until([] { return admission_test::receiving.load(); });
	const auto statement = engine.prepare(owner, connection, "SELECT 1", 0);
	engine.statement(owner, statement).add(engine.limits());
	admission_test::stop_before_enqueue = true;
	// The checkpoint executes only after Engine observed a connected pool and
	// inserted the candidate job, immediately before attempting queue admission.
	rejects([&] {
		if (kind == 0) engine.query(owner, connection, "SELECT 1", 0, 0, 0, true);
		else if (kind == 1) engine.begin(owner, connection, 0, 0, true);
		else engine.execute(owner, statement, 0, 0, 0, true);
	});
	check(pg::EngineTestAccess::retained(engine, 1),
	      "Rejected admission retained a job/ending marker");
	check(pool.reservations[0] == 0, "Rejected BEGIN reserved its stopped lane");
	check(engine.statement(owner, statement).batch.size() == 1,
	      "Rejected statement admission consumed its snapshots");
	const auto stats = lane.snapshot();
	check(stats.queued_jobs == 0 && stats.completed_jobs == 1 && stats.sql_commands == 1 &&
	          stats.connection_attempts == 2 && stats.reconnects == 0,
	      "Rejected admission was counted/dispatched or failed SQL was replayed");
	const auto dispatch = engine.poll();
	check(dispatch.jobs == std::vector{accepted} && engine.poll().jobs.empty(),
	      "Previously accepted SQL lost or duplicated terminal publication");
	check(accepted->status.load() == pg::Status::uncertain &&
	          accepted->error == pg::Error::connection && accepted->completed == 0 &&
	          accepted->items_status == std::vector{pg::ItemStatus::uncertain},
	      "Previously dispatched SQL lost conservative uncertain metadata");
	engine.close_job(owner, first);
	check(pg::EngineTestAccess::retained(engine, 0), "Accepted terminal job could not be released");
	std::cout << "readiness_stop_race kind=" << kind
	          << " PASS rejected=not_ready retained_jobs=0 queued=0 commands=1 uncertain=1\n";
}

void worker_failure() {
	pg::CompletionQueue completed;
	admission_test::fail_start = true;
	{
		pg::Lane lane("admission_stub", {}, "unused", completed);
		until([&] { return lane.stopped.load(); });
		rejects([&] { lane.enqueue(std::make_shared<pg::Job>()); });
		check(lane.snapshot().queued_jobs == 0 && !completed.pop(),
		      "Failed worker retained or completed a rejected job");
	}
	admission_test::fail_start = false;
	admission_test::fail_recovery = false;
	std::cout << "worker_start_exception PASS rejected=not_ready queued=0\n";
}

void accepted_before_stop() {
	pg::CompletionQueue completed;
	pg::Lane lane("admission_stub", {}, "unused", completed);
	until([&] { return lane.state.load() == pg::State::connected; });
	auto job = std::make_shared<pg::Job>();
	job->id = 1;
	job->cancel.store(true); // Worker cannot enter libpq SQL through the stub.
	job->accepted_at = pg::Clock::now();
	job->items_status.assign(1, pg::ItemStatus::not_executed);
	lane.enqueue(job);
	lane.close();
	until([&] { return lane.stopped.load(); });
	check(completed.pop() == job && !completed.pop(),
	      "Work accepted before shutdown lost or duplicated its terminal publication");
	check(job->status.load() == pg::Status::cancelled && job->error == pg::Error::cancelled &&
	          job->items_status == std::vector{pg::ItemStatus::cancelled},
	      "Work accepted before shutdown lost terminal cancellation metadata");
	const auto stats = lane.snapshot();
	check(stats.queued_jobs == 0 && stats.completed_jobs == 1 && stats.sql_commands == 0,
	      "Work accepted before shutdown lost its counters or dispatched SQL");
	std::cout << "accepted_before_stop PASS completions=1 cancelled=1 commands=0\n";
}
} // namespace

int main() {
	try {
		for (int kind = 0; kind != 3; ++kind) snapshot_race(kind);
		worker_failure();
		accepted_before_stop();
		std::cout << "lane_admission PASS cases=5\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "lane_admission FAIL " << error.what() << '\n';
		return 1;
	}
}
