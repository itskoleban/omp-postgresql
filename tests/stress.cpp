// Run only through run_postgres.py or against an explicitly disposable service.
#include "pg/engine.hpp"
#include "pg/parameters.hpp"
#include "pg/stream.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
#include <unordered_set>
#ifdef _WIN32
#include <windows.h>

#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {

constexpr pg::Owner owner = 901;
std::uint64_t checks = 0;
std::string phase;

[[noreturn]] void fail(const std::string &message) {
	// Print before unwinding, including when Engine destruction cannot finish.
	std::cerr << "FAIL " << phase << ": " << message << std::endl;
	throw std::runtime_error(message);
}

void check(bool condition, const char *message) {
	++checks;
	if (!condition) {
		fail(message);
	}
}

unsigned setting(const char *name, unsigned fallback, unsigned maximum, unsigned minimum = 1) {
	const auto value = std::getenv(name);
	if (!value) {
		return fallback;
	}
	unsigned result = 0;
	const auto end = value + std::char_traits<char>::length(value);
	const auto parsed = std::from_chars(value, end, result);
	if (parsed.ec != std::errc{} || parsed.ptr != end || result < minimum || result > maximum) {
		fail(std::string("Invalid ") + name);
	}
	return result;
}

std::uint64_t peak_kib() {
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS counters{};
	if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
		fail("GetProcessMemoryInfo failed");
	}
	return counters.PeakWorkingSetSize / 1024;
#else
	rusage usage{};
	if (getrusage(RUSAGE_SELF, &usage)) {
		fail("getrusage failed");
	}
#ifdef __APPLE__
	return static_cast<std::uint64_t>(usage.ru_maxrss) / 1024;
#else
	return static_cast<std::uint64_t>(usage.ru_maxrss);
#endif
#endif
}

std::pair<std::uint64_t, std::uint64_t> resources() {
#ifdef _WIN32
	FILETIME created{}, exited{}, kernel{}, user{};
	PROCESS_MEMORY_COUNTERS memory{};
	check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) &&
	          GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)),
	      "Resource sampling failed");
	auto ticks = [](FILETIME t) {
		return (std::uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
	};
	return {(ticks(kernel) + ticks(user)) / 10, memory.WorkingSetSize / 1024};
#else
	rusage usage{};
	check(getrusage(RUSAGE_SELF, &usage) == 0, "CPU sampling failed");
	std::uint64_t rss = 0;
#ifdef __linux__
	std::ifstream status("/proc/self/status");
	std::string line;
	while (std::getline(status, line)) {
		if (line.starts_with("VmRSS:")) {
			auto start = line.find_first_of("0123456789");
			check(start != std::string::npos, "Invalid Linux RSS record");
			rss = std::stoull(line.substr(start));
		}
	}
	check(rss != 0, "Current RSS unavailable");
#endif
	return {std::uint64_t(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000 +
	            usage.ru_utime.tv_usec + usage.ru_stime.tv_usec,
	        rss};
#endif
}

template <class F> void until(pg::Engine &engine, F condition) {
	const auto deadline = pg::Clock::now() + std::chrono::seconds(20);
	while (!condition()) {
		engine.poll();
		if (pg::Clock::now() >= deadline) {
			fail("deadline");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

void ready(pg::Engine &engine, pg::Id connection) {
	until(engine, [&] {
		return engine.connection(owner, connection).state() == pg::State::connected;
	});
}

std::shared_ptr<pg::Job> wait(pg::Engine &engine, pg::Id id) {
	auto job = engine.job(owner, id);
	until(engine, [&] {
		return pg::terminal(job->status.load()) && job->delivered;
	});
	return job;
}

std::shared_ptr<pg::Job> success(pg::Engine &engine, pg::Id id) {
	auto job = wait(engine, id);
	check(job->status.load() == pg::Status::confirmed, "Expected confirmed job");
	check(job->completed == job->items_status.size(), "All SQL items confirmed");
	return job;
}

pg::Id query(pg::Engine &engine, pg::Id connection, std::string sql, int lane = 0, pg::Id tx = 0,
             bool notify = false) {
	return engine.query(owner, connection, std::move(sql), lane, tx, lane, notify);
}

void execute(pg::Engine &engine, pg::Id connection, std::string sql) {
	auto job = success(engine, query(engine, connection, std::move(sql)));
	engine.close_job(owner, job->id);
}

template <class F> void rejects(F action, pg::Error expected) {
	try {
		action();
	} catch (const pg::Failure &error) {
		check(error.code == expected, "Expected local rejection reason");
		return;
	}
	check(false, "Expected local rejection");
}

// Periodic mixed work shares the same long-lived Engine as the load waves.
void mixed(pg::Engine &engine, pg::Id connection, const char *service, const std::string &table,
           std::array<std::int64_t, 4> &expected) {
	for (const bool read_only : {true, false}) {
		auto tx =
		    engine.begin(owner, connection, 0, 0, false, pg::Isolation::read_committed, read_only);
		success(engine, tx);
		auto job =
		    success(engine, query(engine, connection,
		                          read_only ? "SELECT n FROM " + table + " WHERE id=0"
		                                    : "UPDATE " + table + " SET n=n+1 WHERE id IN (0,4)",
		                          0, tx));
		engine.close_job(owner, job->id);
		job = success(engine, engine.finish(owner, tx, true, 0, false));
		engine.close_job(owner, job->id);
		engine.close_job(owner, tx);
		if (!read_only) {
			++expected[0];
		}
	}
	auto statement = engine.prepare(owner, connection, "SELECT i FROM generate_series(1,33) i", 0);
	engine.statement(owner, statement).add(engine.limits());
	auto id = engine.execute_stream(owner, statement, 7, 1, 0, 0, false);
	engine.close_statement(owner, statement);
	auto stream = engine.job(owner, id);
	unsigned rows = 0;
	until(engine, [&] {
		if (stream->stream->next()) {
			for (const auto &row : stream->stream->current()->rows) {
				check(row[0] == std::to_string(++rows), "Mixed stream row order/contents");
			}
			stream->stream->release();
		}
		return stream->delivered && !stream->stream->available();
	});
	check(stream->status.load() == pg::Status::confirmed && rows == 33,
	      "Mixed stream completeness");
	engine.close_job(owner, id);
	// Cancellation forces recovery, then destroy this additional physical connection.
	auto side = engine.connect(owner, service, 1);
	ready(engine, side);
	id = query(engine, side, "SELECT pg_sleep(1)");
	auto cancelled = engine.job(owner, id);
	until(engine, [&] {
		return cancelled->status.load() == pg::Status::running;
	});
	cancelled->cancel.store(true);
	wait(engine, id);
	std::cout << "mixed_cancel status=" << static_cast<int>(cancelled->status.load())
	          << " error=" << static_cast<int>(cancelled->error)
	          << " message=" << cancelled->message << std::endl;
	check(cancelled->status.load() != pg::Status::confirmed &&
	          cancelled->error == pg::Error::cancelled,
	      "Mixed cancellation outcome");
	engine.close_job(owner, id);
	ready(engine, side);
	execute(engine, side, "SELECT 42");
	engine.disconnect(owner, side);
	until(engine, [&] {
		return engine.connection(owner, side).state() == pg::State::disconnected;
	});
	engine.poll();
	engine.poll();
}

void load(const char *service, std::mt19937 &random, unsigned rounds, unsigned seconds,
          const std::string &table) {
	phase = "load";
	std::cout << phase << " connecting" << std::endl;
	pg::Limits limits;
	// Exactly one wave fits; leaked handles must prevent the next wave from being admitted.
	limits.jobs = 48;
	limits.statements = 4;
	pg::Engine engine(limits);
	auto connection = engine.connect(owner, service, 4);
	ready(engine, connection);
	execute(engine, connection, "CREATE TABLE " + table + "(id integer PRIMARY KEY, n bigint)");
	execute(engine, connection, "INSERT INTO " + table + " SELECT i,0 FROM generate_series(0,4) i");
	std::array<std::int64_t, 4> expected{};
	std::uint64_t operations = 0;
	// Updating the shared row first gives every SQL operation the same lock order.
	const auto sql = "WITH shared AS (UPDATE " + table +
	                 " SET n=n+1 WHERE id=4 RETURNING n) UPDATE " + table +
	                 " SET n=n+1 WHERE id=$1::integer AND EXISTS(SELECT 1 FROM shared) RETURNING n";
	const auto started = pg::Clock::now();
	const auto deadline = started + std::chrono::seconds(seconds);
	const auto cpu_start = resources().first;
	unsigned round = 0;
	for (; round < rounds || pg::Clock::now() < deadline; ++round) {
		phase = "load round " + std::to_string(round);
		std::cout << phase << std::endl;
		struct Expected {
			pg::Id id;
			std::int64_t first;
			int count;
		};
		std::vector<Expected> pending;
		std::vector<std::weak_ptr<pg::Job>> released;
		// Mix individual queries, immutable snapshots and heterogeneous SQL in FIFO order.
		for (int index = 0; index < 48; ++index) {
			const int lane = static_cast<int>(random() % 4);
			const int count = index % 3 == 0 ? 20 : index % 3 == 1 ? 4 : 1;
			const auto first = expected[lane] + 1;
			pg::Id id;
			if (count == 1) {
				auto direct = sql;
				direct.replace(direct.find("$1::integer"), 11, std::to_string(lane));
				id = query(engine, connection, std::move(direct), lane);
			} else {
				auto statement = engine.prepare(owner, connection, sql, 1);
				if (count == 20) {
					for (int item = 0; item < count; ++item) {
						engine.statement(owner, statement).bind(1, pg::integer(lane), limits);
						engine.statement(owner, statement).add(limits);
					}
					id = engine.execute(owner, statement, lane, 0, lane, false);
				} else {
					auto batch = engine.create_batch(owner, connection);
					for (int item = 0; item < count; ++item) {
						engine.statement(owner, statement).bind(1, pg::integer(lane), limits);
						engine.statement(owner, statement).add(limits);
						engine.add_batch(owner, batch, statement);
						auto other =
						    engine.prepare(owner, connection, "SELECT " + std::to_string(item), 0);
						engine.statement(owner, other).add(limits);
						engine.add_batch(owner, batch, other);
						engine.close_statement(owner, other);
					}
					id = engine.execute_batch(owner, batch, lane, 0, lane, false);
					engine.close_batch(owner, batch);
				}
				engine.close_statement(owner, statement); // Accepted jobs own the snapshots.
			}
			expected[lane] += count;
			operations += count == 4 ? 8 : count;
			pending.push_back({id, first, count});
		}
		std::vector<std::int64_t> latencies;
		for (const auto &item : pending) {
			auto job = success(engine, item.id);
			latencies.push_back(job->delivery_us);
			released.push_back(job);
			check(job->results.size() == static_cast<std::size_t>(item.count == 4 ? 8 : item.count),
			      "Retained result count");
			for (int index = 0; index < item.count; ++index) {
				const auto &result = job->results[index * (item.count == 4 ? 2 : 1)];
				check(result.affected == "1" && result.rows.size() == 1 &&
				          result.rows[0][0] == std::to_string(item.first + index),
				      "Per-lane FIFO and exact affected rows under shared-row contention");
				if (item.count == 4) {
					check(job->results[index * 2 + 1].rows[0][0] == std::to_string(index),
					      "Heterogeneous results retain order");
				}
			}
			engine.close_job(owner, item.id);
		}
		until(engine, [&] {
			return std::all_of(released.begin(), released.end(), [](const auto &job) {
				return job.expired();
			});
		});
		check(released.size() == 48, "Every wave releases all retained jobs and results");
		std::int64_t total = 0;
		for (int lane = 0; lane < 4; ++lane) {
			total += expected[lane];
			auto stats = engine.connection(owner, connection).lanes[lane]->snapshot();
			check(stats.queued_jobs == 0 && stats.running_jobs == 0 &&
			          stats.completed_jobs == stats.confirmed_jobs && stats.failed_jobs == 0 &&
			          stats.cancelled_jobs == 0 && stats.uncertain_jobs == 0,
			      "Quiescent lane counters reconcile");
		}
		auto result =
		    success(engine, query(engine, connection, "SELECT n FROM " + table + " ORDER BY id"));
		for (int lane = 0; lane < 4; ++lane) {
			check(result->results[0].rows[lane][0] == std::to_string(expected[lane]),
			      "Lane row integrity");
		}
		check(result->results[0].rows[4][0] == std::to_string(total),
		      "Shared row has no lost updates");
		engine.close_job(owner, result->id);
		std::sort(latencies.begin(), latencies.end());
		const auto [cpu_us, rss_kib] = resources();
		const auto elapsed_us =
		    std::chrono::duration_cast<std::chrono::microseconds>(pg::Clock::now() - started)
		        .count();
		std::cout << "audit_metric round=" << round << " elapsed_us=" << elapsed_us
		          << " cpu_us=" << cpu_us - cpu_start << " rss_kib=" << rss_kib
		          << " pending=0 errors=0 wave_jobs=48 sql_operations=" << operations
		          << " p50_us=" << latencies[23] << " p95_us=" << latencies[45]
		          << " p99_us=" << latencies[47] << std::endl;
		if (round % 32 == 0) {
			mixed(engine, connection, service, table, expected);
			++operations; // One periodic SQL write updating both owned/shared counters.
		}
		// Alternate bursts and idle recovery periods, deterministically from the seed.
		if (round % 16 == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(20 + random() % 80));
		}
		std::cout << "load_sample round=" << round << " elapsed_ms="
		          << std::chrono::duration_cast<std::chrono::milliseconds>(pg::Clock::now() -
		                                                                   started)
		                 .count()
		          << " peak_rss_kib=" << peak_kib() << std::endl;
	}
	execute(engine, connection, "DROP TABLE " + table);
	std::cout << "load_sql_operations=" << operations << " load_rounds=" << round
	          << " minimum_seconds=" << seconds << " lanes=4 peak_rss_kib=" << peak_kib()
	          << std::endl;
}

void lifecycle(const char *service, std::mt19937 &random, unsigned rounds,
               const std::string &table) {
	pg::Engine observer;
	auto observer_connection = observer.connect(owner, service, 1);
	ready(observer, observer_connection);
	execute(observer, observer_connection, "CREATE TABLE " + table + "(n integer)");
	execute(observer, observer_connection, "INSERT INTO " + table + " VALUES(0)");
	for (unsigned round = 0; round < rounds; ++round) {
		// Five distinct transitions, including destruction without a completion poll.
		const unsigned action = round % 5;
		phase = "lifecycle round " + std::to_string(round) + " action " + std::to_string(action);
		std::cout << phase << " peak_rss_kib=" << peak_kib() << std::endl;
		pg::Limits limits;
		limits.jobs = 32;
		limits.transaction_ms = action == 1 ? 3000 : 15000;
		limits.query_ms = 15000;
		auto engine = std::make_unique<pg::Engine>(limits);
		auto connection = engine->connect(owner, service, 2);
		ready(*engine, connection);
		auto tx = engine->begin(owner, connection, 0, 0, false);
		success(*engine, tx);
		auto write =
		    success(*engine, query(*engine, connection, "UPDATE " + table + " SET n=n+1", 0, tx));
		engine->close_job(owner, write->id);
		std::array<std::shared_ptr<pg::Job>, 2> streams;
		for (int lane = 0; lane < 2; ++lane) {
			auto statement = engine->prepare(
			    owner, connection, "SELECT i,repeat('x',1024) FROM generate_series(1,100000) i", 0);
			engine->statement(owner, statement).add(limits);
			auto id =
			    engine->execute_stream(owner, statement, 8, lane, lane == 0 ? tx : 0, 0, true);
			engine->close_statement(owner, statement);
			streams[lane] = engine->job(owner, id);
			until(*engine, [&] {
				return streams[lane]->stream->available();
			});
			until(*engine, [&] {
				return streams[lane]->stream->next();
			});
			until(*engine, [&] {
				return streams[lane]->stream->available();
			});
			check(streams[lane]->status.load() == pg::Status::running &&
			          streams[lane]->stream->current()->rows.size() == 8,
			      "Running stream retains current and pending blocks");
		}
		std::vector<std::shared_ptr<pg::Job>> pending;
		// Leave exactly the finish slot reserved while both producers are blocked.
		for (int index = 0; index < 28; ++index) {
			const int lane = static_cast<int>(random() % 2);
			auto id = query(*engine, connection, "UPDATE " + table + " SET n=n+1000", lane,
			                lane == 0 ? tx : 0, true);
			pending.push_back(engine->job(owner, id));
			if (random() % 2) {
				pending.back()->cancel.store(true);
			}
		}
		rejects(
		    [&] {
			    query(*engine, connection, "SELECT 1", 1);
		    },
		    pg::Error::limit);
		auto finish = engine->finish(owner, tx, true, 0, true);
		pending.push_back(engine->job(owner, finish));
		rejects(
		    [&] {
			    engine->finish(owner, tx, true, 0, false);
		    },
		    pg::Error::transaction);
		if (action == 0) {
			for (const auto &stream : streams) {
				stream->cancel.store(true);
			}
		} else if (action == 1) {
			// Transaction expiry must free its blocked producer; the other lane is cancelled.
			wait(*engine, streams[0]->id);
			check(streams[0]->error == pg::Error::timeout,
			      "Transaction deadline reached under backpressure");
			streams[1]->cancel.store(true);
		} else if (action == 2) {
			engine->disconnect(owner, connection);
			rejects(
			    [&] {
				    query(*engine, connection, "SELECT 1", 1);
			    },
			    pg::Error::not_ready);
		} else if (action == 3) {
			engine->unload(owner);
		} else {
			engine.reset();
		}
		if (engine) {
			const auto deadline = pg::Clock::now() + std::chrono::seconds(20);
			bool settled = false;
			std::unordered_set<pg::Id> callbacks;
			while (!settled) {
				auto dispatch = engine->poll();
				if (action == 3) {
					check(dispatch.jobs.empty() && dispatch.streams.empty(),
					      "No job or row callback after unload");
				}
				for (const auto &job : dispatch.jobs) {
					check(callbacks.insert(job->id).second, "Completion callback at most once");
				}
				// Unloaded owners can be reaped after terminal publication, before the
				// completion ID is drained. They have no delivery obligation to a dead VM.
				auto completed = [action](const auto &job) {
					return pg::terminal(job->status.load()) && (action == 3 || job->delivered);
				};
				settled = std::all_of(pending.begin(), pending.end(), completed) &&
				          std::all_of(streams.begin(), streams.end(), completed);
				if (pg::Clock::now() >= deadline) {
					fail("teardown deadline");
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			if (action == 0 || action == 2) {
				check(callbacks.size() == pending.size() + streams.size(),
				      "Every retained completion dispatched exactly once");
			}
			if (action < 2) {
				ready(*engine, connection);
				check(engine->connection(owner, connection).reservations ==
				          std::vector<pg::Id>({0, 0}),
				      "Terminal finish releases transaction reservation");
				for (const auto &job : pending) {
					engine->close_job(owner, job->id);
				}
				for (const auto &job : streams) {
					engine->close_job(owner, job->id);
				}
				engine->close_job(owner, tx);
				for (int lane = 0; lane < 2; ++lane) {
					auto job = success(*engine, query(*engine, connection, "SELECT 73", lane));
					engine->close_job(owner, job->id);
				}
			} else if (action == 3) {
				auto dispatch = engine->poll(); // Reap any owner jobs that just became terminal.
				check(dispatch.jobs.empty() && dispatch.streams.empty(),
				      "Reaping an unloaded owner stays silent");
				rejects(
				    [&] {
					    engine->job(owner, finish);
				    },
				    pg::Error::invalid);
			}
		}
		for (const auto &job : pending) {
			check((job->status.load() == pg::Status::failed ||
			       job->status.load() == pg::Status::cancelled) &&
			          job->completed == 0 && job->results.empty(),
			      "Queued write/COMMIT never executes or replays");
			check(std::all_of(job->items_status.begin(), job->items_status.end(),
			                  [](auto status) {
				                  return status == pg::ItemStatus::not_executed ||
				                         status == pg::ItemStatus::cancelled;
			                  }),
			      "Never-dispatched items cannot be confirmed or uncertain");
		}
		std::vector<std::weak_ptr<const pg::Result>> blocks;
		std::vector<std::weak_ptr<pg::Job>> jobs;
		for (const auto &stream : streams) {
			check(stream->status.load() == pg::Status::uncertain && stream->completed == 0 &&
			          stream->items_status == std::vector{pg::ItemStatus::uncertain},
			      "Interrupted dispatched stream stays uncertain");
			blocks.emplace_back(stream->stream->current());
			stream->stream->release();
			check(blocks.back().expired(), "Consumer release frees current block");
			check(stream->stream->next() && stream->stream->current()->rows.size() == 8 &&
			          !stream->stream->next(),
			      "Only one pending block survives teardown");
			blocks.emplace_back(stream->stream->current());
			jobs.emplace_back(stream);
		}
		for (const auto &job : pending) {
			jobs.emplace_back(job);
		}
		engine.reset();
		pending.clear();
		streams = {};
		check(std::all_of(blocks.begin(), blocks.end(),
		                  [](const auto &block) {
			                  return block.expired();
		                  }),
		      "All retained stream blocks freed");
		check(std::all_of(jobs.begin(), jobs.end(),
		                  [](const auto &job) {
			                  return job.expired();
		                  }),
		      "All queued and streaming jobs freed");
		auto result =
		    success(observer, query(observer, observer_connection, "SELECT n FROM " + table));
		check(result->results[0].rows[0][0] == "0",
		      "Observer proves rollback and no queued write/replay");
		observer.close_job(owner, result->id);
	}
	execute(observer, observer_connection, "DROP TABLE " + table);
}

} // namespace

int main() {
	const auto service = std::getenv("OMP_PG_TEST_SERVICE");
	if (!service) {
		std::cout << "SKIP: set OMP_PG_TEST_SERVICE to a disposable database\n";
		return 77;
	}
	try {
		const auto seed = setting("OMP_PG_STRESS_SEED", 1592594996, 4294967295u);
		const auto rounds = setting("OMP_PG_STRESS_ROUNDS", 10, 100);
		const auto seconds = setting("OMP_PG_STRESS_SECONDS", 0, 86400, 0);
		std::mt19937 random(seed);
		const auto table =
		    "omp_stress_" + std::to_string(pg::Clock::now().time_since_epoch().count());
		std::cout << "seed=" << seed << " rounds=" << rounds << " libpq=" << PQlibVersion()
		          << " pointer_bytes=" << sizeof(void *) << " peak_rss_start_kib=" << peak_kib()
		          << std::endl;
		load(service, random, rounds, seconds, table);
		lifecycle(service, random, rounds, table);
		std::cout << checks << " stress checks passed peak_rss_final_kib=" << peak_kib()
		          << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL " << phase << ": " << error.what() << std::endl;
		return 1;
	}
}
