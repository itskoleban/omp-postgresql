#include "pg/engine.hpp"
#include "pg/parameters.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <thread>

namespace {
int checks = 0;
constexpr pg::Owner owner = 1;
constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 16> types{{
    {16, 1000},
    {17, 1001},
    {20, 1016},
    {21, 1005},
    {23, 1007},
    {25, 1009},
    {700, 1021},
    {701, 1022},
    {1082, 1182},
    {1083, 1183},
    {1114, 1115},
    {1184, 1185},
    {1186, 1187},
    {1700, 1231},
    {2950, 2951},
    {3802, 3807},
}};

void check(bool condition, const char *message) {
	++checks;
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Function> void rejects(Function function) {
	try {
		function();
	} catch (const pg::Failure &failure) {
		check(failure.code == pg::Error::invalid,
		      "Invalid type input reports a local validation error");
		return;
	}
	throw std::runtime_error("Invalid type input was accepted");
}

std::vector<std::int32_t> all_bytes() {
	std::vector<std::int32_t> values(256);
	std::iota(values.begin(), values.end(), 0);
	return values;
}

void unit() {
	for (auto oid : {17u, 21u, 25u, 701u, 1082u, 1083u, 1114u, 1184u, 1186u, 1700u, 2950u, 3802u}) {
		auto parameter = pg::text("unchanged-UTF8-\xcf\x80", oid);
		check(parameter.oid == oid && parameter.value == "unchanged-UTF8-\xcf\x80",
		      "Text transport preserves UTF-8 and the explicit scalar type");
		rejects([&] {
			pg::text(std::string("x\0y", 3), oid);
		});
	}
	rejects([] {
		pg::text("1", 0);
	});
	rejects([] {
		pg::text("1", 23);
	});
	rejects([] {
		pg::text("\xc0\xaf", 1700);
	});
	for (auto [scalar, array] : types) {
		check(pg::null(scalar).oid == scalar && !pg::null(scalar).value, "Typed scalar NULL");
		check(pg::null(array).oid == array && !pg::null(array).value, "Typed array NULL");
		auto empty = pg::typed_array({}, scalar);
		check(empty.oid == array && empty.value == "{}", "Typed empty array has its own array OID");
	}
	rejects([] {
		pg::null(0);
	});
	rejects([] {
		pg::typed_array({}, 1007);
	});

	auto bytes = all_bytes();
	auto binary = pg::byte_array(bytes);
	check(binary.oid == 17 && binary.value->starts_with("\\x00010203") &&
	          binary.value->ends_with("fcfdfeff") && binary.value->size() == 514,
	      "BYTEA hex covers every byte without embedded NUL");
	check(pg::decode_bytea(*binary.value) == bytes, "BYTEA round-trip preserves every byte");
	check(pg::byte_array({}).value == "\\x" && pg::decode_bytea("\\x").empty(), "Empty BYTEA");
	check(pg::decode_bytea("\\xAbCd") == std::vector<std::int32_t>{171, 205},
	      "Uppercase BYTEA hex");
	rejects([] {
		std::array<std::int32_t, 1> bytes{-1};
		pg::byte_array(bytes);
	});
	rejects([] {
		std::array<std::int32_t, 1> bytes{256};
		pg::byte_array(bytes);
	});
	for (auto value : {"", "not hex", "\\y00", "\\x0", "\\xGG", "\\x00 01"}) {
		rejects([&] {
			pg::decode_bytea(value);
		});
	}

	std::vector<std::optional<std::string>> text{"NULL", "", "x\"\\,{}", std::nullopt};
	check(pg::typed_array(text, 25).value == "{\"NULL\",\"\",\"x\\\"\\\\,{}\",NULL}",
	      "Typed arrays distinguish literal NULL, empty text and SQL NULL");
	std::vector<std::optional<std::string>> bigint{"-9223372036854775808", std::nullopt,
	                                               "9223372036854775807"};
	check(pg::typed_array(bigint, 20).value ==
	          "{\"-9223372036854775808\",NULL,\"9223372036854775807\"}",
	      "BIGINT array boundaries are represented without narrowing");
	for (auto value : {"9223372036854775808", "-9223372036854775809", "1x", "+1", ""}) {
		rejects([&] {
			std::vector<std::optional<std::string>> invalid{value};
			pg::typed_array(invalid, 20);
		});
	}
	std::vector<std::optional<std::string>> invalid_utf8{std::string("a\0b", 3)};
	rejects([&] {
		pg::typed_array(invalid_utf8, 25);
	});
}

void ready(pg::Engine &engine, pg::Id connection) {
	auto deadline = pg::Clock::now() + std::chrono::seconds(20);
	while (engine.connection(owner, connection).state() != pg::State::connected) {
		engine.poll();
		if (engine.connection(owner, connection).state() == pg::State::failed ||
		    pg::Clock::now() >= deadline) {
			throw std::runtime_error("Type test connection was not ready");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

std::shared_ptr<pg::Job> execute(pg::Engine &engine, pg::Id connection, const std::string &sql,
                                 const std::vector<pg::Param> &parameters = {},
                                 bool prepared = true) {
	auto statement = engine.prepare(owner, connection, sql, static_cast<int>(parameters.size()),
	                                false, prepared);
	for (std::size_t index = 0; index < parameters.size(); ++index) {
		engine.statement(owner, statement)
		    .bind(static_cast<int>(index + 1), parameters[index], engine.limits());
	}
	engine.statement(owner, statement).add(engine.limits());
	auto id = engine.execute(owner, statement, 0, 0, 0, false);
	engine.close_statement(owner, statement);
	auto job = engine.job(owner, id);
	auto deadline = pg::Clock::now() + std::chrono::seconds(20);
	while (!pg::terminal(job->status.load()) || !job->delivered) {
		engine.poll();
		if (pg::Clock::now() >= deadline) {
			throw std::runtime_error("Type test query did not complete");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return job;
}

std::shared_ptr<pg::Job> confirmed(pg::Engine &engine, pg::Id connection, const std::string &sql,
                                   const std::vector<pg::Param> &parameters = {},
                                   bool prepared = true) {
	auto job = execute(engine, connection, sql, parameters, prepared);
	if (job->status.load() != pg::Status::confirmed) {
		for (const auto &result : job->results) {
			std::cerr << result.sqlstate << ": " << result.message << '\n';
		}
		throw std::runtime_error(job->message);
	}
	++checks;
	return job;
}

void integration(const char *profile) {
	pg::Engine engine;
	auto connection = engine.connect(owner, profile, 1);
	ready(engine, connection);
	for (const auto &sql :
	     {"SET TimeZone='UTC'", "SET DateStyle='ISO,YMD'", "SET IntervalStyle='postgres'",
	      "SET bytea_output='hex'", "SET extra_float_digits=3"}) {
		auto setting = confirmed(engine, connection, sql, {}, false);
		engine.close_job(owner, setting->id);
	}
	std::string numeric = "123456789012345678901234567890.12345678901234567890012345";
	auto scalars =
	    confirmed(engine, connection, "SELECT $1,$2,$3,$4,$5,$6,$7,$8",
	              {pg::text("-32768", 21), pg::text(numeric, 1700),
	               pg::text("1.2345678901234567", 701), pg::text("2024-02-29", 1082),
	               pg::text("01:02:03.123456", 1083), pg::text("2026-10-12 15:40:30.123456", 1114),
	               pg::text("2026-10-12T15:40:30.123456-05:00", 1184),
	               pg::text("2 days 03:04:05.123456", 1186)});
	auto &row = scalars->results[0].rows[0];
	check(row[0] == "-32768" && row[1] == numeric && row[2] == "1.2345678901234567",
	      "SMALLINT, exact NUMERIC and float8 preserve the intended precision");
	check(row[3] == "2024-02-29" && row[4] == "01:02:03.123456" &&
	          row[5] == "2026-10-12 15:40:30.123456" && row[6] == "2026-10-12 20:40:30.123456+00" &&
	          row[7] == "2 days 03:04:05.123456",
	      "Date/time types use PostgreSQL parsing and the explicit session timezone");
	engine.close_job(owner, scalars->id);

	std::vector<pg::Param> nulls;
	std::string null_sql = "SELECT ";
	for (auto [scalar, array] : types) {
		for (auto oid : {scalar, array}) {
			if (!nulls.empty()) {
				null_sql += ',';
			}
			nulls.push_back(pg::null(oid));
			null_sql += '$' + std::to_string(nulls.size()) + " IS NULL";
		}
	}
	auto typed_nulls = confirmed(engine, connection, null_sql, nulls);
	for (const auto &value : typed_nulls->results[0].rows[0]) {
		check(value == "t", "Explicit scalar and whole-array NULL reach PostgreSQL as NULL");
	}
	engine.close_job(owner, typed_nulls->id);

	auto bytes = all_bytes();
	auto binary = confirmed(engine, connection, "SELECT $1", {pg::byte_array(bytes)});
	check(pg::decode_bytea(*binary->results[0].rows[0][0]) == bytes,
	      "Real PostgreSQL BYTEA round-trip preserves all 256 byte values");
	engine.close_job(owner, binary->id);
	auto empty_binary = confirmed(engine, connection, "SELECT $1", {pg::byte_array({})});
	check(pg::decode_bytea(*empty_binary->results[0].rows[0][0]).empty(), "Real empty BYTEA");
	engine.close_job(owner, empty_binary->id);

	struct ArrayCase {
		std::uint32_t element_oid;
		std::string input;
		std::string output;
	};
	std::vector<ArrayCase> cases{
	    {16, "true", "t"},
	    {17, "\\x00ff", "\\x00ff"},
	    {20, "9223372036854775807", "9223372036854775807"},
	    {21, "-32768", "-32768"},
	    {23, "-2147483648", "-2147483648"},
	    {25, "NULL", "NULL"},
	    {700, "1.25", "1.25"},
	    {701, "1.2345678901234567", "1.2345678901234567"},
	    {1082, "2024-02-29", "2024-02-29"},
	    {1083, "01:02:03.123456", "01:02:03.123456"},
	    {1114, "2026-10-12 15:40:30.123456", "2026-10-12 15:40:30.123456"},
	    {1184, "2026-10-12T15:40:30.123456-05:00", "2026-10-12 20:40:30.123456+00"},
	    {1186, "2 days 03:04:05.123456", "2 days 03:04:05.123456"},
	    {1700, numeric, numeric},
	    {2950, "00112233-4455-6677-8899-aabbccddeeff", "00112233-4455-6677-8899-aabbccddeeff"},
	    {3802, "{\"value\":1}", "{\"value\": 1}"},
	};
	for (const auto &test : cases) {
		std::vector<std::optional<std::string>> values{test.input, std::nullopt, test.input};
		auto parameter = pg::typed_array(values, test.element_oid);
		auto array = confirmed(engine, connection,
		                       "SELECT pg_typeof($1)::oid::text,cardinality($1)::text,"
		                       "($1)[2] IS NULL,($1)[1] IS NOT DISTINCT FROM ($1)[3],($1)[1]::text",
		                       {parameter});
		const auto &values_row = array->results[0].rows[0];
		check(values_row[0] == std::to_string(parameter.oid) && values_row[1] == "3" &&
		          values_row[2] == "t" && values_row[3] == "t" && values_row[4] == test.output,
		      "Typed arrays preserve their SQL type, values and individual NULL elements");
		engine.close_job(owner, array->id);
		auto empty_array = confirmed(engine, connection, "SELECT cardinality($1)",
		                             {pg::typed_array({}, test.element_oid)});
		check(empty_array->results[0].rows[0][0] == "0", "Real typed empty array");
		engine.close_job(owner, empty_array->id);
	}

	std::vector<std::optional<std::string>> text{"NULL", "", std::nullopt,
	                                             "quote\" slash\\ braces{}, comma, UTF8-\xcf\x80"};
	auto strings = confirmed(engine, connection, "SELECT ($1)[1],($1)[2],($1)[3] IS NULL,($1)[4]",
	                         {pg::typed_array(text, 25)});
	const auto &text_row = strings->results[0].rows[0];
	check(text_row[0] == "NULL" && text_row[1] == "" && text_row[2] == "t" &&
	          text_row[3] == text[3],
	      "Real arrays distinguish NULL, empty text and escaped UTF-8 payloads");
	engine.close_job(owner, strings->id);
	for (auto parameter :
	     {pg::text("40000", 21), pg::text("not-numeric", 1700), pg::text("2026-02-29", 1082)}) {
		auto failed = execute(engine, connection, "SELECT $1", {parameter});
		check(failed->status.load() == pg::Status::failed && failed->error == pg::Error::sql &&
		          failed->completed == 0 && failed->results[0].sqlstate.starts_with("22"),
		      "PostgreSQL validates textual scalar formats without silently narrowing them");
		engine.close_job(owner, failed->id);
	}
	std::vector<std::optional<std::string>> bad_date{"2026-02-29", std::nullopt};
	auto invalid_array =
	    execute(engine, connection, "SELECT $1", {pg::typed_array(bad_date, 1082)});
	check(invalid_array->status.load() == pg::Status::failed &&
	          invalid_array->results[0].sqlstate.starts_with("22"),
	      "PostgreSQL validates non-BIGINT typed array element formats");
	engine.close_job(owner, invalid_array->id);
	engine.disconnect(owner, connection);
}
} // namespace

int main(int argc, char **argv) {
	try {
		if (argc > 1 && std::string_view(argv[1]) == "--integration") {
			auto profile = std::getenv("OMP_PG_TEST_SERVICE");
			if (!profile) {
				std::cout << "SKIP: set OMP_PG_TEST_SERVICE\n";
				return 77;
			}
			integration(profile);
		} else {
			unit();
		}
		std::cout << checks << " type checks passed\n";
		return 0;
	} catch (const std::exception &failure) {
		std::cerr << "FAIL: " << failure.what() << '\n';
		return 1;
	}
}
