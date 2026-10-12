#include "pg/component.hpp"
#include "pg/parameters.hpp"
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>

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

Result &result(Job &j, cell index) {
	if (index < 0 || static_cast<std::size_t>(index) >= j.results.size()) {
		throw Failure(Error::invalid, "Result index out of range");
	}
	return j.results[index];
}

const std::optional<std::string> &value(Result &r, cell row, cell column) {
	if (row < 0 || column < 0 || static_cast<std::size_t>(row) >= r.rows.size() ||
	    static_cast<std::size_t>(column) >= r.columns.size()) {
		throw Failure(Error::invalid, "Row or column out of range");
	}
	return r.rows[row][column];
}

// clang-format off
#define PG_NATIVES(X) \
	X(pg_connect, 2) \
	X(pg_connection_close, 1) \
	X(pg_connection_state, 1) \
	X(pg_connection_ready, 1) \
	X(pg_connection_message, 4) \
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
	X(pg_add, 1) \
	X(pg_execute, 5) \
	X(pg_statement_close, 1) \
	X(pg_begin, 4) \
	X(pg_commit, 3) \
	X(pg_rollback, 3) \
	X(pg_job_status, 1) \
	X(pg_job_close, 1) \
	X(pg_cancel, 1) \
	X(pg_job_completed, 1) \
	X(pg_job_error, 1) \
	X(pg_job_message, 3) \
	X(pg_job_duration, 3) \
	X(pg_job_connection, 1) \
	X(pg_result_count, 1) \
	X(pg_result_rows, 2) \
	X(pg_result_columns, 2) \
	X(pg_result_column, 3) \
	X(pg_result_is_null, 4) \
	X(pg_result_string, 6) \
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

cell invoke(Native native, Script &script, const cell *p) {
	auto &e = Component::instance->engine;
	auto owner = script.owner;
	auto &vm = *script.api;
	auto get = [&](cell h) {
		return e.job(owner, h);
	};
	auto r = [&]() -> Result & {
		return result(done(get(p[1])), p[2]);
	};
	auto bind = [&](Param param) {
		e.statement(owner, p[1]).bind(p[2], std::move(param), e.limits());
		return 1;
	};
	switch (native) {
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
	case Native::pg_prepare:
	case Native::pg_prepare_file:
		return e.prepare(owner, p[1], string(vm, p[2], e.limits().sql_bytes), p[3],
		                 native == Native::pg_prepare_file, p[4] != 0);
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
	case Native::pg_add:
		e.statement(owner, p[1]).add(e.limits());
		return 1;
	case Native::pg_execute:
		return e.execute(owner, p[1], p[2], p[3], p[4], p[5] != 0);
	case Native::pg_statement_close:
		e.close_statement(owner, p[1]);
		return 1;
	case Native::pg_begin:
		return e.begin(owner, p[1], p[2], p[3], p[4] != 0);
	case Native::pg_commit:
	case Native::pg_rollback:
		return e.finish(owner, p[1], native == Native::pg_commit, p[2], p[3] != 0);
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
	case Native::pg_job_error:
		return static_cast<cell>(done(get(p[1])).error);
	case Native::pg_job_message:
		return output(vm, p[2], p[3], done(get(p[1])).message);
	case Native::pg_job_duration:
		return output(vm, p[2], p[3], std::to_string(done(get(p[1])).duration_us));
	case Native::pg_job_connection:
		return get(p[1])->connection;
	case Native::pg_result_count:
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
	case Native::pg_result_is_null:
		return !value(r(), p[3], p[4]).has_value();
	case Native::pg_result_string: {
		auto &v = value(r(), p[3], p[4]);
		if (!v) {
			throw Failure(Error::invalid, "SQL NULL is not a string");
		}
		return output(vm, p[5], p[6], *v);
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
		if (!params || params[0] != count * sizeof(cell)) {
			throw Failure(Error::invalid, "Native argument count mismatch");
		}
		if constexpr (native != Native::pg_last_error) {
			script.error.clear();
		}
		return invoke(native, script, params);
	} catch (const Failure &f) {
		script.error = f.detail;
	} catch (...) {
		script.error = "Native allocation or internal error";
	}
	if constexpr (native == Native::pg_job_status || native == Native::pg_connection_state ||
	              native == Native::pg_result_column) {
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
