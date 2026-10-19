#include "pg/component.hpp"
#include "pg/parameters.hpp"
#include "pg/stream.hpp"
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>

namespace pg {

namespace {

// Validate each AMX address before dereferencing; never retain Pawn memory.
cell *address(IPawnScript &s, cell base, std::size_t offset = 0) {
	auto value = static_cast<std::int64_t>(base) + static_cast<std::int64_t>(offset) * sizeof(cell);
	cell *ptr = nullptr;
	if (value < 0 || value > std::numeric_limits<cell>::max() || value % sizeof(cell) ||
	    s.GetAddr(static_cast<cell>(value), &ptr) || !ptr) {
		throw Failure(Error::invalid, "Invalid Pawn address");
	}
	return ptr;
}

std::string string(IPawnScript &s, cell base, std::size_t max) {
	std::string out;
	bool packed = static_cast<ucell>(*address(s, base)) > 255;
	for (std::size_t i = 0; i <= max; ++i) {
		ucell value = static_cast<ucell>(*address(s, base, packed ? i / sizeof(cell) : i));
		auto c = packed ? (value >> ((sizeof(cell) - 1 - i % sizeof(cell)) * 8)) & 255 : value;
		if (!c) {
			return out;
		}
		if (i == max || c > 255) {
			throw Failure(Error::limit, "Pawn string exceeds limit or is not byte encoded UTF-8");
		}
		out += static_cast<char>(c);
	}
	throw Failure(Error::invalid, "Unterminated Pawn string");
}

cell output(IPawnScript &s, cell base, cell capacity, const std::string &v) {
	if (capacity < 1 || capacity > 2 * 1024 * 1024 ||
	    v.size() >= static_cast<std::size_t>(capacity)) {
		throw Failure(Error::limit, "Output buffer too small; value not truncated");
	}
	// Validate the complete write first so rejected calls do not partially modify output.
	for (std::size_t i = 0; i <= v.size(); ++i) {
		address(s, base, i);
	}
	for (std::size_t i = 0; i < v.size(); ++i) {
		*address(s, base, i) = static_cast<unsigned char>(v[i]);
	}
	*address(s, base, v.size()) = 0;
	return 1;
}

Job &done(const std::shared_ptr<Job> &j) {
	if (!terminal(j->status.load())) {
		throw Failure(Error::not_ready, "Job has not completed");
	}
	return *j;
}

const Result &result(const Job &j, cell index) {
	if (index < 0 || static_cast<std::size_t>(index) >= j.results.size()) {
		throw Failure(Error::invalid, "Result index out of range");
	}
	return j.results[index];
}

const std::optional<std::string> &value(const Result &r, cell row, cell column) {
	if (row < 0 || column < 0 || static_cast<std::size_t>(row) >= r.rows.size() ||
	    static_cast<std::size_t>(column) >= r.columns.size()) {
		throw Failure(Error::invalid, "Row or column out of range");
	}
	return r.rows[row][column];
}

void record_error(Script &script, std::string_view message) noexcept {
	try {
		script.error.assign(message);
	} catch (...) {
		// A diagnostic allocation must not escape the noexcept native boundary.
		script.error.clear();
	}
}

std::size_t column_index(const Result &r, cell column) {
	if (column < 0 || static_cast<std::size_t>(column) >= r.columns.size()) {
		throw Failure(Error::invalid, "Column index out of range");
	}
	return static_cast<std::size_t>(column);
}

// clang-format off
#define PG_NATIVES(X) \
	X(pg_log_config, 3) \
	X(pg_connect, 2) \
	X(pg_connection_close, 1) \
	X(pg_connection_state, 1) \
	X(pg_connection_ready, 1) \
	X(pg_connection_message, 4) \
	X(pg_lane_state, 2) \
	X(pg_lane_pending, 2) \
	X(pg_lane_active, 2) \
	X(pg_lane_stat, 5) \
	X(pg_query, 6) \
	X(pg_prepare, 4) \
	X(pg_prepare_file, 4) \
	X(pg_bind_int, 3) \
	X(pg_bind_int64, 3) \
	X(pg_bind_float, 3) \
	X(pg_bind_bool, 3) \
	X(pg_bind_null, 3) \
	X(pg_bind_string, 4) \
	X(pg_bind_int_array, 4) \
	X(pg_bind_string_array, 5) \
	X(pg_bind_bytes, 4) \
	X(pg_bind_array, 7) \
	X(pg_add, 1) \
	X(pg_execute, 5) \
	X(pg_statement_close, 1) \
	X(pg_batch_create, 1) \
	X(pg_batch_add, 2) \
	X(pg_batch_execute, 5) \
	X(pg_batch_close, 1) \
	X(pg_stream_execute, 6) \
	X(pg_stream_next, 1) \
	X(pg_stream_release, 1) \
	X(pg_begin, 4) \
	X(pg_begin_ex, 6) \
	X(pg_savepoint, 4) \
	X(pg_rollback_to, 4) \
	X(pg_release_savepoint, 4) \
	X(pg_commit, 3) \
	X(pg_rollback, 3) \
	X(pg_job_status, 1) \
	X(pg_job_close, 1) \
	X(pg_cancel, 1) \
	X(pg_job_completed, 1) \
	X(pg_job_size, 1) \
	X(pg_job_item_status, 2) \
	X(pg_job_error_index, 1) \
	X(pg_job_error, 1) \
	X(pg_job_message, 3) \
	X(pg_job_duration, 3) \
	X(pg_job_queue_time, 3) \
	X(pg_job_latency, 3) \
	X(pg_job_connection, 1) \
	X(pg_result_count, 1) \
	X(pg_result_rows, 2) \
	X(pg_result_columns, 2) \
	X(pg_result_column, 3) \
	X(pg_result_column_name, 5) \
	X(pg_result_column_type, 3) \
	X(pg_result_is_null, 4) \
	X(pg_result_string, 6) \
	X(pg_result_bytes, 7) \
	X(pg_result_int, 5) \
	X(pg_result_float, 5) \
	X(pg_result_bool, 5) \
	X(pg_result_affected, 4) \
	X(pg_result_sqlstate, 4) \
	X(pg_result_message, 4) \
	X(pg_last_error, 2)
// clang-format on

enum class Native {
#define ENUM(name, count) name,
	PG_NATIVES(ENUM)
#undef ENUM
};

constexpr bool asynchronous(Native native) {
	switch (native) {
	case Native::pg_query:
	case Native::pg_execute:
	case Native::pg_batch_execute:
	case Native::pg_stream_execute:
	case Native::pg_begin:
	case Native::pg_begin_ex:
	case Native::pg_savepoint:
	case Native::pg_rollback_to:
	case Native::pg_release_savepoint:
	case Native::pg_commit:
	case Native::pg_rollback:
		return true;
	default:
		return false;
	}
}

std::unique_ptr<CallbackDescriptor> callback(IPawnScript &vm, const cell *p, int fixed,
                                             bool notify) {
	if (p[0] == fixed * sizeof(cell)) {
		return {}; // Old compiled AMX: no optional tail exists.
	}
	auto name = string(vm, p[fixed + 1], CallbackDescriptor::max_name);
	auto format = string(vm, p[fixed + 2], CallbackDescriptor::max_arguments);
	auto count = static_cast<std::size_t>(p[0] / sizeof(cell) - fixed - 2);
	if (count != format.size() || (name.empty() && !format.empty())) {
		throw Failure(Error::invalid, "Callback format/argument count mismatch");
	}
	if (name.empty()) {
		return {};
	}
	constexpr auto first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
	if (std::string_view(first).find(name[0]) == std::string_view::npos ||
	    name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789") !=
	        std::string::npos) {
		throw Failure(Error::invalid, "Invalid callback public name");
	}
	auto out = std::make_unique<CallbackDescriptor>();
	out->function = std::move(name);
	out->arguments.reserve(count);
	std::size_t bytes = out->function.size() + 1;
	for (std::size_t i = 0; i < count; ++i) {
		auto base = p[fixed + 3 + i];
		if (format[i] == 's') {
			auto value = string(vm, base, CallbackDescriptor::max_string);
			if (!utf8(value)) {
				throw Failure(Error::invalid, "Invalid callback string UTF-8");
			}
			bytes += value.size() + 1;
			out->arguments.emplace_back(std::move(value));
		} else if (format[i] == 'i' || format[i] == 'f' || format[i] == 'b') {
			// Pawn variadic scalar arguments are addresses, including literal temporaries.
			auto value = *address(vm, base);
			if (format[i] == 'b' && value != 0 && value != 1) {
				throw Failure(Error::invalid, "Callback boolean must be 0 or 1");
			}
			bytes += sizeof(cell);
			out->arguments.emplace_back(value); // Float bits are preserved exactly.
		} else {
			throw Failure(Error::invalid, "Callback format supports only i, f, b, s");
		}
		if (bytes > CallbackDescriptor::max_payload) {
			throw Failure(Error::limit, "Callback payload exceeds 4096 bytes");
		}
	}
	int index;
	if (notify && vm.FindPublic(out->function.c_str(), &index)) {
		throw Failure(Error::invalid, "Callback public not found in owning VM");
	}
	return out;
}

enum class LaneMetric {
	completed,
	confirmed,
	failed,
	uncertain,
	cancelled,
	connection_attempts,
	reconnects,
	sql_commands,
	rows_received,
	duration_us
};

cell invoke(Native native, Script &script, const cell *p) {
	auto &e = Component::instance->engine;
	auto owner = script.owner;
	auto &vm = *script.api;
	auto get = [&](cell h) {
		return e.job(owner, h);
	};
	auto lane = [&](cell connection, cell index) -> Lane & {
		auto &c = e.connection(owner, connection);
		if (index < 0 || static_cast<std::size_t>(index) >= c.lanes.size()) {
			throw Failure(Error::invalid, "Lane index out of range");
		}
		return *c.lanes[index];
	};
	auto r = [&]() -> const Result & {
		auto job = get(p[1]);
		if (job->stream && job->stream->current()) {
			if (p[2] != 0) {
				throw Failure(Error::invalid, "A stream block has only result index 0");
			}
			return *job->stream->current();
		}
		return result(done(job), p[2]);
	};
	auto bind = [&](Param param) {
		e.statement(owner, p[1]).bind(p[2], std::move(param), e.limits());
		return 1;
	};
	switch (native) {
	case Native::pg_log_config:
		e.diagnostics().configure(static_cast<LogLevel>(p[1]), static_cast<LogDestination>(p[2]),
		                          string(vm, p[3], 128));
		return 1;
	case Native::pg_connect:
		return e.connect(owner, string(vm, p[1], 128), p[2]);
	case Native::pg_connection_close:
		e.disconnect(owner, p[1]);
		return 1;
	case Native::pg_connection_state:
		return static_cast<cell>(e.connection(owner, p[1]).state());
	case Native::pg_connection_ready:
		return e.connection(owner, p[1]).ready();
	case Native::pg_connection_message: {
		auto &c = e.connection(owner, p[1]);
		if (p[2] < 0 || static_cast<std::size_t>(p[2]) >= c.lanes.size()) {
			throw Failure(Error::invalid, "Channel out of range");
		}
		return output(vm, p[3], p[4], c.lanes[p[2]]->diagnostic());
	}
	case Native::pg_lane_state:
		return static_cast<cell>(lane(p[1], p[2]).state.load());
	case Native::pg_lane_pending:
		return static_cast<cell>(lane(p[1], p[2]).snapshot().queued_jobs);
	case Native::pg_lane_active:
		return static_cast<cell>(lane(p[1], p[2]).snapshot().running_jobs);
	case Native::pg_lane_stat: {
		auto stats = lane(p[1], p[2]).snapshot();
		std::uint64_t counter;
		switch (static_cast<LaneMetric>(p[3])) {
		case LaneMetric::completed:
			counter = stats.completed_jobs;
			break;
		case LaneMetric::confirmed:
			counter = stats.confirmed_jobs;
			break;
		case LaneMetric::failed:
			counter = stats.failed_jobs;
			break;
		case LaneMetric::uncertain:
			counter = stats.uncertain_jobs;
			break;
		case LaneMetric::cancelled:
			counter = stats.cancelled_jobs;
			break;
		case LaneMetric::connection_attempts:
			counter = stats.connection_attempts;
			break;
		case LaneMetric::reconnects:
			counter = stats.reconnects;
			break;
		case LaneMetric::sql_commands:
			counter = stats.sql_commands;
			break;
		case LaneMetric::rows_received:
			counter = stats.rows_received;
			break;
		case LaneMetric::duration_us:
			counter = stats.duration_us;
			break;
		default:
			throw Failure(Error::invalid, "Unknown lane statistics metric");
		}
		return output(vm, p[4], p[5], std::to_string(counter));
	}
	case Native::pg_query:
		return e.query(owner, p[1], string(vm, p[2], e.limits().sql_bytes), p[3], p[4], p[5],
		               p[6] != 0, callback(vm, p, 6, p[6] != 0));
	case Native::pg_prepare:
		return e.prepare(owner, p[1], string(vm, p[2], e.limits().sql_bytes), p[3], false,
		                 p[4] != 0);
	case Native::pg_prepare_file:
		return e.prepare_file(owner, p[1], string(vm, p[2], e.limits().sql_bytes), p[3], p[4] != 0,
		                      p[0] == 4 * sizeof(cell) ? std::string{} : string(vm, p[5], 64));
	case Native::pg_bind_int:
		return bind(integer(p[3]));
	case Native::pg_bind_int64:
		return bind(integer64(string(vm, p[3], 32)));
	case Native::pg_bind_float:
		return bind(floating(std::bit_cast<float>(p[3])));
	case Native::pg_bind_bool:
		if (p[3] != 0 && p[3] != 1) {
			throw Failure(Error::invalid, "Boolean must be 0 or 1");
		}
		return bind(boolean(p[3] != 0));
	case Native::pg_bind_null:
		return bind(null(static_cast<std::uint32_t>(p[3])));
	case Native::pg_bind_string:
		return bind(
		    text(string(vm, p[3], e.limits().parameter_bytes), static_cast<std::uint32_t>(p[4])));
	case Native::pg_bind_int_array: {
		if (p[4] < 0 || p[4] > 16384) {
			throw Failure(Error::limit, "Array size must be 0..16384");
		}
		std::vector<std::int32_t> v;
		v.reserve(p[4]);
		for (int i = 0; i < p[4]; ++i) {
			v.push_back(*address(vm, p[3], i));
		}
		return bind(int_array(v));
	}
	case Native::pg_bind_string_array: {
		// Fixed-width flat rows deliberately avoid Pawn's version-dependent multidimensional
		// offsets.
		if (p[4] < 0 || p[4] > 16384 || p[5] < 1 ||
		    static_cast<std::int64_t>(p[4]) * p[5] > 262144) {
			throw Failure(Error::limit, "Invalid flat string array dimensions");
		}
		std::vector<std::string> v;
		for (int i = 0; i < p[4]; ++i) {
			auto offset = static_cast<std::int64_t>(p[3]) +
			              static_cast<std::int64_t>(i) * p[5] * sizeof(cell);
			if (offset > std::numeric_limits<cell>::max()) {
				throw Failure(Error::invalid, "Array address overflow");
			}
			v.push_back(string(vm, static_cast<cell>(offset), p[5] - 1));
		}
		return bind(string_array(v));
	}
	case Native::pg_bind_bytes: {
		if (p[4] < 0 || e.limits().parameter_bytes < 2 ||
		    static_cast<std::size_t>(p[4]) > (e.limits().parameter_bytes - 2) / 2) {
			throw Failure(Error::limit, "BYTEA exceeds the encoded parameter limit");
		}
		std::vector<std::int32_t> bytes;
		bytes.reserve(p[4]);
		for (int i = 0; i < p[4]; ++i) {
			bytes.push_back(*address(vm, p[3], i));
		}
		return bind(byte_array(bytes));
	}
	case Native::pg_bind_array: {
		if (p[4] < 0 || p[4] > 16384 || p[5] < 1 ||
		    static_cast<std::int64_t>(p[4]) * p[5] > 262144) {
			throw Failure(Error::limit, "Invalid typed array dimensions");
		}
		std::vector<std::optional<std::string>> values;
		values.reserve(p[4]);
		for (int i = 0; i < p[4]; ++i) {
			auto is_null = *address(vm, p[7], i);
			if (is_null != 0 && is_null != 1) {
				throw Failure(Error::invalid, "Array NULL mask values must be 0 or 1");
			}
			if (is_null) {
				values.emplace_back(std::nullopt);
				continue;
			}
			auto offset = static_cast<std::int64_t>(p[3]) +
			              static_cast<std::int64_t>(i) * p[5] * sizeof(cell);
			if (offset < 0 || offset > std::numeric_limits<cell>::max()) {
				throw Failure(Error::invalid, "Array address overflow");
			}
			values.emplace_back(string(vm, static_cast<cell>(offset), p[5] - 1));
		}
		return bind(typed_array(values, static_cast<std::uint32_t>(p[6])));
	}
	case Native::pg_add:
		e.statement(owner, p[1]).add(e.limits());
		return 1;
	case Native::pg_execute:
		return e.execute(owner, p[1], p[2], p[3], p[4], p[5] != 0, callback(vm, p, 5, p[5] != 0));
	case Native::pg_statement_close:
		e.close_statement(owner, p[1]);
		return 1;
	case Native::pg_batch_create:
		return e.create_batch(owner, p[1]);
	case Native::pg_batch_add:
		e.add_batch(owner, p[1], p[2]);
		return 1;
	case Native::pg_batch_execute:
		return e.execute_batch(owner, p[1], p[2], p[3], p[4], p[5] != 0,
		                       callback(vm, p, 5, p[5] != 0));
	case Native::pg_batch_close:
		e.close_batch(owner, p[1]);
		return 1;
	case Native::pg_stream_execute:
		return e.execute_stream(owner, p[1], p[2], p[3], p[4], p[5], p[6] != 0,
		                        callback(vm, p, 6, p[6] != 0));
	case Native::pg_stream_next: {
		auto job = get(p[1]);
		if (!job->stream) {
			throw Failure(Error::invalid, "Job is not a streaming query");
		}
		return job->stream->next();
	}
	case Native::pg_stream_release: {
		auto job = get(p[1]);
		if (!job->stream) {
			throw Failure(Error::invalid, "Job is not a streaming query");
		}
		if (!job->stream->current()) {
			return 0;
		}
		job->stream->release();
		return 1;
	}
	case Native::pg_begin:
		return e.begin(owner, p[1], p[2], p[3], p[4] != 0, Isolation::server_default, false,
		               callback(vm, p, 4, p[4] != 0));
	case Native::pg_begin_ex:
		if (p[2] < 0 || p[2] > 2) {
			throw Failure(
			    Error::invalid,
			    "Explicit isolation must be READ COMMITTED, REPEATABLE READ, or SERIALIZABLE");
		}
		return e.begin(owner, p[1], p[4], p[5], p[6] != 0, static_cast<Isolation>(p[2]), p[3] != 0,
		               callback(vm, p, 6, p[6] != 0));
	case Native::pg_savepoint:
	case Native::pg_rollback_to:
	case Native::pg_release_savepoint: {
		auto kind = native == Native::pg_savepoint     ? Kind::savepoint
		            : native == Native::pg_rollback_to ? Kind::rollback_to
		                                               : Kind::release_savepoint;
		return e.savepoint(owner, p[1], string(vm, p[2], 63), kind, p[3], p[4] != 0,
		                   callback(vm, p, 4, p[4] != 0));
	}
	case Native::pg_commit:
	case Native::pg_rollback:
		return e.finish(owner, p[1], native == Native::pg_commit, p[2], p[3] != 0,
		                callback(vm, p, 3, p[3] != 0));
	case Native::pg_job_status:
		return static_cast<cell>(get(p[1])->status.load());
	case Native::pg_job_close:
		e.close_job(owner, p[1]);
		return 1;
	case Native::pg_cancel: {
		auto j = get(p[1]);
		if (terminal(j->status.load())) {
			return 0;
		}
		j->cancel.store(true);
		return 1;
	}
	case Native::pg_job_completed:
		return static_cast<cell>(done(get(p[1])).completed);
	case Native::pg_job_size:
		return static_cast<cell>(get(p[1])->items_status.size());
	case Native::pg_job_item_status: {
		auto &j = done(get(p[1]));
		if (p[2] < 0 || static_cast<std::size_t>(p[2]) >= j.items_status.size()) {
			throw Failure(Error::invalid, "Job item index out of range");
		}
		return static_cast<cell>(j.items_status[p[2]]);
	}
	case Native::pg_job_error_index:
		return done(get(p[1])).error_index;
	case Native::pg_job_error:
		return static_cast<cell>(done(get(p[1])).error);
	case Native::pg_job_message:
		return output(vm, p[2], p[3], done(get(p[1])).message);
	case Native::pg_job_duration:
		return output(vm, p[2], p[3], std::to_string(done(get(p[1])).duration_us));
	case Native::pg_job_queue_time:
		return output(vm, p[2], p[3], std::to_string(done(get(p[1])).queue_us));
	case Native::pg_job_latency: {
		auto &job = done(get(p[1]));
		if (!job.delivered) {
			throw Failure(Error::not_ready, "Job completion has not been dispatched yet");
		}
		return output(vm, p[2], p[3], std::to_string(job.delivery_us));
	}
	case Native::pg_job_connection:
		return get(p[1])->connection;
	case Native::pg_result_count:
		if (auto job = get(p[1]); job->stream && job->stream->current()) {
			return 1;
		}
		return static_cast<cell>(done(get(p[1])).results.size());
	case Native::pg_result_rows:
		return static_cast<cell>(r().rows.size());
	case Native::pg_result_columns:
		return static_cast<cell>(r().columns.size());
	case Native::pg_result_column: {
		auto name = string(vm, p[3], 1024);
		auto &cols = r().columns;
		auto it = std::find(cols.begin(), cols.end(), name);
		return it == cols.end() ? -1 : static_cast<cell>(it - cols.begin());
	}
	case Native::pg_result_column_name: {
		auto &selected = r();
		return output(vm, p[4], p[5], selected.columns[column_index(selected, p[3])]);
	}
	case Native::pg_result_column_type: {
		auto &selected = r();
		auto index = column_index(selected, p[3]);
		if (index >= selected.column_types.size()) {
			throw Failure(Error::internal, "Column type metadata is unavailable");
		}
		return static_cast<cell>(selected.column_types[index]);
	}
	case Native::pg_result_is_null:
		return !value(r(), p[3], p[4]).has_value();
	case Native::pg_result_string: {
		auto &v = value(r(), p[3], p[4]);
		if (!v) {
			throw Failure(Error::invalid, "SQL NULL is not a string");
		}
		return output(vm, p[5], p[6], *v);
	}
	case Native::pg_result_bytes: {
		auto &selected = r();
		auto index = column_index(selected, p[4]);
		if (index >= selected.column_types.size() || selected.column_types[index] != 17) {
			throw Failure(Error::invalid, "Binary output requires a BYTEA column");
		}
		auto &encoded = value(selected, p[3], p[4]);
		if (!encoded) {
			throw Failure(Error::invalid, "SQL NULL has no binary value");
		}
		if (encoded->size() < 2 || encoded->compare(0, 2, "\\x") != 0 ||
		    (encoded->size() - 2) % 2) {
			throw Failure(Error::invalid, "BYTEA output must use hexadecimal format");
		}
		auto count = (encoded->size() - 2) / 2;
		if (p[7] < 0 || p[7] > 2 * 1024 * 1024 || count > static_cast<std::size_t>(p[7])) {
			throw Failure(Error::limit, "Binary output buffer is too small");
		}
		auto *written = address(vm, p[6]);
		for (std::size_t i = 0; i < count; ++i) {
			address(vm, p[5], i);
		}
		auto end = static_cast<std::int64_t>(p[5]) +
		           static_cast<std::int64_t>(count) * static_cast<std::int64_t>(sizeof(cell));
		if (count && p[6] >= p[5] && static_cast<std::int64_t>(p[6]) < end) {
			throw Failure(Error::invalid, "Length output overlaps the byte buffer");
		}
		auto bytes = decode_bytea(*encoded);
		for (std::size_t i = 0; i < bytes.size(); ++i) {
			*address(vm, p[5], i) = bytes[i];
		}
		*written = static_cast<cell>(bytes.size());
		return 1;
	}
	case Native::pg_result_int:
	case Native::pg_result_float:
	case Native::pg_result_bool: {
		auto &v = value(r(), p[3], p[4]);
		if (!v) {
			throw Failure(Error::invalid, "SQL NULL has no scalar value");
		}
		cell encoded = 0;
		if (native == Native::pg_result_bool) {
			if (*v != "t" && *v != "f") {
				throw Failure(Error::invalid, "Value is not PostgreSQL bool");
			}
			encoded = *v == "t";
		} else if (native == Native::pg_result_int) {
			auto [end, ec] = std::from_chars(v->data(), v->data() + v->size(), encoded);
			if (ec != std::errc{} || end != v->data() + v->size()) {
				throw Failure(Error::invalid, "Not a signed int32; read BIGINT as string");
			}
		} else {
			float f;
			auto [end, ec] = std::from_chars(v->data(), v->data() + v->size(), f);
			if (ec != std::errc{} || end != v->data() + v->size() || !std::isfinite(f)) {
				throw Failure(Error::invalid, "Value exceeds finite Pawn Float");
			}
			encoded = std::bit_cast<cell>(f);
		}
		*address(vm, p[5]) = encoded;
		return 1;
	}
	case Native::pg_result_affected:
		return output(vm, p[3], p[4], r().affected);
	case Native::pg_result_sqlstate:
		return output(vm, p[3], p[4], r().sqlstate);
	case Native::pg_result_message:
		return output(vm, p[3], p[4], r().message);
	case Native::pg_last_error:
		return output(vm, p[1], p[2], script.error);
	}
	return 0;
}

template <Native native, int count>
cell AMX_NATIVE_CALL call(AMX *amx, const cell *params) noexcept {
	auto *component = Component::instance;
	if (!component) {
		return 0;
	}
	auto it = component->scripts.find(amx);
	if (it == component->scripts.end()) {
		return 0;
	}
	auto &script = it->second;
	try {
		bool valid = params && params[0] == count * sizeof(cell);
		if constexpr (asynchronous(native)) {
			valid = valid ||
			        (params && params[0] >= (count + 2) * sizeof(cell) &&
			         params[0] <= (count + 2 + CallbackDescriptor::max_arguments) * sizeof(cell) &&
			         params[0] % sizeof(cell) == 0);
		} else if constexpr (native == Native::pg_prepare_file) {
			valid = valid || (params && params[0] == 5 * sizeof(cell));
		}
		if (!valid) {
			throw Failure(Error::invalid, "Native argument count mismatch");
		}
		if constexpr (native != Native::pg_last_error) {
			script.error.clear();
		}
		return invoke(native, script, params);
	} catch (const Failure &f) {
		if constexpr (asynchronous(native)) {
			LogRecord record{LogEvent::admission_rejected, script.owner};
			record.error = f.code;
			Component::instance->engine.diagnostics().emit(record);
		}
		record_error(script, f.detail);
	} catch (...) {
		record_error(script, "Native allocation or internal error");
	}
	if constexpr (native == Native::pg_job_status || native == Native::pg_connection_state ||
	              native == Native::pg_result_column || native == Native::pg_job_item_status ||
	              native == Native::pg_job_error_index || native == Native::pg_lane_state) {
		return -1;
	}
	return 0;
}

} // namespace

void register_natives(IPawnScript &script) {
	static const AMX_NATIVE_INFO entries[] = {
#define ENTRY(name, count) {#name, call<Native::name, count>},
	    PG_NATIVES(ENTRY)
#undef ENTRY
	};
	script.Register(entries, static_cast<int>(sizeof(entries) / sizeof(entries[0])));
}

} // namespace pg
