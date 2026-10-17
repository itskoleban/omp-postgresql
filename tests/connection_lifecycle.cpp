// Run through run_postgres.py or against explicitly disposable service profiles.
#include "pg/engine.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <set>
#include <thread>

namespace {

constexpr pg::Owner owner = 1001;
unsigned checks = 0;
std::string phase;

void trace(std::string value) {
	phase = std::move(value);
	std::cout << phase << std::endl;
}

[[noreturn]] void fail(const char *message) {
	// Emit before stack unwinding: destruction itself may be the stalled operation.
	std::cerr << "FAIL " << phase << ": " << message << std::endl;
	throw std::runtime_error(message);
}

void check(bool condition, const char *message) {
	++checks;
	if (!condition) {
		fail(message);
	}
}

template <class F> void until(pg::Engine &engine, F condition) {
	const auto deadline = pg::Clock::now() + std::chrono::seconds(15);
	while (!condition()) {
		engine.poll();
		if (pg::Clock::now() >= deadline) {
			fail("Deadline expired");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

unsigned rounds() {
	const auto value = std::getenv("OMP_PG_CONNECTION_ROUNDS");
	if (!value) {
		return 10;
	}
	unsigned result = 0;
	const auto end = value + std::char_traits<char>::length(value);
	const auto parsed = std::from_chars(value, end, result);
	if (parsed.ec != std::errc{} || parsed.ptr != end || result < 1 || result > 1000) {
		fail("OMP_PG_CONNECTION_ROUNDS must be 1..1000");
	}
	return result;
}

void exercise(const char *service, const char *bad_service, unsigned count) {
	pg::Limits limits;
	limits.reconnect_base_ms = 1000;
	for (unsigned round = 0; round < count; ++round) {
		const auto prefix = "connection round " + std::to_string(round) + " ";
		// Some workers may be starting or inside libpq when destruction requests stop.
		trace(prefix + "immediate create");
		{
			pg::Engine engine(limits);
			engine.connect(owner, service, 8);
			trace(prefix + "immediate destroy");
		}
		trace(prefix + "ready create");
		{
			pg::Engine engine(limits);
			const auto connection = engine.connect(owner, service, 8);
			until(engine, [&] {
				return engine.connection(owner, connection).state() == pg::State::connected;
			});
			std::vector<std::shared_ptr<pg::Job>> jobs;
			for (int lane = 0; lane < 8; ++lane) {
				jobs.push_back(
				    engine.job(owner, engine.query(owner, connection, "SELECT pg_backend_pid()",
					                               lane, 0, 0, false)));
			}
			std::set<std::string> backends;
			for (const auto &job : jobs) {
				until(engine, [&] {
					return pg::terminal(job->status.load()) && job->delivered;
				});
				check(job->status.load() == pg::Status::confirmed && job->completed == 1,
				      "Each lane executes successfully");
				check(job->results.size() == 1 && job->results[0].rows.size() == 1 &&
				          job->results[0].rows[0].size() == 1 && job->results[0].rows[0][0],
				      "Backend identity returned");
				backends.insert(*job->results[0].rows[0][0]);
			}
			check(backends.size() == 8, "Each lane has a distinct physical session");
			trace(prefix + "ready destroy");
		}
		trace(prefix + "disconnect during startup");
		{
			pg::Engine engine(limits);
			const auto connection = engine.connect(owner, service, 8);
			engine.disconnect(owner, connection);
			bool rejected = false;
			try {
				engine.query(owner, connection, "SELECT 1", 0, 0, 0, false);
			} catch (const pg::Failure &error) {
				rejected = error.code == pg::Error::not_ready;
			}
			check(rejected, "Closing pool rejects SQL acceptance");
			// Do not poll/reap the connection before inspecting its stopped workers.
			const auto deadline = pg::Clock::now() + std::chrono::seconds(15);
			while (engine.connection(owner, connection).state() != pg::State::disconnected) {
				if (pg::Clock::now() >= deadline) {
					fail("Startup disconnect deadline");
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			for (const auto &lane : engine.connection(owner, connection).lanes) {
				check(lane->stopped.load(), "All workers acknowledge stop");
				check(lane->snapshot().sql_commands == 0, "Rejected SQL was never dispatched");
			}
			trace(prefix + "disconnected destroy");
		}
		trace(prefix + "failed connection retry");
		{
			pg::Engine engine(limits);
			const auto connection = engine.connect(owner, bad_service, 8);
			until(engine, [&] {
				for (const auto &lane : engine.connection(owner, connection).lanes) {
					if (lane->diagnostic().empty()) {
						return false;
					}
				}
				return true;
			});
			// Interrupt retry backoff; do not wait for all configured retries to expire.
			trace(prefix + "retry destroy");
		}
		trace(prefix + "complete");
	}
}

} // namespace

int main() {
	const auto service = std::getenv("OMP_PG_TEST_SERVICE");
	const auto bad_service = std::getenv("OMP_PG_TEST_BAD_CONNECT");
	if (!service || !bad_service) {
		std::cout << "SKIP: disposable valid and refused-connection service profiles required\n";
		return 77;
	}
	try {
		const auto count = rounds();
		std::cout << "connection_rounds=" << count << " lanes=8 libpq=" << PQlibVersion()
		          << " pointer_bytes=" << sizeof(void *) << std::endl;
		exercise(service, bad_service, count);
		std::cout << checks << " connection lifecycle checks passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL " << phase << ": " << error.what() << std::endl;
		return 1;
	}
}
