// Real libpq, test-owned loopback peers, and an independent disposable TLS database.
#include "pg/engine.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

namespace {

constexpr pg::Owner owner = 1002, healthy_owner = 1003;
constexpr int lanes = 4;
unsigned checks = 0;
std::string phase;

void check(bool condition, const char *message) {
	++checks;
	if (!condition) {
		// Print before unwinding: a destructor stall must not hide the failed assertion.
		std::cerr << "FAIL " << phase << ": " << message << std::endl;
		throw std::runtime_error(message);
	}
}

template <class F> void until(F condition) {
	const auto deadline = pg::Clock::now() + std::chrono::seconds(10);
	while (!condition()) {
		if (pg::Clock::now() >= deadline) {
			check(false, "Connection stall test deadline");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

std::size_t markers(const std::filesystem::path &directory, const char *prefix) {
	std::size_t count = 0;
	for (const auto &entry : std::filesystem::directory_iterator(directory)) {
		if (entry.path().filename().string().starts_with(prefix)) {
			++count;
		}
	}
	return count;
}

void rejected(pg::Engine &engine, pg::Id connection) {
	bool refused = false;
	try {
		engine.query(owner, connection, "SELECT 1", 0, 0, 0, false);
	} catch (const pg::Failure &failure) {
		refused = failure.code == pg::Error::not_ready;
	}
	check(refused, "Incomplete connection must reject SQL acceptance");
	for (const auto &lane : engine.connection(owner, connection).lanes) {
		const auto stats = lane->snapshot();
		check(stats.sql_commands == 0 && stats.queued_jobs == 0 && stats.running_jobs == 0,
		      "No SQL was queued or dispatched to an incomplete session");
	}
}

void exercise(const char *healthy_service, const std::filesystem::path &root,
              const std::string &mode) {
	const auto directory = root / mode;
	const auto service = "stall_" + mode;
	for (const std::string action : {"timeout", "disconnect", "unload", "destroy"}) {
		phase = "stall " + mode + " " + action;
		std::cout << phase << " start" << std::endl;
		const auto baseline = markers(directory, "ready-");
		check(markers(directory, "closed-") == baseline, "Previous peer connections closed");
		pg::Limits limits;
		// Expiry must override the profile's connect_timeout=60. Stop/unload must
		// interrupt a much longer deadline, rather than merely wait for expiry.
		limits.connect_ms = action == "timeout" ? 1000 : 60000;
		limits.reconnect_attempts = action == "timeout" ? 1 : 0;
		limits.reconnect_base_ms = 100;
		auto engine = std::make_unique<pg::Engine>(limits);
		const auto healthy = engine->connect(healthy_owner, healthy_service, 1);
		until([&] {
			return engine->connection(healthy_owner, healthy).state() == pg::State::connected;
		});
		engine->poll();
		const auto connection = engine->connect(owner, service, lanes);
		until([&] {
			return markers(directory, "ready-") >= baseline + lanes;
		});
		check(engine->connection(owner, connection).state() != pg::State::connected,
		      "A protocol stall cannot become ready");
		rejected(*engine, connection);
		// A healthy independent owner must still execute while all four peers stall.
		auto job = engine->job(healthy_owner,
		                       engine->query(healthy_owner, healthy, "SELECT 42", 0, 0, 0, false));
		until([&] {
			engine->poll();
			return pg::terminal(job->status.load()) && job->delivered;
		});
		check(job->status.load() == pg::Status::confirmed && job->results.size() == 1 &&
		          job->results[0].rows.size() == 1 && job->results[0].rows[0].size() == 1 &&
		          job->results[0].rows[0][0] == "42",
		      "Healthy owner progresses while other sessions are stalled");
		if (action == "timeout") {
			until([&] {
				return engine->connection(owner, connection).state() == pg::State::failed;
			});
			// Logical failed can precede the last worker's final retry.
			until([&] {
				for (const auto &lane : engine->connection(owner, connection).lanes) {
					if (lane->state.load() != pg::State::failed) {
						return false;
					}
				}
				return true;
			});
			check(markers(directory, "ready-") == baseline + 2 * lanes,
			      "Each lane reaches both the initial handshake and its bounded retry");
			for (const auto &lane : engine->connection(owner, connection).lanes) {
				check(lane->snapshot().connection_attempts == 2, "Exactly one reconnect attempt");
				check(!lane->diagnostic().empty(), "Timed-out handshake retains diagnostics");
			}
			rejected(*engine, connection);
		}
		const auto stop_start = pg::Clock::now();
		if (action == "disconnect" || action == "timeout") {
			engine->disconnect(owner, connection);
			// No poll/reap until every worker can be inspected.
			until([&] {
				return engine->connection(owner, connection).state() == pg::State::disconnected;
			});
			for (const auto &lane : engine->connection(owner, connection).lanes) {
				check(lane->stopped.load(), "Every stalled worker acknowledges shutdown");
			}
		} else if (action == "unload") {
			engine->unload(owner);
			bool gone = false;
			until([&] {
				const auto dispatch = engine->poll();
				for (const auto &event : dispatch.connections) {
					// The component filters VM generations before invoking Pawn callbacks.
					check(event.owner != owner || event.state != pg::State::connected,
					      "Unloaded stalled connection never reports ready");
				}
				check(dispatch.jobs.empty() && dispatch.streams.empty(), "No dead-owner jobs/rows");
				try {
					engine->connection(owner, connection);
				} catch (const pg::Failure &failure) {
					gone = failure.code == pg::Error::invalid;
				}
				return gone;
			});
		}
		std::cout << phase << " destroy" << std::endl;
		engine.reset();
		check(pg::Clock::now() - stop_start < std::chrono::seconds(10),
		      "Stop must interrupt the 60-second connection deadline");
		const auto expected = baseline + (action == "timeout" ? 2 : 1) * lanes;
		until([&] {
			return markers(directory, "closed-") == expected;
		});
		std::cout << phase << " complete" << std::endl;
	}
}

} // namespace

int main() {
	const auto service = std::getenv("OMP_PG_TEST_SERVICE");
	const auto root = std::getenv("OMP_PG_TEST_STALL_ROOT");
	if (!service || !root) {
		std::cout << "SKIP: run_postgres.py disposable database and stall fixtures required\n";
		return 77;
	}
	try {
		std::cout << "stall_lanes=" << lanes << " libpq=" << PQlibVersion()
		          << " pointer_bytes=" << sizeof(void *) << std::endl;
		for (const std::string mode : {"ssl", "tls", "startup"}) {
			exercise(service, std::filesystem::path(root), mode);
		}
		std::cout << checks << " stalled connection checks passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL " << phase << ": " << error.what() << std::endl;
		return 1;
	}
}
