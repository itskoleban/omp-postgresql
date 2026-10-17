// Main-thread Engine benchmark. Use only with an explicitly disposable service.
#include "pg/engine.hpp"
#include "pg/parameters.hpp"
#include "pg/stream.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#ifdef _WIN32
#include <windows.h>

#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
constexpr pg::Owner owner = 902;
constexpr int window = 32, group = 20;
constexpr std::array modes{"individual",
                           "snapshots20",
                           "unnest20",
                           "heterogeneous20",
                           "individual4",
                           "buffered",
                           "streaming",
                           "file",
                           "section1",
                           "section128",
                           "section1024",
                           "file_snapshots20",
                           "section1024_snapshots20"};

struct SqlDirectory {
	std::filesystem::path path =
	    std::filesystem::temp_directory_path() /
	    ("omp-pg-bench-sql-" + std::to_string(pg::Clock::now().time_since_epoch().count()));
	SqlDirectory() {
		std::filesystem::create_directory(path);
	}
	~SqlDirectory() {
		std::error_code ignored;
		std::filesystem::remove_all(path, ignored);
	}
};

void require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

int number(const char *value, int maximum) {
	int result = 0;
	const auto end = value + std::char_traits<char>::length(value);
	const auto parsed = std::from_chars(value, end, result);
	require(parsed.ec == std::errc{} && parsed.ptr == end && result >= 1 && result <= maximum,
	        "Expected decimal rows 1..10000 and repetitions 1..100");
	return result;
}

struct Resources {
	std::uint64_t cpu_us, peak_kib;
};

Resources resources() {
#ifdef _WIN32
	FILETIME created{}, exited{}, kernel{}, user{};
	PROCESS_MEMORY_COUNTERS memory{};
	require(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) &&
	            GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)),
	        "Process resource accounting failed");
	auto ticks = [](FILETIME value) {
		return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
	};
	return {(ticks(kernel) + ticks(user)) / 10, memory.PeakWorkingSetSize / 1024};
#else
	rusage usage{};
	require(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage failed");
	const auto cpu =
	    static_cast<std::uint64_t>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000 +
	    usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
#ifdef __APPLE__
	return {cpu, static_cast<std::uint64_t>(usage.ru_maxrss) / 1024};
#else
	return {cpu, static_cast<std::uint64_t>(usage.ru_maxrss)};
#endif
#endif
}

template <class F> void until(pg::Engine &engine, F condition) {
	const auto deadline = pg::Clock::now() + std::chrono::seconds(60);
	while (!condition()) {
		engine.poll();
		require(pg::Clock::now() < deadline, "Benchmark deadline exceeded");
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
}

void confirmed(const pg::Job &job) {
	require(job.status.load() == pg::Status::confirmed && job.completed == job.items_status.size(),
	        "Benchmark SQL was not fully confirmed");
}

pg::Result sql(pg::Engine &engine, pg::Id connection, const std::string &text) {
	const auto id = engine.query(owner, connection, text, 0, 0, 0, false);
	auto job = engine.job(owner, id);
	until(engine, [&] {
		return job->delivered;
	});
	confirmed(*job);
	require(job->results.size() == 1, "Expected one setup/verification result");
	auto result = std::move(job->results[0]);
	engine.close_job(owner, id);
	return result;
}

struct Sample {
	std::vector<std::int64_t> latency, queue, execution;
	std::uint64_t sql_count = 0, sql_bytes = 0, parameter_bytes = 0;
	std::int64_t elapsed_us = 0;
	Resources before{}, after{};
};

// Nearest-rank percentiles, including small samples; do not interpolate timings.
std::int64_t percentile(std::vector<std::int64_t> values, double fraction) {
	require(!values.empty(), "Missing benchmark samples");
	std::sort(values.begin(), values.end());
	return values[static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1];
}

Sample measure(pg::Engine &engine, pg::Id connection, const std::string &table, int mode, int rows,
               const std::filesystem::path &sql_root) {
	const bool reading = mode == 5 || mode == 6;
	const auto update =
	    "UPDATE " + table + " SET value=value+1 WHERE id=$1::int RETURNING id,value";
	const auto other = "UPDATE " + table + " SET value=1+value WHERE id=$1::int RETURNING id,value";
	const auto bulk = "UPDATE " + table +
	                  " a SET value=value+1 FROM unnest($1::int[]) u(id) WHERE a.id=u.id "
	                  "RETURNING a.id,a.value";
	const auto select = "SELECT id,value FROM " + table + " ORDER BY id";
	const bool file = mode == 7 || mode == 11;
	const bool section = mode >= 8 && !file;
	if (file || section) {
		std::ofstream source(sql_root / "update.sql", std::ios::binary | std::ios::trunc);
		if (file) {
			source << update;
		} else {
			source << "-- name: update\n" << update << '\n';
			const int count = mode == 8 ? 1 : mode == 9 ? 128 : 1024;
			for (int i = 1; i < count; ++i) {
				source << "-- name: unused_" << i << "\nSELECT " << i << ";\n";
			}
		}
		require(source.good(), "SQL benchmark fixture write failed");
	}
	const auto first_statement =
	    section
	        ? engine.prepare_section(owner, connection, "update.sql", "update", 1)
	        : engine.prepare(owner, connection, file ? "update.sql" : (reading ? select : update),
	                         reading ? 0 : 1, file);
	const auto second_statement = engine.prepare(owner, connection, other, 1);
	const auto bulk_statement = engine.prepare(owner, connection, bulk, 1);
	struct Pending {
		std::shared_ptr<pg::Job> job;
		int first, count, commands, streamed = 0;
	};
	std::vector<Pending> pending;
	pending.reserve(window);
	Sample sample;
	sample.before = resources();
	const auto started = pg::Clock::now();
	const auto deadline = started + std::chrono::seconds(60);
	int next = 1;
	while (next <= rows || !pending.empty()) {
		while (next <= rows && pending.size() < window) {
			const int count = reading ? rows
			                  : mode == 0 || mode == 4 || (mode >= 7 && mode <= 10)
			                      ? 1
			                      : std::min(group, rows - next + 1);
			const int commands = reading || mode == 2 ? 1 : count;
			pg::Id id;
			if (reading) {
				engine.statement(owner, first_statement).add(engine.limits());
				id = mode == 6 ? engine.execute_stream(owner, first_statement, 128, 0, 0, 0, false)
				               : engine.execute(owner, first_statement, 0, 0, 0, false);
				sample.sql_bytes += select.size();
			} else if (mode == 2) {
				std::vector<std::int32_t> values(static_cast<std::size_t>(count));
				std::iota(values.begin(), values.end(), next);
				auto parameter = pg::int_array(values);
				sample.parameter_bytes += parameter.value->size();
				engine.statement(owner, bulk_statement)
				    .bind(1, std::move(parameter), engine.limits());
				engine.statement(owner, bulk_statement).add(engine.limits());
				id = engine.execute(owner, bulk_statement, 0, 0, 0, false);
				sample.sql_bytes += bulk.size();
			} else {
				const auto batch = mode == 3 ? engine.create_batch(owner, connection) : 0;
				for (int item = 0; item < count; ++item) {
					const bool alternate = mode == 3 && item % 2;
					const auto statement = alternate ? second_statement : first_statement;
					auto parameter = pg::integer(next + item);
					sample.parameter_bytes += parameter.value->size();
					engine.statement(owner, statement)
					    .bind(1, std::move(parameter), engine.limits());
					engine.statement(owner, statement).add(engine.limits());
					sample.sql_bytes +=
					    alternate ? other.size() : update.size() + (section ? 2 : 0);
					if (batch) {
						engine.add_batch(owner, batch, statement);
					}
				}
				id = batch ? engine.execute_batch(owner, batch, 0, 0, 0, false)
				           : engine.execute(owner, first_statement, mode == 4 ? (next - 1) % 4 : 0,
				                            0, 0, false);
				if (batch) {
					engine.close_batch(owner, batch);
				}
			}
			sample.sql_count += commands;
			pending.push_back({engine.job(owner, id), next, count, commands});
			next += count;
		}
		engine.poll();
		bool progress = false;
		std::erase_if(pending, [&](Pending &item) {
			auto &job = *item.job;
			if (job.stream && job.stream->next()) {
				for (const auto &row : job.stream->current()->rows) {
					++item.streamed;
					require(row.size() == 2 && row[0] == std::to_string(item.streamed) &&
					            row[1] == "1",
					        "Stream content/order mismatch");
				}
				job.stream->release();
				progress = true;
			}
			if (!job.delivered || (job.stream && job.stream->available())) {
				return false;
			}
			confirmed(job);
			require(job.completed == static_cast<std::size_t>(item.commands), "SQL count mismatch");
			if (job.stream) {
				require(item.streamed == rows, "Incomplete stream");
			} else {
				require(job.results.size() == static_cast<std::size_t>(item.commands),
				        "Result count mismatch");
				std::vector<bool> seen(static_cast<std::size_t>(item.count));
				int returned = 0;
				for (const auto &result : job.results) {
					require(result.affected ==
					            std::to_string(reading || mode == 2 ? item.count : 1),
					        "Affected row count mismatch");
					for (const auto &row : result.rows) {
						require(row.size() == 2 && row[0] && row[1] == "1",
						        "Returned value mismatch");
						const auto index = number(row[0]->c_str(), rows) - item.first;
						require(index >= 0 && index < item.count && !seen[index],
						        "Duplicate/unexpected row");
						seen[index] = true;
						++returned;
					}
				}
				require(returned == item.count, "Missing returned rows");
			}
			sample.latency.push_back(job.delivery_us);
			sample.queue.push_back(job.queue_us);
			sample.execution.push_back(job.duration_us);
			engine.close_job(owner, job.id);
			progress = true;
			return true;
		});
		require(pg::Clock::now() < deadline, "Benchmark workload deadline exceeded");
		if (!progress) {
			std::this_thread::sleep_for(std::chrono::microseconds(100));
		}
	}
	sample.elapsed_us =
	    std::chrono::duration_cast<std::chrono::microseconds>(pg::Clock::now() - started).count();
	sample.after = resources();
	for (auto statement : {first_statement, second_statement, bulk_statement}) {
		engine.close_statement(owner, statement);
	}
	const auto verified =
	    sql(engine, connection, "SELECT count(*) FROM " + table + " WHERE value=1");
	require(verified.rows.size() == 1 && verified.rows[0][0] == std::to_string(rows),
	        "Final table integrity failed");
	return sample;
}

void report(const char *mode, int repetition, int rows, const Sample &sample) {
	std::cout << mode << ',' << repetition << ',' << rows << ',' << sample.sql_count << ','
	          << sample.latency.size() << ',' << sample.sql_bytes << ',' << sample.parameter_bytes
	          << ',' << sample.elapsed_us << ',' << rows * 1000000.0 / sample.elapsed_us << ','
	          << percentile(sample.latency, .5) << ',' << percentile(sample.latency, .95) << ','
	          << percentile(sample.latency, .99) << ',' << percentile(sample.queue, .5) << ','
	          << percentile(sample.queue, .95) << ',' << percentile(sample.queue, .99) << ','
	          << percentile(sample.execution, .5) << ',' << percentile(sample.execution, .95) << ','
	          << percentile(sample.execution, .99) << ','
	          << sample.after.cpu_us - sample.before.cpu_us << ',' << sample.after.peak_kib << '\n';
}
} // namespace

int main(int argc, char **argv) {
	try {
		require(argc <= 3, "Usage: pg_engine_benchmark [rows [repetitions]]");
		const int rows = argc > 1 ? number(argv[1], 10000) : 1000;
		const int repetitions = argc > 2 ? number(argv[2], 100) : 7;
		const auto service = std::getenv("OMP_PG_TEST_SERVICE");
		if (!service) {
			std::cerr << "Set OMP_PG_TEST_SERVICE to an explicitly disposable database service\n";
			return 77;
		}
		SqlDirectory sql_directory;
		pg::Engine engine({}, sql_directory.path);
		const auto connection = engine.connect(owner, service, 4);
		until(engine, [&] {
			return engine.connection(owner, connection).ready() == 4;
		});
		const auto metadata =
		    sql(engine, connection,
		        "SELECT current_setting('server_version'),ssl::text,coalesce(version,'none') "
		        "FROM pg_stat_ssl WHERE pid=pg_backend_pid()");
		require(metadata.rows.size() == 1, "Missing server/TLS metadata");
		std::cout << "# commit=" << PG_BENCH_COMMIT << " source_tree=" << PG_BENCH_TREE
		          << " configuration=" << PG_BENCH_CONFIG << " compiler=" << PG_BENCH_COMPILER
		          << " sanitize=" << PG_BENCH_SANITIZE << " pointer_bits=" << sizeof(void *) * 8
#ifdef _WIN32
		          << " os=Windows"
#elif defined(__linux__)
		          << " os=Linux"
#else
		          << " os=Other"
#endif
		          << " libpq=" << PQlibVersion() << " PostgreSQL=" << *metadata.rows[0][0]
		          << " ssl=" << *metadata.rows[0][1] << " tls=" << *metadata.rows[0][2]
		          << " pool=4 window=" << window << " group=" << group << " stream_rows=128"
		          << " warmup=1 order_seed=902 network=service-defined RTT=unmeasured\n";
		std::cout << "mode,repetition,rows,sql_commands,jobs,sql_text_bytes,parameter_value_bytes,"
		             "total_us,rows_per_second,"
		             "job_p50_us,job_p95_us,job_p99_us,queue_p50_us,queue_p95_us,queue_p99_us,"
		             "execution_p50_us,execution_p95_us,execution_p99_us,process_cpu_us,process_"
		             "peak_kib\n";
		std::cout << std::fixed << std::setprecision(2);
		const auto table =
		    "omp_pg_engine_bench_" + std::to_string(pg::Clock::now().time_since_epoch().count());
		sql(engine, connection,
		    "CREATE TABLE " + table + "(id int PRIMARY KEY,value int NOT NULL)");
		sql(engine, connection,
		    "INSERT INTO " + table + " SELECT i,0 FROM generate_series(1," + std::to_string(rows) +
		        ") i");
		std::mt19937 random(902);
		std::array<int, modes.size()> order{};
		std::iota(order.begin(), order.end(), 0);
		for (int repetition = 0; repetition <= repetitions; ++repetition) {
			std::shuffle(order.begin(), order.end(), random);
			for (int mode : order) {
				sql(engine, connection,
				    "UPDATE " + table + " SET value=" + (mode == 5 || mode == 6 ? "1" : "0"));
				auto sample = measure(engine, connection, table, mode, rows, sql_directory.path);
				if (repetition) {
					report(modes[mode], repetition, rows, sample);
				}
			}
		}
		sql(engine, connection, "DROP TABLE " + table);
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
