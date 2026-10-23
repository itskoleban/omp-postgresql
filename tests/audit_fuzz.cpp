// Deterministic semantic fuzzing of component input helpers, not the AMX loader.
#include "pg/parameters.hpp"
#include "pg/sql_sections.hpp"
#include "pg/statement.hpp"
#include <charconv>
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
void exercise(std::mt19937 &rng) {
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
	input += 'z';
	rejects(
	    [&] {
		    pg::decode_bytea(input);
	    },
	    pg::Error::invalid);
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
