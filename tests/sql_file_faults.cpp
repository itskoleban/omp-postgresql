#include "pg/sql_sections.hpp"
#include "pg/statement.hpp"
#include "../src/sql_reader.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#ifdef __linux__
#include <cerrno>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
void check(bool condition, const char *message) {
	if (!condition) throw std::runtime_error(message);
}

template <class F> void rejects(F operation, const std::string &diagnostic) {
	try {
		operation();
	} catch (const pg::Failure &failure) {
		check(failure.code == pg::Error::file && failure.detail == diagnostic,
		      "Wrong SQL file failure classification or diagnostic");
		return;
	}
	throw std::runtime_error("SQL file failure returned content instead of rejecting it");
}

// Simulate a device/read exception after a known number of bytes. A failure can
// occur after complete blocks, or while the next requested block is being filled.
class FaultBuffer : public std::streambuf {
	std::string bytes_;
	std::size_t fail_after_, position_ = 0;
	std::streamsize xsgetn(char *output, std::streamsize requested) override {
		const auto count = std::min(static_cast<std::size_t>(requested),
		                            std::min(bytes_.size(), fail_after_) - position_);
		std::memcpy(output, bytes_.data() + position_, count);
		position_ += count;
		if (count != static_cast<std::size_t>(requested) && position_ == fail_after_) {
			throw std::ios_base::failure("Injected SQL read failure");
		}
		return static_cast<std::streamsize>(count);
	}
public:
	FaultBuffer(std::string bytes, std::size_t fail_after)
	    : bytes_(std::move(bytes)), fail_after_(fail_after) {}
	std::size_t consumed() const { return position_; }
	void repair() { position_ = 0; fail_after_ = std::numeric_limits<std::size_t>::max(); }
};

void stream_errors() {
	const std::string sql = "INSERT INTO accounts VALUES (1);" + std::string(12288, ' ');
	for (const std::size_t offset : {0, 1, 4095, 4096, 4097, 8192}) {
		FaultBuffer buffer(sql, offset);
		std::istream input(&buffer);
		rejects([&] { pg::detail::read_sql_stream(input, sql.size()); }, "SQL file read failed");
		check(input.bad() && buffer.consumed() == offset,
		      "Injected read failure did not reach the intended byte offset");
		// The previous partial prefix must not appear in a recovered read.
		buffer.repair();
		input.clear();
		check(pg::detail::read_sql_stream(input, sql.size()) == sql,
		      "Recovered stream retained partial SQL or lost bytes");
	}
	for (const auto state : {std::ios::failbit, std::ios::badbit,
	                         std::ios::badbit | std::ios::eofbit}) {
		std::istringstream input(sql);
		input.setstate(state);
		rejects([&] { pg::detail::read_sql_stream(input, sql.size()); }, "SQL file read failed");
	}
	// Successful EOF (including an empty file) is distinct from a read error.
	for (const std::size_t size : {0, 1, 4095, 4096, 4097, 8192}) {
		const std::string bytes = sql.substr(0, size);
		for (const auto budget : {size, std::numeric_limits<std::size_t>::max()}) {
			std::istringstream input(bytes);
			check(pg::detail::read_sql_stream(input, budget) == bytes,
			      "Successful stream EOF changed content");
		}
		if (size) {
			std::istringstream input(bytes);
			rejects([&] { pg::detail::read_sql_stream(input, size - 1); },
			        "SQL file exceeds size limit");
		}
	}
	std::cout << "PASS: six interrupted reads, recovery, stream states and EOF boundaries\n";
}

#ifdef __linux__
struct Files {
	std::filesystem::path root = std::filesystem::temp_directory_path() /
	    ("omp-pg-read-faults-" + std::to_string(getpid()) + "-" +
	     std::to_string(pg::Clock::now().time_since_epoch().count()));
	Files() {
		check(std::filesystem::create_directory(root), "Cannot create SQL fixture");
		std::ofstream file(root / "fault.sql", std::ios::binary);
		file << "-- name: query\nSELECT 42;";
		file.close();
		check(file.good(), "Cannot write SQL fixture");
	}
	~Files() {
		std::error_code ignored;
		std::filesystem::permissions(root, std::filesystem::perms::owner_all, ignored);
		std::filesystem::remove_all(root, ignored);
	}
};

void recovered(const Files &files) {
	const auto text = pg::read_sql_contents(files.root, "fault.sql", 4096);
	check(pg::read_sql(files.root, "fault.sql", 4096) == text &&
	          pg::parse_sql_sections(text).select("query") == "\nSELECT 42;",
	      "Whole-file or section mode did not recover after an OS failure");
}

int permission_errors() {
	if (geteuid() == 0) {
		std::cout << "NOT RUN: root can bypass SQL file permissions\n";
		return 77;
	}
	Files files;
	const auto path = files.root / "fault.sql";
	for (const auto &denied : {path, files.root}) {
		std::filesystem::permissions(denied, std::filesystem::perms::none);
		errno = 0;
		const int descriptor = open(path.c_str(), O_RDONLY);
		const int error = errno;
		if (descriptor >= 0) close(descriptor);
		if (descriptor >= 0 || error != EACCES) {
			std::cout << "NOT RUN: identity/filesystem does not enforce EACCES\n";
			return 77;
		}
		for (int mode = 0; mode != 2; ++mode) {
			rejects([&] {
				if (mode) pg::read_sql(files.root, "fault.sql", 4096);
				else pg::read_sql_contents(files.root, "fault.sql", 4096);
			}, "fault.sql: SQL file permission denied");
		}
		std::filesystem::permissions(denied, std::filesystem::perms::owner_all);
		recovered(files);
	}
	std::cout << "PASS: real file and directory EACCES, both readers, recovery\n";
	return 0;
}

void descriptor_errors() {
	Files files;
	struct Limit {
		rlimit previous{};
		Limit() {
			check(getrlimit(RLIMIT_NOFILE, &previous) == 0, "Cannot read descriptor limit");
			auto reduced = previous;
			reduced.rlim_cur = 0;
			check(setrlimit(RLIMIT_NOFILE, &reduced) == 0, "Cannot reduce descriptor limit");
		}
		~Limit() {
			if (setrlimit(RLIMIT_NOFILE, &previous) != 0) std::terminate();
		}
	};
	for (int mode = 0; mode != 2; ++mode) {
		{
			// Normal-build process only: sanitizer runtimes may need their own FDs.
			Limit limit;
			rejects([&] {
				if (mode) pg::read_sql(files.root, "fault.sql", 4096);
				else pg::read_sql_contents(files.root, "fault.sql", 4096);
			}, "fault.sql: SQL file cannot be opened");
		}
		recovered(files);
	}
	std::cout << "PASS: actual descriptor exhaustion, both readers, restored-limit recovery\n";
}
#endif
} // namespace

int main(int argc, char **argv) {
	try {
		if (argc == 1) { stream_errors(); return 0; }
#ifdef __linux__
		if (argc == 2 && std::string_view(argv[1]) == "--permission") return permission_errors();
		if (argc == 2 && std::string_view(argv[1]) == "--descriptors") { descriptor_errors(); return 0; }
#endif
		throw std::runtime_error("Unknown SQL file fault test mode");
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
