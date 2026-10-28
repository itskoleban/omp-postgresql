// Real libpq, test-owned loopback peers, and an independent disposable TLS database.
#include "pg/engine.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr pg::Owner owner = 1002, healthy_owner = 1003;
constexpr int lanes = 4;
unsigned checks = 0;
std::string phase;
std::string_view trace_mode, trace_action;
const auto trace_start = pg::Clock::now();

void trace(const char *event, const char *wait = "none", std::int64_t elapsed_us = 0) {
	const auto now = pg::Clock::now();
	std::cout << "STALL event=" << event << " mode=" << trace_mode << " action=" << trace_action
	          << " wait=" << wait << " monotonic_us="
	          << std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()
	          << " utc_ns=" << std::chrono::duration_cast<std::chrono::nanoseconds>(
	                               std::chrono::system_clock::now().time_since_epoch()).count()
	          << " test_us="
	          << std::chrono::duration_cast<std::chrono::microseconds>(now - trace_start).count()
	          << " elapsed_us=" << elapsed_us << std::endl;
}

void check(bool condition, const char *message) {
	++checks;
	if (!condition) {
		// Print before unwinding: a destructor stall must not hide the failed assertion.
		std::cerr << "FAIL " << phase << ": " << message << std::endl;
		throw std::runtime_error(message);
	}
}

template <class F, class Snapshot> void until(const char *wait, F condition, Snapshot snapshot) {
	const auto start = pg::Clock::now();
	const auto deadline = start + std::chrono::seconds(10);
	trace("wait_begin", wait);
	while (!condition()) {
		if (pg::Clock::now() >= deadline) {
			trace("wait_expired", wait, std::chrono::duration_cast<std::chrono::microseconds>(
			                                  pg::Clock::now() - start).count());
			// Diagnostic failure must not hide the original assertion or extend its budget.
			try { snapshot(); } catch (...) { trace("snapshot_failed", wait); }
			check(false, "Connection stall test deadline");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	trace("wait_end", wait, std::chrono::duration_cast<std::chrono::microseconds>(
	                               pg::Clock::now() - start).count());
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
		trace_mode = mode;
		trace_action = action;
		phase = "stall " + mode + " " + action;
		trace("scenario_begin");
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
		pg::Id healthy = 0, connection = 0;
		std::shared_ptr<pg::Job> healthy_job;
		auto snapshot = [&] {
			trace("snapshot");
			std::cout << "STALL peers mode=" << mode << " action=" << action
			          << " accepted=" << markers(directory, "accepted-")
			          << " ready=" << markers(directory, "ready-")
			          << " closed=" << markers(directory, "closed-") << std::endl;
			for (const auto &[who, handle] : {std::pair{healthy_owner, healthy},
			                                std::pair{owner, connection}}) {
				if (!handle) continue;
				try {
					const auto &c = engine->connection(who, handle);
					std::cout << "STALL connection owner=" << who << " handle=" << handle
					          << " state=" << static_cast<int>(c.state()) << std::endl;
					for (std::size_t index = 0; index < c.lanes.size(); ++index) {
						const auto &lane = c.lanes[index];
						const auto stats = lane->snapshot();
						std::cout << "STALL lane owner=" << who << " index=" << index
						          << " state=" << static_cast<int>(lane->state.load())
						          << " stopped=" << lane->stopped.load()
						          << " attempts=" << stats.connection_attempts
						          << " queued=" << stats.queued_jobs << " running=" << stats.running_jobs
						          << " terminal=" << stats.completed_jobs << " commands=" << stats.sql_commands
						          << " rows=" << stats.rows_received << std::endl;
					}
				} catch (const pg::Failure &) {
					std::cout << "STALL connection owner=" << who << " handle=" << handle
					          << " retired=1" << std::endl;
				}
			}
			if (healthy_job) {
				const auto status = healthy_job->status.load();
				std::cout << "STALL healthy_job id=" << healthy_job->id
				          << " status=" << static_cast<int>(status)
				          << " delivered=" << healthy_job->delivered;
				// Worker-owned result fields are readable only after terminal acquire.
				if (pg::terminal(status)) {
					std::cout << " error=" << static_cast<int>(healthy_job->error)
					          << " completed=" << healthy_job->completed
					          << " results=" << healthy_job->results.size();
				}
				std::cout << std::endl;
			}
		};
		healthy = engine->connect(healthy_owner, healthy_service, 1);
		until("healthy_ready", [&] {
			return engine->connection(healthy_owner, healthy).state() == pg::State::connected;
		}, snapshot);
		engine->poll();
		connection = engine->connect(owner, service, lanes);
		until("peer_ready", [&] {
			return markers(directory, "ready-") >= baseline + lanes;
		}, snapshot);
		check(engine->connection(owner, connection).state() != pg::State::connected,
		      "A protocol stall cannot become ready");
		rejected(*engine, connection);
		// A healthy independent owner must still execute while all four peers stall.
		auto job = healthy_job = engine->job(healthy_owner,
		                       engine->query(healthy_owner, healthy, "SELECT 42", 0, 0, 0, false));
		trace("healthy_submitted");
		until("healthy_terminal", [&] {
			engine->poll();
			return pg::terminal(job->status.load()) && job->delivered;
		}, snapshot);
		snapshot();
		check(job->status.load() == pg::Status::confirmed && job->results.size() == 1 &&
		          job->results[0].rows.size() == 1 && job->results[0].rows[0].size() == 1 &&
		          job->results[0].rows[0][0] == "42",
		      "Healthy owner progresses while other sessions are stalled");
		if (action == "timeout") {
			until("connection_failed", [&] {
				return engine->connection(owner, connection).state() == pg::State::failed;
			}, snapshot);
			// Logical failed can precede the last worker's final retry.
			until("workers_failed", [&] {
				for (const auto &lane : engine->connection(owner, connection).lanes) {
					if (lane->state.load() != pg::State::failed) {
						return false;
					}
				}
				return true;
			}, snapshot);
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
			until("disconnected", [&] {
				return engine->connection(owner, connection).state() == pg::State::disconnected;
			}, snapshot);
			for (const auto &lane : engine->connection(owner, connection).lanes) {
				check(lane->stopped.load(), "Every stalled worker acknowledges shutdown");
			}
		} else if (action == "unload") {
			engine->unload(owner);
			bool gone = false;
			until("owner_retired", [&] {
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
			}, snapshot);
		}
		std::cout << phase << " destroy" << std::endl;
		trace("destroy_begin");
		engine.reset();
		trace("destroy_end", "none", std::chrono::duration_cast<std::chrono::microseconds>(
		                                pg::Clock::now() - stop_start).count());
		check(pg::Clock::now() - stop_start < std::chrono::seconds(10),
		      "Stop must interrupt the 60-second connection deadline");
		const auto expected = baseline + (action == "timeout" ? 2 : 1) * lanes;
		until("peers_closed", [&] {
			return markers(directory, "closed-") == expected;
		}, [&] {
			std::cout << "STALL peers mode=" << mode << " action=" << action
			          << " expected=" << expected << " closed=" << markers(directory, "closed-")
			          << std::endl;
		});
		std::cout << phase << " complete" << std::endl;
		trace("scenario_end");
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
	// The observer watches only this test-owned directory. It never opens PGconn
	// from the main thread or changes connection/SQL decisions.
	const auto directory = std::filesystem::path(root);
	std::ofstream(directory / "stall-active") <<
#ifdef _WIN32
	    _getpid();
	{
		std::ofstream libraries(directory / "stall-runtime.log");
		for (const auto name : {"executable", "libpq.dll", "libssl-3-x64.dll", "libcrypto-3-x64.dll",
		                        "libssl-3.dll", "libcrypto-3.dll", "libstdc++-6.dll",
		                        "libgcc_s_seh-1.dll", "libwinpthread-1.dll"}) {
			wchar_t path[32768];
			auto module = GetModuleHandleA(name);
			const bool executable = std::string_view(name) == "executable";
			auto length = module || executable ? GetModuleFileNameW(executable ? nullptr : module, path, 32768) : 0;
			if (length && length < 32768) {
				const auto value = std::filesystem::path(path).u8string();
				libraries << name << '\t';
				libraries.write(reinterpret_cast<const char *>(value.data()), value.size());
				libraries << '\n';
			}
		}
	}
#else
	    getpid();
#ifdef __linux__
	{
		std::error_code error;
		auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
		if (!error) std::ofstream(directory / "stall-runtime.log") << "executable\t" << executable.string() << '\n';
	}
#endif
#endif
	try {
		std::cout << "stall_lanes=" << lanes << " libpq=" << PQlibVersion()
		          << " pointer_bytes=" << sizeof(void *) << std::endl;
		for (const std::string mode : {"ssl", "tls", "startup"}) {
			exercise(service, std::filesystem::path(root), mode);
		}
		std::cout << checks << " stalled connection checks passed" << std::endl;
		std::ofstream(directory / "stall-finished") << 0;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL " << phase << ": " << error.what() << std::endl;
		std::ofstream(directory / "stall-finished") << 1;
		return 1;
	}
}
