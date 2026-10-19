#include "pg/config.hpp"
#include "pg/engine.hpp"
#include "pg/log_file.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/magic.h>
#include <sys/statfs.h>
#endif
#endif

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
#ifdef _WIN32
	// The writer retains DELETE access for atomic handle-relative rotation.
	// Live readers must share that access; std::ifstream's Windows sharing does not.
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
	                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
	                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return {};
	}
	LARGE_INTEGER size{};
	std::string text;
	if (GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
	    size.QuadPart <= static_cast<LONGLONG>(2 * pg::LogFileWriter::file_limit + 1)) {
		text.resize(static_cast<std::size_t>(size.QuadPart));
		DWORD read = 0;
		if (ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) {
			text.resize(read);
		} else {
			text.clear();
		}
	}
	CloseHandle(file);
	return text;
#else
	std::ifstream file(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(file), {}};
#endif
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

#ifndef _WIN32
// These OS fault cases run as separate CTest processes: resource limits and signal
// disposition must never affect another test or a real database/server process.
struct OsFixture {
	std::filesystem::path base;
	explicit OsFixture(const std::filesystem::path &parent = std::filesystem::temp_directory_path())
	    : base(parent / ("omp-pg-os-logs-" + std::to_string(getpid()) + "-" +
	                     std::to_string(pg::Clock::now().time_since_epoch().count()))) {
		check(std::filesystem::create_directory(base));
		std::filesystem::permissions(base, std::filesystem::perms::owner_all);
		check(std::filesystem::create_directory(base / "logs"));
	}
	~OsFixture() {
		std::error_code ec;
		std::filesystem::remove_all(base, ec);
	}
};

// Exactly one accepted event, accounting for legitimate numeric try-lock drops.
std::size_t enqueue_one(pg::LogFileWriter &writer) {
	std::size_t attempts = 0;
	eventually([&] {
		++attempts;
		return writer.enqueue({pg::LogEvent::worker_failed});
	});
	return attempts;
}

void disabled_after_failure(pg::LogFileWriter &writer, std::uint64_t dropped) {
	eventually([&] {
		return writer.failures() == 1 && writer.unavailable() && writer.dropped() == dropped;
	});
	// A failed destination stays disabled; later events are accounted without an
	// automatic reopen/retry or an unbounded failure-report loop.
	const auto attempts = enqueue_one(writer);
	eventually([&] {
		return writer.dropped() == dropped + attempts;
	});
	check(writer.failures() == 1 && writer.unavailable());
}

void recovered_file(pg::LogFileWriter &writer, const std::filesystem::path &path) {
	auto drops = writer.dropped();
	writer.configure(path.filename().string());
	writer.enqueue({pg::LogEvent::worker_failed});
	eventually([&] {
		if (events(contents(path)) == 1) {
			return true;
		}
		if (writer.dropped() != drops) {
			drops = writer.dropped();
			writer.enqueue({pg::LogEvent::worker_failed});
		}
		return false;
	});
	check(!writer.unavailable() && writer.failures() == 1);
}

int permission_failure() {
	if (geteuid() == 0) {
		std::cout << "SKIP actual EACCES requires an unprivileged effective uid\n";
		return 77;
	}
	OsFixture fixture;
	const auto path = fixture.base / "logs/denied.log";
	std::ofstream(path) << "keep";
	check(chmod(path.c_str(), 0400) == 0);
	// Establish the actual effective OS error, rather than assuming chmod denies
	// the running identity (root and some capabilities can bypass permissions).
	errno = 0;
	const int denied = ::open(path.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
	const int error = errno;
	if (denied >= 0) {
		::close(denied);
	}
	check(denied < 0 && error == EACCES);
	pg::LogFileWriter writer(fixture.base);
	writer.configure("denied.log");
	eventually([&] {
		return writer.failures() == 1;
	});
	const auto attempts = enqueue_one(writer);
	disabled_after_failure(writer, attempts);
	check(contents(path) == "keep");
	check(chmod(path.c_str(), 0600) == 0);
	recovered_file(writer, path);
	check(contents(path).starts_with("keep"));
	std::cout
	    << "Actual EACCES: disabled once, drops accounted, explicit recovery preserves append\n";
	return 0;
}

int short_write() {
	struct Limits {
		rlimit original{};
		struct sigaction previous{};
		bool changed = false;
		Limits() {
			check(getrlimit(RLIMIT_FSIZE, &original) == 0);
			struct sigaction ignored{};
			ignored.sa_handler = SIG_IGN;
			check(sigemptyset(&ignored.sa_mask) == 0);
			check(sigaction(SIGXFSZ, &ignored, &previous) == 0);
		}
		void restore() {
			if (changed) {
				check(setrlimit(RLIMIT_FSIZE, &original) == 0);
				changed = false;
			}
		}
		~Limits() {
			if (changed) {
				setrlimit(RLIMIT_FSIZE, &original);
			}
			sigaction(SIGXFSZ, &previous, nullptr);
		}
	} limits;
	check(limits.original.rlim_max == RLIM_INFINITY || limits.original.rlim_max >= 1);
	OsFixture fixture;
	const auto path = fixture.base / "logs/partial.log";
	rlimit one_byte = limits.original;
	one_byte.rlim_cur = 1;
	check(setrlimit(RLIMIT_FSIZE, &one_byte) == 0);
	limits.changed = true;
	pg::LogFileWriter writer(fixture.base);
	writer.configure("partial.log");
	const auto attempts = enqueue_one(writer);
	disabled_after_failure(writer, attempts);
	// The logger requested a complete record (>1 byte). The kernel wrote one
	// byte, proving a real positive short write rather than a pre-write failure.
	check(std::filesystem::file_size(path) == 1 && contents(path).size() == 1);
	limits.restore();
	recovered_file(writer, path);
	check(contents(path).size() > 1);
	std::cout << "Actual RLIMIT_FSIZE short write: one byte, disabled once, accounted, recovered\n";
	return 0;
}

#ifdef __linux__
int disk_full(const std::filesystem::path &parent) {
	// The caller provisions a small, isolated tmpfs. Never fill a host disk or a
	// shared large /dev/shm, even when a mistaken path is supplied by CI.
	struct stat directory{}, ancestor{};
	check(std::filesystem::symlink_status(parent).type() == std::filesystem::file_type::directory &&
	      stat(parent.c_str(), &directory) == 0 && stat((parent / "..").c_str(), &ancestor) == 0 &&
	      directory.st_uid == geteuid() && directory.st_dev != ancestor.st_dev &&
	      std::filesystem::is_empty(parent));
	struct statfs fs{};
	check(statfs(parent.c_str(), &fs) == 0 && fs.f_type == TMPFS_MAGIC && fs.f_bsize > 0);
	constexpr std::uint64_t maximum = 16 * 1024 * 1024;
	check(fs.f_blocks > 0 && static_cast<std::uint64_t>(fs.f_blocks) <=
	                             maximum / static_cast<std::uint64_t>(fs.f_bsize));
	const char *profile = std::getenv("OMP_PG_TEST_SERVICE");
	check(profile && *profile); // Configured integration qualification must use run_postgres.py.
	OsFixture fixture(std::filesystem::absolute(parent));
	struct WorkingDirectory {
		std::filesystem::path previous = std::filesystem::current_path();
		explicit WorkingDirectory(const std::filesystem::path &path) {
			std::filesystem::current_path(path);
		}
		~WorkingDirectory() {
			std::error_code ec;
			std::filesystem::current_path(previous, ec);
		}
	} cwd(fixture.base);
	// Keep cwd stable until the Engine and both writer/SQL workers have stopped.
	// Diagnostics uses its actual component-relative base, without a test-only sink.
	pg::Engine engine({}, ".", pg::LogLevel::off);
	constexpr pg::Owner owner = 71;
	const auto connection = engine.connect(owner, profile, 1);
	const auto deadline = pg::Clock::now() + std::chrono::seconds(25);
	Output output;
	auto tick = [&] {
		check(pg::Clock::now() < deadline);
		engine.poll();
		flush(engine.diagnostics(), output);
	};
	while (engine.connection(owner, connection).state() != pg::State::connected) {
		tick();
		check(engine.connection(owner, connection).state() != pg::State::failed);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	auto query = [&](const char *sql) {
		const auto id = engine.query(owner, connection, sql, 0, 0, 0, false);
		auto job = engine.job(owner, id);
		while (!pg::terminal(job->status.load()) || !job->delivered) {
			tick();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		tick();
		return job;
	};
	auto baseline = query("SELECT 1/0");
	check(baseline->status == pg::Status::failed && baseline->error == pg::Error::sql &&
	      baseline->results.size() == 1 && baseline->results[0].sqlstate == "22012");
	const auto original_message = baseline->message;
	const auto original_result = baseline->results[0];
	const auto original_index = baseline->error_index;
	const auto original_items = baseline->items_status;
	const auto original_completed = baseline->completed;
	engine.close_job(owner, baseline->id);
	engine.diagnostics().configure(pg::LogLevel::trace, pg::LogDestination::file,
	                               "logs/engine-full.log");
	const auto engine_path = fixture.base / "logs/engine-full.log";
	eventually([&] {
		return std::filesystem::exists(engine_path);
	});
	check(engine.diagnostics().sink_failures() == 0 &&
	      std::filesystem::file_size(engine_path) == 0);
	const auto path = fixture.base / "logs/full.log";
	pg::LogFileWriter writer(fixture.base);
	writer.configure("full.log");
	eventually([&] {
		return std::filesystem::exists(path);
	});
	check(writer.failures() == 0 && std::filesystem::file_size(path) == 0);
	const auto filler_path = fixture.base / "filler";
	struct Descriptor {
		int value;
		~Descriptor() {
			if (value >= 0) {
				::close(value);
			}
		}
	} filler{::open(filler_path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600)};
	check(filler.value >= 0);
	std::array<char, 4096> block{};
	std::uint64_t filled = 0;
	int error = 0;
	for (;;) {
		errno = 0;
		const auto count = ::write(filler.value, block.data(), block.size());
		if (count < 0) {
			error = errno;
			break;
		}
		check(count > 0);
		filled += static_cast<std::uint64_t>(count);
		check(filled <= maximum);
	}
	check(error == ENOSPC && filled > 0);
	const auto attempts = enqueue_one(writer);
	disabled_after_failure(writer, attempts);
	check(std::filesystem::file_size(path) == 0);
	// Actual SQL jobs emit into this Engine's own failed disk sink. Preserve the
	// exact pre-fault SQL diagnostic and retained result instead of merely running
	// SQL beside an unrelated standalone writer.
	auto failed = query("SELECT 1/0");
	eventually([&] {
		flush(engine.diagnostics(), output);
		return engine.diagnostics().sink_failures() == 1;
	});
	check(failed->status == pg::Status::failed && failed->error == pg::Error::sql &&
	      failed->message == original_message && failed->error_index == original_index &&
	      failed->completed == original_completed && failed->items_status == original_items &&
	      failed->results.size() == 1 && failed->results[0].sqlstate == original_result.sqlstate &&
	      failed->results[0].message == original_result.message &&
	      failed->results[0].columns == original_result.columns &&
	      failed->results[0].column_types == original_result.column_types &&
	      failed->results[0].affected == original_result.affected &&
	      failed->results[0].rows == original_result.rows &&
	      std::filesystem::file_size(engine_path) == 0);
	engine.close_job(owner, failed->id);
	auto succeeded = query("SELECT 1");
	check(succeeded->status == pg::Status::confirmed && succeeded->error == pg::Error::none &&
	      succeeded->results.size() == 1 && succeeded->results[0].rows.size() == 1 &&
	      succeeded->results[0].rows[0].size() == 1 && succeeded->results[0].rows[0][0] == "1" &&
	      engine.diagnostics().sink_failures() == 1 &&
	      std::filesystem::file_size(engine_path) == 0);
	engine.close_job(owner, succeeded->id);
	// Permit only the documented numeric failure summary in the console fallback.
	check(output.lines.size() == 1 && output.lines[0].find("sink_failures=1") != std::string::npos);
	check(::close(filler.value) == 0);
	filler.value = -1;
	check(std::filesystem::remove(filler_path));
	recovered_file(writer, path);
	engine.diagnostics().configure(pg::LogLevel::trace, pg::LogDestination::file,
	                               "logs/engine-full.log");
	written(engine.diagnostics(), output, [&] {
		return events(contents(engine_path)) == 1;
	});
	check(engine.diagnostics().sink_failures() == 1);
	std::cout << "Actual tiny-tmpfs ENOSPC: filled_bytes=" << filled
	          << "; disabled once, drops accounted, explicit recovery; same-Engine SQL error/"
	             "SQLSTATE/index/results and subsequent query preserved\n";
	return 0;
}
#endif
#endif

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
			    return false; // Synthetic pre-write failure; OS failures have separate tests.
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
int main(int argc, char **argv) {
	try {
		if (argc != 1) {
#ifndef _WIN32
			if (argc == 2 && std::string_view(argv[1]) == "--permission") {
				return permission_failure();
			}
			if (argc == 2 && std::string_view(argv[1]) == "--short-write") {
				return short_write();
			}
#ifdef __linux__
			if (argc == 3 && std::string_view(argv[1]) == "--enospc") {
				return disk_full(argv[2]);
			}
#endif
#endif
			throw std::runtime_error("Unsupported diagnostics test arguments");
		}
		unit();
		files();
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
