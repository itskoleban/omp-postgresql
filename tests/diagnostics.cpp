#include "pg/config.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value) {
	if (!value) {
		throw std::runtime_error("Diagnostics contract failed");
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
void unit() {
	constexpr std::string_view names[]{"OFF", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
	for (int level = 0; level < 6; ++level) {
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
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
