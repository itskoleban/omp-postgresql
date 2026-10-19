#include "pg/config.hpp"
#include "pg/log_file.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
	if (!value) {
		throw std::runtime_error("Diagnostics contract failed at line " +
		                         std::to_string(where.line()));
	}
}
struct Output {
	std::vector<std::string> lines;
	static void sink(void *p, std::string_view text) {
		check(text.size() < 512 && text.find_first_of("\r\n") == std::string_view::npos);
		static_cast<Output *>(p)->lines.emplace_back(text);
	}
};
void flush(pg::Diagnostics &d, Output &out) {
	for (int i = 0; i < 6; ++i) {
		d.drain(Output::sink, &out);
	}
}

std::string contents(const std::filesystem::path &path) {
	std::ifstream file(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(file), {}};
}
template <class F>
void eventually(F predicate, std::source_location where = std::source_location::current()) {
	const auto until = pg::Clock::now() + std::chrono::seconds(5);
	while (!predicate()) {
		check(pg::Clock::now() < until, where);
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}

// A try-lock handoff may drop even a single event while the writer changes control state.
// Retry only after an observed loss, never just because an accepted write is delayed.
template <class F>
std::size_t written(pg::Diagnostics &logger, Output &out, F observed,
                    pg::LogRecord record = {pg::LogEvent::worker_failed},
                    std::source_location where = std::source_location::current()) {
	auto drops = logger.dropped();
	std::size_t attempts = 1;
	logger.emit(record);
	flush(logger, out);
	eventually(
	    [&] {
		    if (observed()) {
			    return true;
		    }
		    if (logger.dropped() != drops) {
			    drops = logger.dropped();
			    ++attempts;
			    logger.emit(record);
			    flush(logger, out);
		    }
		    return false;
	    },
	    where);
	return attempts;
}

std::size_t events(std::string_view text) {
	std::size_t count = 0, start = 0;
	while ((start = text.find("worker_failed", start)) != text.npos) {
		++count;
		start += 13;
	}
	return count;
}

void files() {
	struct Fixture {
		std::filesystem::path base =
		    std::filesystem::temp_directory_path() /
		    ("omp-pg-logs-" + std::to_string(pg::Clock::now().time_since_epoch().count()));
		Fixture() {
			std::filesystem::create_directory(base);
		}
		~Fixture() {
			std::error_code ec;
			std::filesystem::remove_all(base, ec);
		}
	} fixture;
	const auto base = fixture.base;
	for (const auto &bad : {"/logs/a.log", "C:/logs/a.log", "//host/logs/a.log", "../logs/a.log",
	                        "logs/../a.log", "logs/a.txt", "logs/sub/a.log", "logs\\a.log",
	                        "logs/.hidden.log", "logs/a..b.log", "logs/CON.log", "logs/Lpt1.log",
	                        "logs/a\n.log", "logs/\xc3\xb1.log", "logs/a.log ", "././logs/a.log"}) {
		bool rejected = false;
		try {
			pg::log_path(pg::LogDestination::file, bad);
		} catch (const pg::Failure &e) {
			rejected = e.code == pg::Error::invalid;
		}
		check(rejected);
	}
	for (const auto &bad :
	     {std::string("logs/a\0.log", 11), "logs/" + std::string(97, 'a') + ".log"}) {
		bool rejected = false;
		try {
			pg::log_path(pg::LogDestination::both, bad);
		} catch (const pg::Failure &) {
			rejected = true;
		}
		check(rejected);
	}
	Output output;
	for (int level = 0; level < 6; ++level) {
		for (const auto destination :
		     {pg::LogDestination::console, pg::LogDestination::file, pg::LogDestination::both}) {
			const auto name =
			    "level" + std::to_string(level) + "-" + std::to_string(int(destination)) + ".log";
			pg::Diagnostics logger(pg::LogLevel::warn, base);
			logger.configure(static_cast<pg::LogLevel>(level), destination,
			                 destination == pg::LogDestination::console ? "" : "./logs/" + name);
			output.lines.clear();
			if (level && destination != pg::LogDestination::console) {
				auto attempts = written(logger, output, [&] {
					return events(contents(base / "logs" / name)) == 1;
				});
				check(logger.sink_failures() == 0 && logger.dropped() == attempts - 1);
				auto ordinary =
				    std::count_if(output.lines.begin(), output.lines.end(), [](const auto &line) {
					    return line.find("worker_failed") != line.npos;
				    });
				check(ordinary == (destination == pg::LogDestination::both ? attempts : 0));
			} else {
				logger.emit({pg::LogEvent::worker_failed});
				flush(logger, output);
				check(output.lines.size() == (level ? 1 : 0));
				check(!std::filesystem::exists(base / "logs" / name));
			}
		}
	}
	const auto default_path = base / "logs/omp-postgresql.log";
	{
		pg::Job secret;
		secret.sql = "SELECT 'secret_password'";
		secret.section = "secret_filename";
		secret.message = "secret_server\nFORGED";
		secret.callback = std::make_unique<pg::CallbackDescriptor>();
		secret.callback->arguments.emplace_back("secret_callback");
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/privacy.log");
		written(
		    logger, output,
		    [&] {
			    return events(contents(base / "logs/privacy.log")) == 1;
		    },
		    pg::job_record(pg::LogEvent::worker_failed, secret));
		auto text = contents(base / "logs/privacy.log");
		check(text.find("secret_") == text.npos && text.find("FORGED") == text.npos &&
		      text.find("SELECT") == text.npos);
	}
	for (int restart = 0; restart < 2; ++restart) {
		pg::Diagnostics logger(pg::LogLevel::warn, base);
		logger.configure(pg::LogLevel::debug, pg::LogDestination::file);
		auto previous = contents(default_path);
		output.lines.clear();
		written(logger, output, [&] {
			return events(contents(default_path)) > events(previous);
		});
		check(contents(default_path).starts_with(previous));
		check(output.lines.empty());
	}
	auto appended = contents(default_path);
	check(events(appended) == 2);
	check(appended.find("Z [omp-pg][ERROR]") != std::string::npos);
	{
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		logger.emit({pg::LogEvent::job_running});
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/new.log");
		check(logger.dropped() == 1); // Queued old records never cross destination boundaries.
		try {
			logger.configure(pg::LogLevel::off, pg::LogDestination::file, "../private.log");
			check(false);
		} catch (const pg::Failure &) {
			check(logger.level() == pg::LogLevel::trace);
		}
		logger.configure(pg::LogLevel::off, pg::LogDestination::console);
		logger.emit({pg::LogEvent::worker_failed});
		output.lines.clear();
		flush(logger, output);
		check(output.lines.empty());
	}
	{
		// Exactly bounded rotation; keep old bytes in one deterministic archive.
		std::ofstream initial(base / "logs/rotate.log", std::ios::binary);
		initial << std::string(pg::LogFileWriter::file_limit - 1, 'a');
		initial.close();
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/rotate.log");
		written(logger, output, [&] {
			// Don't open a Windows reader without FILE_SHARE_DELETE while rotation is pending.
			return std::filesystem::exists(base / "logs/rotate.log.1") &&
			       contents(base / "logs/rotate.log").find("worker_failed") != std::string::npos;
		});
		check(std::filesystem::file_size(base / "logs/rotate.log.1") ==
		      pg::LogFileWriter::file_limit - 1);
		check(std::filesystem::file_size(base / "logs/rotate.log") < pg::LogFileWriter::file_limit);
		logger.configure(pg::LogLevel::off, pg::LogDestination::console);
	}
	{
		std::filesystem::create_directory(base / "logs/broken.log");
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/broken.log");
		eventually([&] {
			return logger.sink_failures() == 1;
		});
		logger.emit({pg::LogEvent::worker_failed});
		output.lines.clear();
		flush(logger, output);
		check(output.lines.size() == 1 &&
		      output.lines[0].find("sink_failures=1") != std::string::npos);
		flush(logger, output);
		check(output.lines.size() == 1); // Rate-limited fallback.
		eventually([&] {
			return logger.dropped() == 1;
		});
		for (int idle = 0; idle < 10; ++idle) {
			flush(logger, output);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		check(logger.dropped() == 1); // Synthetic reports must never cause more drops.
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/recovered.log");
		written(logger, output, [&] {
			return events(contents(base / "logs/recovered.log")) == 1;
		});
	}
	{
		// Hard links cannot turn permitted logging into appending to another file.
		std::ofstream(base / "secret") << "private-secret";
		std::filesystem::create_hard_link(base / "secret", base / "logs/link.log");
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/link.log");
		eventually([&] {
			return logger.sink_failures() == 1;
		});
		logger.emit({pg::LogEvent::worker_failed});
		flush(logger, output);
		check(contents(base / "secret") == "private-secret");
	}
	{
		std::ofstream initial(base / "logs/oversized.log", std::ios::binary);
		initial << std::string(pg::LogFileWriter::file_limit + 1, 'a');
		initial.close();
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/oversized.log");
		eventually([&] {
			return logger.sink_failures() == 1;
		});
		check(std::filesystem::file_size(base / "logs/oversized.log") ==
		          pg::LogFileWriter::file_limit + 1 &&
		      !std::filesystem::exists(base / "logs/oversized.log.1"));
	}
#ifndef _WIN32
	{
		std::filesystem::create_directory(base / "linked-root");
		std::filesystem::create_directory(base / "outside");
		std::filesystem::create_directory_symlink(base / "outside", base / "linked-root/logs");
		pg::Diagnostics logger(pg::LogLevel::trace, base / "linked-root");
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file);
		eventually([&] {
			return logger.sink_failures() == 1;
		});
		check(!std::filesystem::exists(base / "outside/omp-postgresql.log"));
	}
	{
		const auto denied = base / "logs/denied.log";
		std::ofstream(denied) << "keep";
		std::filesystem::permissions(denied, std::filesystem::perms::owner_read);
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/denied.log");
		eventually([&] {
			return logger.sink_failures() == 1;
		});
		check(contents(denied) == "keep");
	}
	{
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/grown.log");
		written(logger, output, [&] {
			return events(contents(base / "logs/grown.log")) == 1;
		});
		std::ofstream grow(base / "logs/grown.log", std::ios::binary | std::ios::app);
		grow << std::string(pg::LogFileWriter::file_limit, 'x');
		grow.close();
		auto size = std::filesystem::file_size(base / "logs/grown.log");
		written(logger, output, [&] {
			return logger.sink_failures() == 1;
		});
		check(std::filesystem::file_size(base / "logs/grown.log") == size);
	}
	{
		std::filesystem::create_symlink(base / "secret", base / "logs/symlink.log");
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/symlink.log");
		eventually([&] {
			return logger.sink_failures() == 1;
		});
	}
	{
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		logger.configure(pg::LogLevel::trace, pg::LogDestination::file, "logs/deleted.log");
		written(logger, output, [&] {
			return events(contents(base / "logs/deleted.log")) == 1;
		});
		std::filesystem::remove(base / "logs/deleted.log");
		written(logger, output, [&] {
			return logger.sink_failures() == 1;
		});
		check(!std::filesystem::exists(base / "logs/deleted.log"));
	}
#endif
	{
		// Synthetic delayed/error completion, with actual secure file opening.
		struct Gate {
			std::atomic<bool> entered{false}, release{false};
		} gate;
		pg::LogFileWriter writer(
		    base,
		    [](void *context) {
			    auto &g = *static_cast<Gate *>(context);
			    g.entered.store(true);
			    while (!g.release.load()) {
				    std::this_thread::yield();
			    }
			    return false; // Pre-write failure only; actual short writes/disk exhaustion remain
			                  // unqualified.
		    },
		    &gate);
		struct Release {
			Gate &gate;
			~Release() {
				gate.release.store(true);
			}
		} release{gate};
		writer.configure("injected.log");
		eventually([&] {
			return writer.enqueue({pg::LogEvent::worker_failed});
		});
		const auto initial_drops = writer.dropped();
		eventually([&] {
			return gate.entered.load();
		});
		for (int i = 0; i < 1000; ++i) {
			writer.enqueue({pg::LogEvent::worker_failed});
		}
		check(writer.dropped() >= 1000 - pg::Diagnostics::capacity);
		writer.configure("reconfigured.log"); // Must not wait behind delayed I/O.
		gate.release.store(true);
		eventually([&] {
			return writer.failures() == 1;
		});
		check(writer.dropped() == initial_drops + 1001);
	}
	{
		pg::LogFileWriter writer(base, [](void *) -> bool {
			throw std::runtime_error("injected failure");
		});
		writer.configure("thread-failure.log");
		std::uint64_t initial_drops = 0;
		eventually([&] {
			if (writer.enqueue({pg::LogEvent::worker_failed})) {
				return true;
			}
			++initial_drops;
			return false;
		});
		eventually([&] {
			return writer.failures() == 1;
		});
		eventually([&] {
			try {
				writer.configure("thread-failure.log");
			} catch (const pg::Failure &e) {
				return e.code == pg::Error::internal;
			}
			return false;
		});
		check(writer.dropped() == initial_drops + 1);
		writer.configure("");
	}
	{
		pg::Diagnostics logger(pg::LogLevel::trace, base);
		std::vector<std::thread> producers;
		for (int lane = 0; lane < 4; ++lane) {
			producers.emplace_back([&] {
				for (int i = 0; i < 20000; ++i) {
					logger.emit({pg::LogEvent::worker_failed});
				}
			});
		}
		for (int i = 0; i < 40; ++i) {
			auto destination = static_cast<pg::LogDestination>(i % 3);
			logger.configure(static_cast<pg::LogLevel>(i % 6), destination,
			                 destination == pg::LogDestination::console ? ""
			                                                            : "logs/transitions.log");
			flush(logger, output);
		}
		for (auto &producer : producers) {
			producer.join();
		}
		logger.configure(pg::LogLevel::off, pg::LogDestination::console);
	}
	std::cout
	    << "File output "
	       "levels/destinations/paths/append/rotation/failure/recovery/links/injection/concurrency "
	       "passed\n";
}
void unit() {
	struct Setting {
		std::optional<std::string> original;
		Setting() {
			if (const char *raw = std::getenv("OMP_PG_LOG_LEVEL")) {
				original = raw;
			}
		}
		static void set(const char *value) {
#ifdef _WIN32
			check(_putenv_s("OMP_PG_LOG_LEVEL", value ? value : "") == 0);
#else
			check(value ? setenv("OMP_PG_LOG_LEVEL", value, 1) == 0
			            : unsetenv("OMP_PG_LOG_LEVEL") == 0);
#endif
		}
		~Setting() {
			set(original ? original->c_str() : nullptr);
		}
	} setting;
	Setting::set(nullptr);
	check(pg::environment_log_level() == pg::LogLevel::warn);
	constexpr std::string_view names[]{"OFF", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
	for (int level = 0; level < 6; ++level) {
		Setting::set(names[level].data());
		check(pg::environment_log_level() == static_cast<pg::LogLevel>(level));
		check(pg::parse_log_level(names[level]) == static_cast<pg::LogLevel>(level));
		pg::Diagnostics d(static_cast<pg::LogLevel>(level));
		std::size_t expected = 0;
		for (int event = 0; event < static_cast<int>(pg::LogEvent::count); ++event) {
			pg::LogRecord r{static_cast<pg::LogEvent>(event)};
			d.emit(r);
			if (d.enabled(pg::record_level(r))) {
				++expected;
			}
		}
		Output out;
		flush(d, out);
		check(out.lines.size() == expected && d.dropped() == 0);
	}
	for (auto invalid : {"", "info", "Warn", "INFO\n", " INFO", "7", "DEBUG ", "INFO\rERROR"}) {
		bool failed = false;
		try {
			pg::parse_log_level(invalid);
		} catch (const pg::Failure &e) {
			failed = e.code == pg::Error::invalid &&
			         std::string(e.what()).find(invalid) == std::string::npos;
			// Empty substrings occur in every message; the input is never interpolated.
			if (std::string_view(invalid).empty()) {
				failed = e.code == pg::Error::invalid;
			}
		}
		check(failed);
	}
	Setting::set("private-invalid-setting\nINFO");
	bool rejected_environment = false;
	try {
		pg::environment_log_level();
	} catch (const pg::Failure &e) {
		rejected_environment = std::string(e.what()).find("private-invalid") == std::string::npos;
	}
	check(rejected_environment);
	Setting::set(nullptr);
	for (auto level : {pg::LogLevel::error, pg::LogLevel::warn, pg::LogLevel::info,
	                   pg::LogLevel::debug, pg::LogLevel::trace}) {
		pg::Diagnostics overflow(level);
		for (std::size_t i = 0; i < pg::Diagnostics::capacity + 1; ++i) {
			overflow.emit({pg::LogEvent::worker_failed});
		}
		Output output;
		overflow.drain(Output::sink, &output);
		check(overflow.dropped() == 1 && output.lines.size() == pg::Diagnostics::drain_budget);
		check(output.lines[0].starts_with(level == pg::LogLevel::error ? "[omp-pg][ERROR]"
		                                                               : "[omp-pg][WARN]"));
	}
	pg::Job secret;
	secret.id = 17;
	secret.owner = 23;
	secret.connection = 29;
	secret.lane = 2;
	secret.sql = "SELECT 'password=secret'";
	secret.message = "private database\nFORGED";
	secret.section = "private-path";
	secret.callback = std::make_unique<pg::CallbackDescriptor>();
	secret.callback->arguments.emplace_back("player-private-payload");
	pg::Diagnostics warnings(pg::LogLevel::warn);
	auto record = pg::job_record(pg::LogEvent::job_terminal, secret);
	record.status = pg::Status::failed;
	record.error = pg::Error::sql;
	record.queue_us = 11;
	record.execution_us = 22;
	warnings.emit(record);
	Output out;
	flush(warnings, out);
	check(out.lines.size() == 1 &&
	      out.lines[0].find("owner=23 conn=29 lane=2 job=17") != std::string::npos);
	for (auto private_text : {"password", "secret", "private", "FORGED", "SELECT", "payload"}) {
		check(out.lines[0].find(private_text) == std::string::npos);
	}
	check(out.lines[0].find("queue_us=11 execution_us=22 completion_wait_us=-1") !=
	      std::string::npos);
	pg::Diagnostics d(pg::LogLevel::trace);
	for (std::size_t i = 0; i < pg::Diagnostics::capacity + 19; ++i) {
		d.emit(record);
	}
	check(d.dropped() == 19);
	Output burst;
	d.drain(Output::sink, &burst);
	check(burst.lines.size() == pg::Diagnostics::drain_budget &&
	      burst.lines[0].find("log_dropped count=19") != std::string::npos);
	flush(d, burst);
	check(burst.lines.size() == pg::Diagnostics::capacity + 1);
	d.emit(record);
	d.drain(
	    [](void *, std::string_view) {
		    throw std::runtime_error("broken output");
	    },
	    nullptr);
	check(d.sink_failures() == 1);
	d.emit(record);
	Output recovered;
	flush(d, recovered);
	check(!recovered.lines.empty());
	// Reentrant/slow sink launches producers and waits for them: never holds queue locks.
	d.emit(record);
	d.drain(
	    [](void *p, std::string_view) {
		    auto &logger = *static_cast<pg::Diagnostics *>(p);
		    logger.drain(
		        [](void *, std::string_view) {
			        check(false);
		        },
		        nullptr);
		    std::thread producer([&] {
			    for (int i = 0; i < 10000; ++i) {
				    logger.emit({pg::LogEvent::job_running});
			    }
		    });
		    producer.join();
	    },
	    &d);
	check(d.dropped() >= 19 + 10000 - pg::Diagnostics::capacity);
	flush(d, recovered);
	// Concurrent flood conservation, including contention drops; no allocations in emit.
	pg::Diagnostics concurrent(pg::LogLevel::trace);
	std::vector<std::thread> workers;
	for (int n = 0; n < 8; ++n) {
		workers.emplace_back([&, n] {
			for (int i = 0; i < 5000; ++i) {
				auto r = record;
				r.job = i;
				r.owner = n;
				concurrent.emit(r);
			}
		});
	}
	for (auto &t : workers) {
		t.join();
	}
	Output flooded;
	flush(concurrent, flooded);
	check(flooded.lines.size() - 1 + concurrent.dropped() == 40000);
	for (int reload = 0; reload < 100; ++reload) {
		pg::Diagnostics cycle(pg::LogLevel::trace);
		std::thread t([&] {
			for (int i = 0; i < 1000; ++i) {
				cycle.emit(record);
			}
		});
		Output drained;
		cycle.drain(Output::sink, &drained);
		t.join();
		flush(cycle, drained);
	}
	std::cout << "Diagnostics filtering/privacy/bounds/flood/sink/reload passed; buffer_bytes="
	          << sizeof(pg::Diagnostics) << " record_bytes=" << sizeof(pg::LogRecord) << '\n';
}
} // namespace
int main() {
	try {
		unit();
		files();
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
