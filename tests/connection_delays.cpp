// Exercise actual Engine and libpq sessions through a delayed, opaque TLS relay.
#include "pg/engine.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;
constexpr pg::Owner owner = 1943;

void check(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <class F> void until(F condition) {
	const auto deadline = pg::Clock::now() + 8s;
	while (!condition()) {
		check(pg::Clock::now() < deadline, "Timed out waiting for Engine state");
		std::this_thread::sleep_for(1ms);
	}
}

void exercise(pg::Engine &engine, const std::string &service, int delay_ms, int lanes) {
	const auto start = pg::Clock::now();
	const auto connection = engine.connect(owner, service, lanes);
	const auto &pool = engine.connection(owner, connection);
	until([&] {
		const auto state = pool.state();
		check(state != pg::State::failed, "A delayed TLS/SCRAM lane failed to connect");
		return state == pg::State::connected;
	});
	const auto elapsed =
	    std::chrono::duration_cast<std::chrono::milliseconds>(pg::Clock::now() - start);
	check(elapsed >= std::chrono::milliseconds(2 * delay_ms),
	      "The startup relay released bytes too soon");
	check(pool.lanes.size() == static_cast<std::size_t>(lanes), "Unexpected lane count");

	std::set<std::string> backends;
	for (int lane = 0; lane < lanes; ++lane) {
		const auto stats = pool.lanes[lane]->snapshot();
		check(stats.connection_attempts == 1 && stats.reconnects == 0,
		      "Each lane must connect on its initial physical attempt");
		const auto id = engine.query(
		    owner, connection, "SELECT ssl, pid, 1 FROM pg_stat_ssl WHERE pid = pg_backend_pid()",
		    lane, 0, 0, false);
		const auto job = engine.job(owner, id);
		until([&] {
			engine.poll();
			return job->delivered;
		});
		check(job->status.load() == pg::Status::confirmed && job->results.size() == 1,
		      "A delayed TLS/SCRAM lane failed to confirm its query");
		const auto &rows = job->results[0].rows;
		check(rows.size() == 1 && rows[0].size() == 3 && rows[0][0] == "t" && rows[0][2] == "1",
		      "Expected one confirmed result over the verified TLS session");
		check(rows[0][1] && backends.insert(*rows[0][1]).second,
		      "Expected a distinct PostgreSQL backend per lane");
		engine.close_job(owner, id);
	}
	engine.disconnect(owner, connection);
	until([&] {
		return pool.state() == pg::State::disconnected;
	});
	engine.poll();
	std::cout << "PASS profile=" << service << " delay_ms_per_phase=" << delay_ms
	          << " lanes=" << lanes << " ready_elapsed_ms=" << elapsed.count() << '\n';
}

} // namespace

int main() {
	if (!std::getenv("OMP_PG_TEST_CONNECTION_DELAYS")) {
		std::cout << "SKIP: run_postgres.py disposable TLS delay fixtures required\n";
		return 77;
	}
	pg::Limits limits;
	limits.connect_ms = 6000;
	limits.reconnect_attempts = 0;
	pg::Engine engine(limits);
	for (int delay_ms : {10, 50, 100, 200, 500, 1000}) {
		for (int lanes : {1, 2}) {
			try {
				exercise(engine, "tls_delay_" + std::to_string(delay_ms), delay_ms, lanes);
			} catch (const std::exception &error) {
				std::cerr << "FAIL delay_ms_per_phase=" << delay_ms << " lanes=" << lanes
				          << " message=" << error.what() << '\n';
				return EXIT_FAILURE;
			}
		}
	}
	try {
		exercise(engine, "tls_delay_fallback", 10, 1);
	} catch (const std::exception &error) {
		std::cerr << "FAIL TLS multihost fallback: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
