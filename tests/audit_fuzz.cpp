// Deterministic semantic fuzzing of component input helpers, not the AMX loader.
#include "pg/parameters.hpp"
#include "pg/sql_sections.hpp"
#include "pg/statement.hpp"
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>

namespace {
std::string input;
unsigned iteration = 0;
void check(bool ok, const char *why) {
	if (!ok) {
		throw std::runtime_error(why);
	}
}
template <class F> void rejects(F f, pg::Error expected) {
	try {
		f();
	} catch (const pg::Failure &e) {
		check(e.code == expected, "Wrong rejection category");
		return;
	}
	throw std::runtime_error("Invalid input accepted");
}
unsigned setting(const char *name, unsigned fallback, unsigned maximum) {
	const char *s = std::getenv(name);
	if (!s) {
		return fallback;
	}
	unsigned n = 0;
	auto end = s + std::char_traits<char>::length(s);
	auto p = std::from_chars(s, end, n);
	check(p.ec == std::errc{} && p.ptr == end && n && n <= maximum, "Invalid fuzz setting");
	return n;
}
void scalar(std::string &s, unsigned c) {
	if (c < 128) {
		s += char(c);
	} else if (c < 2048) {
		s += char(192 | (c >> 6));
		s += char(128 | (c & 63));
	} else if (c < 65536) {
		s += char(224 | (c >> 12));
		s += char(128 | ((c >> 6) & 63));
		s += char(128 | (c & 63));
	} else {
		s += char(240 | (c >> 18));
		s += char(128 | ((c >> 12) & 63));
		s += char(128 | ((c >> 6) & 63));
		s += char(128 | (c & 63));
	}
}
void batch_atomicity(std::mt19937 &rng) {
	pg::Limits limits;
	limits.batch_count = 3;
	const std::string first(rng() % 129, 'a');
	const std::string second(1 + rng() % 129, 'b');
	const auto first_cost = 2 * sizeof(pg::Param) + first.size();
	const auto second_cost = 2 * sizeof(pg::Param) + second.size();
	limits.batch_bytes = first_cost + second_cost;
	pg::Statement s{};
	s.bindings.resize(2);
	s.bind(1, pg::text(first), limits);
	// A missing trailing parameter must not commit the partially copied row.
	rejects([&] { s.add(limits); }, pg::Error::invalid);
	check(s.batch.empty() && s.types.empty() && s.bytes == 0 &&
	          s.bindings[0]->value == first && !s.bindings[1],
	      "Missing parameter changed partial batch state");
	s.bind(2, pg::null(23), limits);
	s.add(limits);
	check(s.bytes == first_cost && !s.bindings[0] && !s.bindings[1],
	      "Accepted batch did not consume bindings exactly once");
	s.bind(1, pg::text(second), limits);
	s.bind(2, pg::boolean(true), limits);
	rejects([&] { s.add(limits); }, pg::Error::invalid);
	check(s.batch.size() == 1 && s.bytes == first_cost &&
	          s.types == std::vector<std::uint32_t>{25, 23} &&
	          s.bindings[0]->value == second && s.bindings[1]->oid == 16,
	      "Type rejection changed retained state or consumed bindings");
	s.bind(2, pg::null(23), limits);
	// One extra byte exceeds the exact remaining budget without hitting row count.
	s.bind(1, pg::text(second + "x"), limits);
	rejects([&] { s.add(limits); }, pg::Error::limit);
	check(s.batch.size() == 1 && s.bytes == first_cost &&
	          s.bindings[0]->value == second + "x" && s.bindings[1]->oid == 23,
	      "Byte limit rejection consumed bindings or changed accounting");
	s.bind(1, pg::text(second), limits);
	s.add(limits);
	check(s.batch.size() == 2 && s.bytes == limits.batch_bytes &&
	          s.batch[0][0].value == first && s.batch[1][0].value == second &&
	          !s.batch[0][1].value && !s.batch[1][1].value &&
	          !s.bindings[0] && !s.bindings[1],
	      "Repair after rejection changed snapshots or exact-boundary admission");
	limits.parameter_bytes = second.size() - 1;
	s.bind(1, pg::text(""), limits);
	rejects([&] { s.bind(1, pg::text(second), limits); }, pg::Error::limit);
	check(s.bindings[0]->value == "" && s.batch[0][0].value == first &&
	          s.batch[1][0].value == second && s.bytes == limits.batch_bytes,
	      "Oversized rebind destroyed previous binding or snapshots");
}
void exercise(std::mt19937 &rng) {
	batch_atomicity(rng);
	// Generate valid documents with independently known section names and exact slices.
	const std::string nl = (rng() & 1) ? "\r\n" : "\n";
	const std::string tag = "q" + std::to_string(rng());
	const std::string hidden = "; -- name: fake" + nl + "/* nested */";
	const std::string bodies[] = {"SELECT 'it''s; -- name: fake' AS v;",
	                              "SELECT E'it\\'s; -- name: fake' AS v;",
	                              "SELECT $" + tag + "$" + hidden + "$" + tag + "$ AS v;",
	                              "SELECT 1 /* outer /* -- name: fake */ still outer */;",
	                              "SELECT \"column; -- name: fake\";"};
	const auto body = bodies[rng() % 5];
	const auto number = std::to_string(rng() % 100000);
	const auto second = "SELECT " + number + ";";
	input = "-- name: first" + nl + body + nl + "-- name: second" + nl + second;
	auto parsed = pg::parse_sql_sections(input, "fuzz.sql");
	check(parsed.sections.size() == 2, "Lexer exposed a hidden declaration");
	check(parsed.select("first") == nl + body + nl, "First section slice corrupted");
	check(parsed.select("second") == nl + second, "Second section slice corrupted");
	for (const auto &[name, range] : parsed.sections) {
		check(range.first <= input.size() && range.second <= input.size() - range.first,
		      "Section range outside input");
		check(pg::parse_sql_sections("-- name: isolated\n" + parsed.select(name))
		              .select("isolated") == "\n" + parsed.select(name),
		      "Selection not compositional");
	}
	input += nl + "-- name: first\nSELECT 3;";
	rejects(
	    [&] {
		    pg::parse_sql_sections(input);
	    },
	    pg::Error::file);
	input = "-- name: x\nSELECT 1;\nSELECT 2;";
	rejects(
	    [&] {
		    pg::parse_sql_sections(input);
	    },
	    pg::Error::file);

	// Independent scalar generator, then byte sequences forbidden by Unicode UTF-8.
	input.clear();
	for (unsigned n = 0; n != 16; ++n) {
		auto c = 1 + rng() % 0x10ffff;
		if (c >= 0xd800 && c <= 0xdfff) {
			c = 'x';
		}
		scalar(input, c);
	}
	check(pg::utf8(input), "Valid Unicode scalar encoding rejected");
	const std::string bad[] = {std::string(1, '\0'), "\x80",    "\xc0\xaf", "\xed\xa0\x80",
	                           "\xf4\x90\x80\x80",   "\xe2\x82"};
	input += bad[rng() % 6];
	check(!pg::utf8(input), "Invalid Unicode encoding accepted");
	rejects(
	    [&] {
		    pg::text(input, 25);
	    },
	    pg::Error::invalid);
	rejects(
	    [&] {
		    pg::parse_sql_sections(input);
	    },
	    pg::Error::file);

	std::vector<std::int32_t> bytes(rng() % 1025);
	for (auto &b : bytes) {
		b = rng() & 255;
	}
	const auto encoded = pg::byte_array(bytes);
	input = *encoded.value;
	check(pg::decode_bytea(input) == bytes, "BYTEA roundtrip changed bytes");
	// Keep an even number of digits: rejection must inspect content, not length.
	input = *pg::byte_array(std::vector<std::int32_t>{0, 255}).value;
	input[2 + rng() % 4] = 'g';
	rejects([&] { pg::decode_bytea(input); }, pg::Error::invalid);
	input = *encoded.value;
	input += 'z';
	rejects(
	    [&] {
		    pg::decode_bytea(input);
	    },
	    pg::Error::invalid);
	// Generate all float classes by representation, including signed zero and
	// subnormals. Parse independently and require exact finite bit preservation.
	const auto bits = static_cast<std::uint32_t>(rng());
	const auto value = std::bit_cast<float>(bits);
	if (std::isfinite(value)) {
		auto parameter = pg::floating(value);
		input = *parameter.value;
		float decoded = 0;
		const auto result = std::from_chars(input.data(), input.data()+input.size(), decoded);
		check(parameter.oid == 700 && result.ec == std::errc{} &&
		          result.ptr == input.data()+input.size() &&
		          std::bit_cast<std::uint32_t>(decoded) == bits,
		      "FLOAT4 encoding changed its finite representation");
	} else {
		rejects([&] { pg::floating(value); }, pg::Error::invalid);
	}
	const auto wide_bits = (std::uint64_t(rng()) << 32) | rng();
	const auto wide = std::bit_cast<std::int64_t>(wide_bits);
	input = std::to_string(wide);
	const auto bigint = pg::integer64(input);
	check(bigint.oid == 20 && bigint.value == input, "BIGINT transport changed digits");
	input += 'x';
	rejects([&] { pg::integer64(input); }, pg::Error::invalid);
	pg::Limits limits;
	limits.batch_count = 1;
	pg::Statement statement{};
	statement.bindings.resize(1);
	statement.bind(1, pg::integer(std::int32_t(rng() % 100000)), limits);
	statement.add(limits);
	const auto retained = statement.batch.front().front().value;
	const auto retained_bytes = statement.bytes;
	statement.bind(1, pg::integer(42), limits);
	rejects(
	    [&] {
		    statement.add(limits);
	    },
	    pg::Error::limit);
	check(statement.bytes == retained_bytes && statement.batch.size() == 1 &&
	          statement.batch.front().front().value == retained &&
	          statement.bindings[0]->value == "42",
	      "Rejected add consumed or corrupted snapshots");
}
} // namespace
int main() {
	unsigned seed = 0;
	try {
		seed = setting("OMP_PG_FUZZ_SEED", 0x41554449, 0xffffffffu);
		const auto count = setting("OMP_PG_FUZZ_CASES", 4096, 10000000);
		std::cout << "semantic_fuzz seed=" << seed << " cases=" << count << std::endl;
		std::mt19937 rng(seed);
		// Fixed boundary corpus complements randomly generated representations.
		for (const auto &s : {"-9223372036854775808", "9223372036854775807", "0", "-0"}) {
			input = s;
			check(pg::integer64(input).value == input, "BIGINT boundary digits changed");
		}
		for (const auto &s : {"9223372036854775808", "-9223372036854775809", "",
		                       "+1", " 1", "1 ", "--1"}) {
			input = s;
			rejects([&] { pg::integer64(input); }, pg::Error::invalid);
		}
		for (auto bits : {0u, 0x80000000u, 1u, 0x007fffffu, 0x00800000u,
		                  0x7f7fffffu, 0xff7fffffu}) {
			const auto parameter = pg::floating(std::bit_cast<float>(bits));
			input = *parameter.value;
			float decoded = 0;
			const auto result = std::from_chars(input.data(), input.data()+input.size(), decoded);
			check(result.ec == std::errc{} && result.ptr == input.data()+input.size() &&
			          std::bit_cast<std::uint32_t>(decoded) == bits,
			      "FLOAT4 boundary representation changed");
		}
		for (; iteration != count; ++iteration) {
			exercise(rng);
		}
		std::cout << "semantic_fuzz PASS cases=" << iteration << std::endl;
		return 0;
	} catch (const std::exception &e) {
		std::ofstream corpus("audit-fuzz-failure.bin", std::ios::binary);
		corpus.write(input.data(), static_cast<std::streamsize>(input.size()));
		std::cerr << "semantic_fuzz FAIL seed=" << seed << " iteration=" << iteration
		          << " condition=" << e.what() << " corpus=audit-fuzz-failure.bin\n";
		return 1;
	}
}
