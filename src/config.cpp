#include "pg/config.hpp"
#include <charconv>
#include <cstdlib>
#include <string_view>

namespace pg {

Limits environment_limits() {
	Limits l;
	auto read = [](const char *name, auto &field, int maximum, int minimum = 1) {
		const char *raw = std::getenv(name);
		if (!raw) {
			return;
		}
		std::string_view text(raw);
		int n;
		auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
		if (ec != std::errc{} || end != text.data() + text.size() || n < minimum || n > maximum) {
			throw Failure(Error::invalid, std::string("Invalid setting: ") + name);
		}
		field = static_cast<std::remove_reference_t<decltype(field)>>(n);
	};
	read("OMP_PG_MAX_CONNECTIONS", l.connections, 64);
	read("OMP_PG_MAX_LANES", l.lanes, 16);
	read("OMP_PG_MAX_STATEMENTS", l.statements, 1024);
	read("OMP_PG_MAX_JOBS", l.jobs, 4096);
	read("OMP_PG_MAX_BATCH_COUNT", l.batch_count, 65536);
	read("OMP_PG_MAX_BATCH_BYTES", l.batch_bytes, 64 * 1024 * 1024);
	read("OMP_PG_MAX_SQL_BYTES", l.sql_bytes, 4 * 1024 * 1024);
	read("OMP_PG_MAX_PARAMETER_BYTES", l.parameter_bytes, 4 * 1024 * 1024);
	read("OMP_PG_MAX_RESULT_BYTES", l.result_bytes, 64 * 1024 * 1024);
	read("OMP_PG_MAX_RESULT_ROWS", l.result_rows, 1000000);
	read("OMP_PG_CONNECT_MS", l.connect_ms, 120000);
	read("OMP_PG_QUERY_MS", l.query_ms, 3600000);
	read("OMP_PG_TRANSACTION_MS", l.transaction_ms, 3600000);
	read("OMP_PG_RECONNECT_ATTEMPTS", l.reconnect_attempts, 10, 0);
	read("OMP_PG_RECONNECT_BASE_MS", l.reconnect_base_ms, 60000);
	return l;
}

} // namespace pg
