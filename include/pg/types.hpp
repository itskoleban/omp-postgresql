#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pg {

using Id = std::int32_t;
using Owner = std::uint64_t;
using Clock = std::chrono::steady_clock;
enum class State { disconnected, connecting, connected, reconnecting, failed, closing };
enum class Status { queued, running, confirmed, failed, uncertain, cancelled };
enum class ItemStatus { not_executed, confirmed, failed, uncertain, cancelled };
enum class Kind { execute, begin, commit, rollback, savepoint, rollback_to, release_savepoint };
enum class Isolation { server_default = -1, read_committed = 0, repeatable_read, serializable };
enum class Error {
	none,
	invalid,
	limit,
	not_ready,
	sql,
	connection,
	timeout,
	cancelled,
	file,
	transaction,
	internal
};

inline bool terminal(Status s) {
	return s >= Status::confirmed;
}

struct Limits {
	std::size_t connections = 8, lanes = 8, statements = 64, jobs = 128;
	std::size_t sql_bytes = 256 * 1024, parameter_bytes = 256 * 1024;
	std::size_t batch_bytes = 1024 * 1024, batch_count = 1024;
	std::size_t result_bytes = 2 * 1024 * 1024, result_rows = 10000;
	int connect_ms = 10000, query_ms = 30000, transaction_ms = 60000;
	int reconnect_attempts = 3, reconnect_base_ms = 100;
};

struct Param {
	std::uint32_t oid = 0;
	std::optional<std::string> value;
};

using Params = std::vector<Param>;

struct Result {
	std::vector<std::string> columns;
	std::vector<std::uint32_t> column_types;
	std::vector<std::vector<std::optional<std::string>>> rows;
	std::string affected = "0"; // Decimal: never narrow PostgreSQL's row count to a Pawn cell.
	std::string sqlstate, message;
};

struct Operation {
	std::string sql;
	bool file = false, prepared = true;
	Params params;
	std::string section;
};

struct Batch {
	Owner owner;
	Id connection;
	std::vector<Operation> operations;
	std::size_t bytes = 0;
};

class Stream;

struct Job {
	Id id = 0, connection = 0, transaction = 0;
	Owner owner = 0;
	int lane = 0, context = 0;
	Kind kind = Kind::execute;
	Isolation isolation = Isolation::server_default;
	bool read_only = false;
	std::string savepoint;
	std::string sql;
	bool file = false, prepared = true, notify = true;
	std::string section;
	bool delivered = false; // Main-thread bookkeeping only.
	std::vector<Params> batch;
	std::vector<Operation> operations;
	std::vector<ItemStatus> items_status;
	int error_index = -1;
	std::shared_ptr<Stream> stream;
	Clock::time_point accepted_at;
	std::int64_t queue_us = 0;
	std::int64_t delivery_us = 0; // Main thread, set at completion dispatch.
	std::atomic<Status> status{Status::queued};
	std::atomic<bool> cancel{false};
	// Written only by the worker; readable only after acquire-load of terminal status.
	Error error = Error::none;
	std::string message;
	std::vector<Result> results;
	std::size_t completed = 0;
	std::int64_t duration_us = 0;
};

struct Failure : std::exception {
	Error code;
	std::string detail;

	Failure(Error c, std::string d) : code(c), detail(std::move(d)) {}

	const char *what() const noexcept override {
		return detail.c_str();
	}
};

} // namespace pg
