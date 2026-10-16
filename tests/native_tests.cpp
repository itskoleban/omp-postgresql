#include "pawn_memory.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <regex>
#include <thread>

namespace {
unsigned checks = 0;
constexpr cell stack = 128 * sizeof(cell);
constexpr cell tail = 255 * sizeof(cell);

void check(bool condition, const char *message) {
	++checks;
	if (!condition) {
		throw std::runtime_error(message);
	}
}

class Harness {
public:
	pg::Component component;
	PawnMemory vm;

	Harness() {
		pg::Component::instance = &component;
		component.onAmxLoad(vm);
	}

	std::string &error() {
		return component.scripts.at(&vm.amx).error;
	}

	pg::Owner owner() {
		return component.scripts.at(&vm.amx).owner;
	}

	cell call(const std::string &name, std::initializer_list<cell> args) {
		std::vector<cell> params{static_cast<cell>(args.size() * sizeof(cell))};
		params.insert(params.end(), args);
		return vm.natives.at(name)(&vm.amx, params.data());
	}

	void text(const std::string &value, cell base = 0, bool packed = false) {
		auto start = static_cast<std::size_t>(base) / sizeof(cell);
		auto count = packed ? value.size() / sizeof(cell) + 1 : value.size() + 1;
		check(start + count <= vm.cells.size(), "Test string exceeds physical memory");
		std::fill_n(vm.cells.begin() + start, count, 0);
		for (std::size_t i = 0; i < value.size(); ++i) {
			auto byte = static_cast<unsigned char>(value[i]);
			if (packed) {
				auto shift = (sizeof(cell) - 1 - i % sizeof(cell)) * 8;
				auto &slot = vm.cells[start + i / sizeof(cell)];
				slot = static_cast<cell>(static_cast<ucell>(slot) |
				                         (static_cast<ucell>(byte) << shift));
			} else {
				vm.cells[start + i] = byte;
			}
		}
	}

	void reject(const std::string &name, std::initializer_list<cell> args,
	            const std::string &diagnostic) {
		auto before = vm.cells;
		check(call(name, args) == 0, "Invalid native input was accepted");
		if (error() != diagnostic) {
			throw std::runtime_error(name + ": expected " + diagnostic + ", got " + error());
		}
		check(vm.cells == before, "Rejected native partially modified Pawn memory");
	}

	void wait(cell job) {
		check(job > 0, "Native job was not accepted");
		auto deadline = pg::Clock::now() + std::chrono::seconds(20);
		auto j = component.engine.job(owner(), job);
		while (!pg::terminal(j->status.load()) || !j->delivered) {
			component.engine.poll();
			if (pg::Clock::now() > deadline) {
				throw std::runtime_error("Native test job timed out");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		check(j->status.load() == pg::Status::confirmed, "Native test SQL failed");
	}
};

void unit() {
	Harness h;
	// Derive names and arities independently from the public Pawn declarations.
	std::ifstream source(PG_PAWN_API_PATH);
	check(source.good(), "Cannot read public Pawn API");
	std::regex declaration(R"(^native (?:bool:)?(pg_\w+)\((.*)\);)");
	std::map<std::string, int> arities;
	std::string line;
	std::smatch match;
	while (std::getline(source, line)) {
		if (std::regex_search(line, match, declaration)) {
			auto args = match[2].str();
			arities.emplace(match[1].str(),
			                1 + static_cast<int>(std::count(args.begin(), args.end(), ',')));
		}
	}
	check(arities.size() == 68 && arities.size() == h.vm.natives.size(),
	      "Pawn/native inventory differs");
	for (auto &[name, count] : arities) {
		auto native = h.vm.natives.at(name);
		auto failure = name == "pg_job_status" || name == "pg_connection_state" ||
		                       name == "pg_result_column" || name == "pg_job_item_status" ||
		                       name == "pg_job_error_index" || name == "pg_lane_state"
		                   ? -1
		                   : 0;
		for (cell bytes :
		     {-1, 0, 1, count * 4 - 4, count * 4 + 4, std::numeric_limits<cell>::max()}) {
			h.error().clear();
			auto before = h.vm.cells;
			cell params[] = {bytes}; // Deliberately no argument payload: reject before reading it.
			check(native(&h.vm.amx, params) == failure,
			      "Wrong arity returned wrong error sentinel");
			check(h.error() == "Native argument count mismatch", "Wrong arity was not rejected");
			check(h.vm.cells == before, "Wrong arity wrote Pawn memory");
		}
		check(native(&h.vm.amx, nullptr) == failure, "Null argument block returned wrong sentinel");
		check(h.error() == "Native argument count mismatch",
		      "Null argument block was not rejected");
		std::vector<cell> params(count + 1, 0);
		params[0] = count * sizeof(cell);
		h.error().clear();
		check(native(&h.vm.amx, params.data()) == failure, "Invalid input returned wrong sentinel");
		check(!h.error().empty(), "Invalid zero handles/arguments were not rejected");
	}

	for (cell base : {-4, -1, 1, 3, 64 * 4, 127 * 4, 256 * 4, std::numeric_limits<cell>::max()}) {
		h.reject("pg_bind_int64", {0, 1, base}, "Invalid Pawn address");
	}
	for (auto fault : {PawnMemory::Fault::null, PawnMemory::Fault::allocation}) {
		h.vm.fault = fault;
		h.reject("pg_bind_int64", {0, 1, 0},
		         fault == PawnMemory::Fault::null ? "Invalid Pawn address"
		                                          : "Native allocation or internal error");
	}
	h.vm.fault = PawnMemory::Fault::none;
	std::mt19937 random(0x414d5801);
	for (int i = 0; i < 4096; ++i) {
		auto base = std::bit_cast<cell>(static_cast<std::uint32_t>(random()));
		auto before = h.vm.cells;
		check(h.call("pg_bind_int_array", {0, 1, base, 1}) == 0, "Fuzzed address was accepted");
		check(h.error() == "Invalid Pawn address" ||
		          h.error() == "Invalid statement handle or owner",
		      "Unexpected seeded address diagnostic");
		check(h.vm.cells == before, "Address fuzz modified memory");
	}

	h.vm.cells[255] = '7';
	h.reject("pg_bind_int64", {0, 1, tail}, "Invalid Pawn address");
	h.vm.cells[255] = static_cast<cell>(0x37373737); // Packed, missing terminator.
	h.reject("pg_bind_int64", {0, 1, tail}, "Invalid Pawn address");
	h.text(std::string(33, '7'));
	h.reject("pg_bind_int64", {0, 1, 0}, "Pawn string exceeds limit or is not byte encoded UTF-8");
	h.vm.cells[0] = '1';
	h.vm.cells[1] = 256;
	h.reject("pg_bind_int64", {0, 1, 0}, "Pawn string exceeds limit or is not byte encoded UTF-8");
	h.text("bad\xc0\xaf");
	h.reject("pg_bind_string", {0, 1, 0, 25}, "Invalid UTF-8 or embedded NUL");
	h.reject("pg_bind_int_array", {0, 1, 0, -1}, "Array size must be 0..16384");
	h.reject("pg_bind_int_array", {0, 1, 0, 16385}, "Array size must be 0..16384");
	h.reject("pg_bind_string_array", {0, 1, 0, 2, std::numeric_limits<cell>::max()},
	         "Invalid flat string array dimensions");
	h.reject("pg_bind_array", {0, 1, 0, 16385, 1, 25, 0}, "Invalid typed array dimensions");
	h.vm.cells[0] = 2;
	h.reject("pg_bind_array", {0, 1, stack, 1, 4, 25, 0}, "Array NULL mask values must be 0 or 1");
	h.reject("pg_bind_array", {0, 1, stack, 1, 4, 25, 1}, "Invalid Pawn address");
	h.reject("pg_bind_bool", {0, 1, 2}, "Boolean must be 0 or 1");

	// A failure in preflight must preserve the entire destination, including its prefix.
	for (cell base : {1, 63 * 4, tail}) {
		h.error() = "abcdef";
		h.reject("pg_last_error", {base, 8}, "Invalid Pawn address");
	}
	for (cell capacity : {-1, 0, 6, 2 * 1024 * 1024 + 1}) {
		h.error() = "abcdef";
		h.reject("pg_last_error", {stack, capacity},
		         "Output buffer too small; value not truncated");
	}
	h.error() = "abcdef";
	check(h.call("pg_last_error", {stack, 7}) == 1 && h.vm.cells[128] == 'a' &&
	          h.vm.cells[134] == 0,
	      "Exact-capacity diagnostic copy failed");
	check(h.error() == "abcdef", "Reading last error cleared its content");
	AMX foreign{};
	cell params[] = {sizeof(cell), 0};
	check(h.vm.natives.at("pg_job_close")(&foreign, params) == 0, "Foreign VM was accepted");
	h.component.onAmxUnload(h.vm);
	check(h.vm.natives.at("pg_job_close")(&h.vm.amx, params) == 0, "Unloaded VM was accepted");
	pg::Component::instance = nullptr;
	check(h.vm.natives.at("pg_job_close")(&h.vm.amx, params) == 0,
	      "Missing component was accepted");
	check(h.vm.unexpected_calls == 0, "Native used an unexpected SDK method");
}

void integration(const char *profile) {
	Harness h;
	h.text(profile);
	auto connection = h.call("pg_connect", {0, 1});
	check(connection > 0, "Native connection rejected");
	auto deadline = pg::Clock::now() + std::chrono::seconds(20);
	while (h.call("pg_connection_state", {connection}) != static_cast<cell>(pg::State::connected)) {
		h.component.engine.poll();
		if (pg::Clock::now() > deadline) {
			throw std::runtime_error("Native connection timeout");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	h.text("SELECT $1::text");
	auto statement = h.call("pg_prepare", {connection, 0, 1, 1});
	check(statement > 0, "Native preparation rejected");
	for (bool packed : {false, true}) {
		h.text("snapshot", 0, packed);
		check(h.call("pg_bind_string", {statement, 1, 0, 25}) == 1, "Valid Pawn string rejected");
		check(h.call("pg_add", {statement}) == 1, "Snapshot rejected");
		h.vm.cells.fill(0); // Accepted work must not retain VM memory.
	}
	h.reject("pg_bind_string", {statement, 1, tail + 4, 25}, "Invalid Pawn address");
	check(!h.component.engine.statement(h.owner(), statement).bindings[0],
	      "Invalid bind changed staging");
	h.text("snapshot");
	check(h.call("pg_bind_string", {statement, 1, 0, 25}) == 1, "Rebinding failed");
	h.reject("pg_bind_string", {statement, 1, 1, 25}, "Invalid Pawn address");
	check(h.component.engine.statement(h.owner(), statement).bindings[0]->value == "snapshot",
	      "Rejected rebind lost the previous parameter");
	auto job = h.call("pg_execute", {statement, 0, 0, 0, 0});
	check(h.call("pg_statement_close", {statement}) == 1, "Statement close rejected");
	h.wait(job);
	check(h.call("pg_result_count", {job}) == 2, "Snapshot result count differs");
	for (int i = 0; i < 2; ++i) {
		check(h.call("pg_result_string", {job, i, 0, 0, stack, 9}) == 1,
		      "Snapshot string copy failed");
		check(h.vm.cells[128] == 's' && h.vm.cells[135] == 't' && h.vm.cells[136] == 0,
		      "Snapshot changed after Pawn memory reuse");
	}
	h.reject("pg_result_string", {job, 0, 0, 0, 63 * 4, 9}, "Invalid Pawn address");
	h.reject("pg_result_string", {job, 0, 0, 0, stack, 8},
	         "Output buffer too small; value not truncated");
	h.reject("pg_result_string", {job, -1, 0, 0, stack, 9}, "Result index out of range");
	check(h.call("pg_job_close", {job}) == 1, "Job close rejected");

	// The same wrappers must handle arrays, binary NULs and empty/nonempty snapshots.
	h.text("SELECT $1::int[], $2::text[], $3::bytea, $4::text[]");
	statement = h.call("pg_prepare", {connection, 0, 4, 1});
	check(statement > 0, "Array statement rejected");
	h.vm.cells[0] = -2147483647;
	h.vm.cells[1] = 2147483647;
	check(h.call("pg_bind_int_array", {statement, 1, 0, 2}) == 1, "Integer cells rejected");
	h.text("alpha", 0);
	h.text("", 8 * 4);
	check(h.call("pg_bind_string_array", {statement, 2, 0, 2, 8}) == 1, "Flat slots rejected");
	h.vm.cells[0] = 0;
	h.vm.cells[1] = 1;
	h.vm.cells[2] = 255;
	check(h.call("pg_bind_bytes", {statement, 3, 0, 3}) == 1, "Binary cells rejected");
	h.text("alpha");
	h.vm.cells[128] = 0;
	h.vm.cells[129] = 1;
	check(h.call("pg_bind_array", {statement, 4, 0, 2, 8, 25, stack}) == 1,
	      "Nullable slots rejected");
	h.vm.cells[128] = 2;
	h.reject("pg_bind_array", {statement, 4, 0, 2, 8, 25, stack},
	         "Array NULL mask values must be 0 or 1");
	check(h.call("pg_add", {statement}) == 1, "Typed snapshot rejected after invalid rebind");
	// Empty arrays/BYTEA must not dereference their unused source addresses.
	check(h.call("pg_bind_int_array", {statement, 1, -1, 0}) == 1, "Empty integer array rejected");
	check(h.call("pg_bind_string_array", {statement, 2, -1, 0, 1}) == 1,
	      "Empty flat array rejected");
	check(h.call("pg_bind_bytes", {statement, 3, -1, 0}) == 1, "Empty BYTEA rejected");
	check(h.call("pg_bind_array", {statement, 4, -1, 0, 1, 25, -1}) == 1,
	      "Empty typed array rejected");
	check(h.call("pg_add", {statement}) == 1, "Empty snapshot rejected");
	h.vm.cells.fill(0);
	job = h.call("pg_execute", {statement, 0, 0, 0, 0});
	check(h.call("pg_statement_close", {statement}) == 1, "Array statement close failed");
	h.wait(job);
	auto data = h.component.engine.job(h.owner(), job);
	check(data->results.size() == 2 && data->results[0].rows.size() == 1 &&
	          data->results[1].rows.size() == 1,
	      "Array results differ");
	check(data->results[0].rows[0] ==
	          std::vector<std::optional<std::string>>{"{-2147483647,2147483647}", "{alpha,\"\"}",
	                                                  "\\x0001ff", "{alpha,NULL}"},
	      "Native array snapshots changed after invalid rebind or memory reuse");
	check(data->results[1].rows[0] ==
	          std::vector<std::optional<std::string>>{"{}", "{}", "\\x", "{}"},
	      "Native empty snapshots differ");
	check(h.call("pg_job_close", {job}) == 1, "Array job close failed");

	h.text("SELECT '\\x0001ff'::bytea, 42::integer, true, NULL::text, 2147483648::bigint", stack);
	job = h.call("pg_query", {connection, stack, 0, 0, 0, 0});
	h.wait(job);
	h.vm.cells.fill(0x12345678);
	h.reject("pg_result_bytes", {job, 0, 0, 0, 63 * 4, stack, 3}, "Invalid Pawn address");
	h.reject("pg_result_bytes", {job, 0, 0, 0, stack, stack + 4, 3},
	         "Length output overlaps the byte buffer");
	h.reject("pg_result_bytes", {job, 0, 0, 0, stack, tail + 4, 3}, "Invalid Pawn address");
	h.reject("pg_result_bytes", {job, 0, 0, 0, stack, 0, 2}, "Binary output buffer is too small");
	check(h.call("pg_result_bytes", {job, 0, 0, 0, stack, 0, 3}) == 1 && h.vm.cells[0] == 3 &&
	          h.vm.cells[128] == 0 && h.vm.cells[129] == 1 && h.vm.cells[130] == 255,
	      "BYTEA copy differs");
	h.reject("pg_result_int", {job, 0, 0, 1, 1}, "Invalid Pawn address");
	h.reject("pg_result_int", {job, 0, 0, 4, stack}, "Not a signed int32; read BIGINT as string");
	h.reject("pg_result_string", {job, 0, 0, 3, stack, 128}, "SQL NULL is not a string");
	check(h.call("pg_result_int", {job, 0, 0, 1, tail}) == 1 && h.vm.cells[255] == 42,
	      "Final valid cell was rejected");
	check(h.call("pg_result_bool", {job, 0, 0, 2, stack}) == 1 && h.vm.cells[128] == 1,
	      "Boolean output differs");
	PawnMemory foreign;
	h.component.onAmxLoad(foreign);
	cell params[] = {sizeof(cell), job};
	check(foreign.natives.at("pg_job_status")(&foreign.amx, params) == -1,
	      "Cross-owner job was accepted");
	h.component.onAmxUnload(foreign);
	check(h.call("pg_job_close", {job}) == 1, "Final job close rejected");
	h.reject("pg_job_close", {job}, "Invalid job handle or owner");
	h.component.onAmxUnload(h.vm);
	check(h.vm.unexpected_calls == 0 && foreign.unexpected_calls == 0,
	      "Unexpected SDK call in integration");
}
} // namespace

int main(int argc, char **argv) {
	try {
		if (argc == 2 && std::string(argv[1]) == "--integration") {
			auto profile = std::getenv("OMP_PG_TEST_SERVICE");
			if (!profile) {
				std::cout << "Set OMP_PG_TEST_SERVICE for native integration\n";
				return 77;
			}
			integration(profile);
		} else {
			unit();
		}
		std::cout << "Native validation passed: " << checks << " checks; address seed=0x414d5801\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Native validation failed after " << checks << " checks: " << e.what() << '\n';
		return 1;
	}
}
