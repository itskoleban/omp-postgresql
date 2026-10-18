#include "pg/sql_sections.hpp"
#include "pg/engine.hpp"
#include "pg/parameters.hpp"
#include "pg/stream.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
#include <tuple>

namespace {

unsigned checks = 0;
constexpr pg::Owner owner = 41;

void check(bool condition, const char *message) {
	++checks;
	if (!condition) {
		std::cerr << "SQL section failure: " << message << std::endl;
		throw std::runtime_error(message);
	}
}

template <class F> void rejects(F f, pg::Error code, const char *message) {
	try {
		f();
	} catch (const pg::Failure &failure) {
		check(failure.code == code && failure.detail == message, "Wrong rejection diagnostic");
		return;
	}
	check(false, "Expected rejection");
}

struct Files {
	std::filesystem::path root =
	    std::filesystem::temp_directory_path() /
	    ("omp-pg-sections-" + std::to_string(pg::Clock::now().time_since_epoch().count()));
	Files() {
		std::filesystem::create_directory(root);
	}
	~Files() {
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}
	void write(const std::string &sql, const char *name = "sections.sql") {
		std::ofstream file(root / name, std::ios::binary | std::ios::trunc);
		file << sql;
		check(file.good(), "Fixture write failed");
	}
};

void unit() {
	for (const auto &valid : {"a", "_", "LOAD_1", "load_1"}) {
		check(pg::valid_section_name(valid), "Valid section name rejected");
	}
	check(pg::valid_section_name(std::string(64, 'a')), "64-byte name rejected");
	for (const auto &bad : {"", "1bad", "two words", "a-b", "a\t", "a\n", "\xc3\xb1", "x;"}) {
		check(!pg::valid_section_name(bad), "Invalid name accepted");
	}
	check(!pg::valid_section_name(std::string(65, 'a')), "Long name accepted");
	const std::string contents =
	    "\xef\xbb\xbf-- ordinary preamble\r\n/* nested /* -- name: fake */ ok */\r\n"
	    "-- name: first\r\nSELECT $1::integer;\r\n"
	    "  -- name: Middle \r\nSELECT '; -- name: fake'::text;\r\n"
	    "\t-- name: last\r\nSELECT $2::jsonb";
	auto document = pg::parse_sql_sections(contents);
	check(document.sections.size() == 3, "Wrong section count");
	check(document.select("first").find("SELECT $1::integer;") != std::string::npos,
	      "First section differs");
	check(document.select("Middle").find("'; -- name: fake'") != std::string::npos,
	      "Middle section differs");
	check(document.select("last") == "\r\nSELECT $2::jsonb", "Last section differs");
	rejects(
	    [&] {
		    document.select("middle");
	    },
	    pg::Error::file, "SQL section not found");
	auto moved = std::move(document);
	check(moved.select("last") == "\r\nSELECT $2::jsonb", "Moving document invalidated spans");
	for (const std::string body :
	     {"SELECT '-- name: fake; it''s quoted';", "SELECT E'it\\'s\n-- name: fake\n;';",
	      "SELECT e'a'\n'it\\'s\n-- name: fake\n';", "SELECT $$\n-- name: fake\n;$$;",
	      "SELECT $body$\n-- name: fake\n;$other$$body$;",
	      "SELECT $\xc3\xb1$\n-- name: fake\n;$\xc3\xb1$;",
	      "SELECT 1 AS \"name;\n-- name: fake\n\"\"q\";",
	      "SELECT 1 /* nested /*\n-- name: fake\n*/ x */; -- ordinary;",
	      "SELECT foo$body$, $1::integer, $2::text, $3::jsonb;",
	      "SELECT U&'d\\0061t', B'10', X'ff', 'a\\';",
	      "WITH x AS (SELECT * FROM unnest($1::integer[]) AS v(id)) SELECT x.id FROM x JOIN x y "
	      "USING(id);",
	      "UPDATE accounts SET money=$1::integer WHERE id=$2 RETURNING id;"}) {
		auto parsed =
		    pg::parse_sql_sections("-- name: real\n" + body + "\n-- name: tail\nSELECT 2;");
		check(parsed.sections.size() == 2 && parsed.select("real").find(body) != std::string::npos,
		      "Quoted/commented content changed section boundaries");
	}
	for (const std::string declaration : std::vector<std::string>{
	         "-- name:", "-- name", "-- name query", "-- name : x", "-- name: 123_invalid",
	         "-- name: a b", "-- name: x;&", "-- name: \xc3\xb1",
	         "-- name: " + std::string(65, 'a'), "\xe2\x80\x94 name: x", "\xe2\x80\x93 name: x",
	         "\xef\xbc\x8d\xef\xbc\x8d name: x"}) {
		rejects(
		    [&] {
			    pg::parse_sql_sections("-- name: valid\nSELECT 1;\n" + declaration + "\nSELECT 2;");
		    },
		    pg::Error::file, "Malformed SQL section declaration");
	}
	for (const auto &sql : {"", "-- ordinary\n", "/* ordinary */"}) {
		rejects(
		    [&] {
			    pg::parse_sql_sections(sql);
		    },
		    pg::Error::file, "SQL file contains no sections");
	}
	rejects(
	    [] {
		    pg::parse_sql_sections("SELECT 1;\n-- name: x\nSELECT 2;");
	    },
	    pg::Error::file, "SQL content before first section");
	rejects(
	    [] {
		    pg::parse_sql_sections("-- name: x\nSELECT 1;\n-- name: x\nSELECT 2;");
	    },
	    pg::Error::file, "Duplicate SQL section");
	for (const auto &body : {"", "/* comment */", "-- ordinary"}) {
		rejects(
		    [&] {
			    pg::parse_sql_sections("-- name: x\n" + std::string(body));
		    },
		    pg::Error::file, "Empty SQL section");
	}
	for (const auto &body : {"SELECT 1; SELECT 2;", "SELECT 1;;", ";SELECT 1"}) {
		rejects(
		    [&] {
			    pg::parse_sql_sections("-- name: x\n" + std::string(body));
		    },
		    pg::Error::file, "SQL section must contain exactly one statement");
	}
	for (const auto &body : {"SELECT '", "SELECT E'\\", "SELECT \""}) {
		rejects(
		    [&] {
			    pg::parse_sql_sections("-- name: x\n" + std::string(body));
		    },
		    pg::Error::file, "Unterminated SQL quoted value");
	}
	rejects(
	    [] {
		    pg::parse_sql_sections("-- name: x\nSELECT $body$;");
	    },
	    pg::Error::file, "Unterminated SQL dollar quote");
	rejects(
	    [] {
		    pg::parse_sql_sections("-- name: x\nSELECT 1; /* /* */");
	    },
	    pg::Error::file, "Unterminated SQL block comment");
	for (const std::string bad :
	     std::vector<std::string>{std::string("-- name: x\nSELECT '") + '\0' + "'", "\xc0\xaf"}) {
		rejects(
		    [&] {
			    pg::parse_sql_sections(bad);
		    },
		    pg::Error::file, "SQL sections require UTF-8 without NUL");
	}
	auto ordinary = pg::parse_sql_sections("-- named queries\n-- namespace: example\n-- name: "
	                                       "x\nSELECT 1; -- name: inline_is_not_a_section");
	check(ordinary.sections.size() == 1, "Ordinary comments mistaken for declarations");
	const std::string long_tag = std::string(127, 'a') + 'b';
	std::string prefixes;
	for (unsigned i = 0; i < 512; ++i) {
		prefixes += '$' + std::string(127, 'a') + 'x';
	}
	auto long_dollar = pg::parse_sql_sections("-- name: x\nSELECT $" + long_tag + '$' + prefixes +
	                                          '$' + long_tag + "$;");
	check(long_dollar.sections.size() == 1, "Long repeated dollar prefixes failed");
	std::string many;
	for (unsigned i = 0; i < pg::sql_section_count; ++i) {
		many += "-- name: q" + std::to_string(i) + "\nSELECT 1;\n";
	}
	check(pg::parse_sql_sections(many).sections.size() == pg::sql_section_count,
	      "Section limit boundary failed");
	rejects(
	    [&] {
		    pg::parse_sql_sections(many + "-- name: extra\nSELECT 1");
	    },
	    pg::Error::limit, "SQL section count limit exceeded");
	// Fixed-seed byte mutations and every truncation exercise the real bounded lexer.
	const std::string adversarial = "-- name: x\nSELECT E'it\\'s', $tag$; -- name: fake\n$tag$, "
	                                "\"a\"\"b\" /* /* */ */;\n-- name: y\nSELECT 2;";
	for (std::size_t i = 0; i < adversarial.size(); ++i) {
		try {
			pg::parse_sql_sections(adversarial.substr(0, i));
		} catch (const pg::Failure &) {
		}
		++checks;
	}
	std::mt19937 random(0x53454354);
	for (unsigned i = 0; i < 4096; ++i) {
		auto changed = adversarial;
		changed[random() % changed.size()] = static_cast<char>(random() & 255);
		try {
			pg::parse_sql_sections(std::move(changed));
		} catch (const pg::Failure &) {
		}
		++checks;
	}
	Files files;
	files.write(contents);
	std::filesystem::create_directory(files.root / "inner");
	rejects(
	    [&] {
		    pg::read_sql_contents(files.root / "inner", "../sections.sql", 4096);
	    },
	    pg::Error::file, "SQL path escapes configured directory");
	rejects(
	    [&] {
		    pg::read_sql_contents(files.root, (files.root / "sections.sql").string(), 4096);
	    },
	    pg::Error::file, "Expected relative .sql path");
	check(pg::parse_sql_sections(pg::read_sql_contents(files.root, "sections.sql", 4096))
	              .sections.size() == 3,
	      "Read/index failed");
	rejects(
	    [&] {
		    pg::read_sql_contents(files.root, "sections.sql", 8);
	    },
	    pg::Error::file, "SQL file exceeds size limit");
	rejects(
	    [&] {
		    pg::read_sql_contents(files.root, "missing.sql", 4096);
	    },
	    pg::Error::file, "SQL file not found");
	rejects(
	    [&] {
		    pg::read_sql_contents(files.root, "../sections.sql", 4096);
	    },
	    pg::Error::file, "SQL file not found");
}

void ready(pg::Engine &engine, pg::Id connection) {
	const auto deadline = pg::Clock::now() + std::chrono::seconds(15);
	while (engine.connection(owner, connection).state() != pg::State::connected) {
		engine.poll();
		check(pg::Clock::now() < deadline, "Connection deadline");
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}

std::shared_ptr<pg::Job> wait(pg::Engine &engine, pg::Id id, bool success = true) {
	auto job = engine.job(owner, id);
	const auto deadline = pg::Clock::now() + std::chrono::seconds(15);
	while (!pg::terminal(job->status.load()) || !job->delivered) {
		engine.poll();
		check(pg::Clock::now() < deadline, "Job deadline");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (success && job->status.load() != pg::Status::confirmed) {
		std::cerr << job->message << std::endl;
		check(false, "Expected confirmed SQL");
	}
	return job;
}

pg::Id run(pg::Engine &engine, pg::Id statement, int lane = 0, pg::Id transaction = 0) {
	engine.statement(owner, statement).add(engine.limits());
	return engine.execute(owner, statement, lane, transaction, 0, false);
}

void dispatched(pg::Engine &engine, pg::Id connection, std::uint64_t previous) {
	const auto deadline = pg::Clock::now() + std::chrono::seconds(15);
	while (engine.connection(owner, connection).lanes[0]->snapshot().sql_commands == previous) {
		check(pg::Clock::now() < deadline, "Dispatch deadline");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

void integration(const char *profile) {
	Files files;
	pg::Limits limits;
	pg::Engine engine(limits, files.root);
	auto connection = engine.connect(owner, profile, 4);
	ready(engine, connection);
	files.write("-- name: first\nSELECT $1::integer AS value;\n-- name: middle\nSELECT "
	            "$1::text;\n-- name: last\nSELECT $1::jsonb;");
	for (auto [name, parameter, expected] :
	     std::vector<std::tuple<std::string, pg::Param, std::string>>{
	         {"first", pg::integer(42), "42"},
	         {"middle", pg::text("snapshot"), "snapshot"},
	         {"last", pg::text("{\"x\":1}", 3802), "{\"x\": 1}"}}) {
		for (bool unified : {false, true}) {
			auto statement =
			    unified ? engine.prepare_file(owner, connection, "sections.sql", 1, true, name)
			            : engine.prepare_section(owner, connection, "sections.sql", name, 1);
			engine.statement(owner, statement).bind(1, parameter, limits);
			auto id = run(engine, statement);
			engine.close_statement(owner, statement);
			auto job = wait(engine, id);
			check(job->results.at(0).rows.at(0).at(0) == expected, "Selected result differs");
			engine.close_job(owner, id);
		}
	}
	// Unselected structural defects reject the entire file before any SQL dispatch.
	for (const auto &tail :
	     {"-- name: duplicate\nSELECT 9;\n-- name: duplicate\nSELECT 8;",
	      "-- name: bad name\nSELECT 8;", "-- name: empty", "-- name: truncated\nSELECT '"}) {
		files.write("-- name: selected\nSELECT 123;\n" + std::string(tail));
		auto statement = engine.prepare_section(owner, connection, "sections.sql", "selected", 0);
		auto before = engine.connection(owner, connection).lanes[0]->snapshot().sql_commands;
		auto job = wait(engine, run(engine, statement), false);
		check(job->error == pg::Error::file && job->results.empty() && job->completed == 0 &&
		          engine.connection(owner, connection).lanes[0]->snapshot().sql_commands == before,
		      "Malformed unselected section allowed dispatch");
		engine.close_job(owner, job->id);
		engine.close_statement(owner, statement);
	}
	// Four workers, immutable parameter snapshots, and statement ownership after close.
	files.write("-- name: selected\nSELECT 1;");
	{
		auto batch = engine.create_batch(owner, connection);
		auto first = engine.prepare(owner, connection, "SELECT 99", 0);
		auto missing = engine.prepare_section(owner, connection, "sections.sql", "missing", 0);
		for (auto s : {first, missing}) {
			engine.statement(owner, s).add(limits);
			engine.add_batch(owner, batch, s);
			engine.close_statement(owner, s);
		}
		auto before = engine.connection(owner, connection).lanes[0]->snapshot().sql_commands;
		auto rejected = wait(engine, engine.execute_batch(owner, batch, 0, 0, 0, false), false);
		check(rejected->error_index == 1 && rejected->error == pg::Error::file &&
		          rejected->items_status ==
		              std::vector{pg::ItemStatus::not_executed, pg::ItemStatus::failed} &&
		          rejected->results.empty() &&
		          engine.connection(owner, connection).lanes[0]->snapshot().sql_commands == before,
		      "Heterogeneous preflight dispatched earlier SQL or misattributed the error");
		engine.close_job(owner, rejected->id);
		engine.close_batch(owner, batch);
	}
	files.write("-- name: selected\nSELECT $1::integer;\n-- name: other\nSELECT -$1::integer;");
	std::vector<pg::Id> jobs;
	for (int lane = 0; lane < 4; ++lane) {
		auto statement = engine.prepare_section(owner, connection, "sections.sql",
		                                        lane % 2 ? "other" : "selected", 1);
		for (int n = 0; n < 20; ++n) {
			engine.statement(owner, statement).bind(1, pg::integer(lane * 100 + n), limits);
			engine.statement(owner, statement).add(limits);
		}
		jobs.push_back(engine.execute(owner, statement, lane, 0, 0, false));
		engine.close_statement(owner, statement);
	}
	for (int lane = 0; lane < 4; ++lane) {
		auto job = wait(engine, jobs[lane]);
		check(job->results.size() == 20, "Snapshot count differs");
		for (int n = 0; n < 20; ++n) {
			check(job->results[n].rows[0][0] ==
			          std::to_string((lane % 2 ? -1 : 1) * (lane * 100 + n)),
			      "Concurrent snapshots differ");
		}
		engine.close_job(owner, job->id);
	}
	// Read occurs at worker execution, then every execution in this job uses that snapshot.
	files.write("-- name: selected\nSELECT pg_sleep(0.15), $1::integer, 10;");
	auto statement = engine.prepare_section(owner, connection, "sections.sql", "selected", 1);
	for (int n = 0; n < 3; ++n) {
		engine.statement(owner, statement).bind(1, pg::integer(n), limits);
		engine.statement(owner, statement).add(limits);
	}
	auto before = engine.connection(owner, connection).lanes[0]->snapshot().sql_commands;
	auto id = engine.execute(owner, statement, 0, 0, 0, false);
	dispatched(engine, connection, before);
	files.write("-- name: selected\nSELECT $1::integer, 20;");
	auto captured = wait(engine, id);
	for (const auto &result : captured->results) {
		check(result.rows[0][2] == "10", "File edit changed accepted worker snapshot");
	}
	engine.close_job(owner, id);
	engine.statement(owner, statement).bind(1, pg::integer(7), limits);
	auto reloaded = wait(engine, run(engine, statement));
	check(reloaded->results[0].rows[0][1] == "20", "New job did not reload the file");
	engine.close_job(owner, reloaded->id);
	engine.close_statement(owner, statement);

	// A heterogeneous job captures all named sections before its first SQL operation.
	files.write("-- name: first\nSELECT pg_sleep(0.15), 11;\n-- name: last\nSELECT 22;");
	auto batch = engine.create_batch(owner, connection);
	for (const auto *name : {"first", "last", "last"}) {
		auto s = engine.prepare_section(owner, connection, "sections.sql", name, 0);
		engine.statement(owner, s).add(limits);
		engine.add_batch(owner, batch, s);
		engine.close_statement(owner, s);
	}
	before = engine.connection(owner, connection).lanes[0]->snapshot().sql_commands;
	id = engine.execute_batch(owner, batch, 0, 0, 0, false);
	engine.close_batch(owner, batch);
	dispatched(engine, connection, before);
	files.write("-- name: first\nSELECT 33;\n-- name: last\nSELECT 44;");
	auto heterogeneous = wait(engine, id);
	check(heterogeneous->results.size() == 3 && heterogeneous->results[0].rows[0][1] == "11" &&
	          heterogeneous->results[1].rows[0][0] == "22" &&
	          heterogeneous->results[2].rows[0][0] == "22",
	      "Heterogeneous file snapshot changed");
	engine.close_job(owner, id);
	// Ordinary SQL remains unchanged, including file mode and multiple-command rejection.
	files.write("SELECT 31;", "plain.sql");
	auto plain = engine.prepare(owner, connection, "plain.sql", 0, true);
	auto result = wait(engine, run(engine, plain));
	check(result->results[0].rows[0][0] == "31", "pg_prepare_file regressed");
	engine.close_job(owner, result->id);
	engine.close_statement(owner, plain);
	for (const auto &sql :
	     {"-- name: selected\nSELEC 1;", "-- name: selected\nSELECT 1; SELECT 2;"}) {
		files.write(sql);
		auto s = engine.prepare_section(owner, connection, "sections.sql", "selected", 0);
		auto failure = wait(engine, run(engine, s), false);
		check(failure->status == pg::Status::failed &&
		          (failure->error == pg::Error::sql || failure->error == pg::Error::file),
		      "SQL rejection differs");
		engine.close_job(owner, failure->id);
		engine.close_statement(owner, s);
	}
	// PostgreSQL authorization is still enforced; a lexer is not a permission system.
	const auto role =
	    "section_reader_" + std::to_string(pg::Clock::now().time_since_epoch().count());
	for (const auto &sql : std::vector<std::string>{"CREATE ROLE " + role + " NOLOGIN",
	                                                "CREATE TEMP TABLE section_private(id integer)",
	                                                "SET ROLE " + role}) {
		auto job = wait(engine, engine.query(owner, connection, sql, 0, 0, 0, false));
		engine.close_job(owner, job->id);
	}
	files.write("-- name: selected\nSELECT * FROM section_private;");
	statement = engine.prepare_section(owner, connection, "sections.sql", "selected", 0);
	auto denied = wait(engine, run(engine, statement), false);
	check(denied->error == pg::Error::sql && denied->results.at(0).sqlstate == "42501",
	      "Permission failure lost SQLSTATE");
	engine.close_job(owner, denied->id);
	engine.close_statement(owner, statement);
	for (const auto &sql : {std::string("RESET ROLE"), "DROP ROLE " + role}) {
		auto job = wait(engine, engine.query(owner, connection, sql, 0, 0, 0, false));
		engine.close_job(owner, job->id);
	}
	// Bind arrays and use a CTE/JOIN/RETURNING through a named section in a transaction.
	files.write(
	    "-- name: create_accounts\nCREATE TEMP TABLE section_accounts(id integer PRIMARY KEY, "
	    "money integer);\n"
	    "-- name: insert_accounts\nINSERT INTO section_accounts VALUES (1,0),(2,0);\n"
	    "-- name: save_accounts\nWITH changed AS (UPDATE section_accounts a SET money=v.money FROM "
	    "unnest($1::integer[],$2::integer[]) v(id,money) WHERE a.id=v.id RETURNING a.id,a.money) "
	    "SELECT c.id,c.money FROM changed c JOIN section_accounts a ON c.id=a.id ORDER BY c.id;");
	auto tx = engine.begin(owner, connection, 0, 0, false);
	wait(engine, tx);
	for (const auto *name : {"create_accounts", "insert_accounts"}) {
		auto s = engine.prepare_section(owner, connection, "sections.sql", name, 0, false);
		auto job = wait(engine, run(engine, s, 0, tx));
		engine.close_job(owner, job->id);
		engine.close_statement(owner, s);
	}
	statement = engine.prepare_section(owner, connection, "sections.sql", "save_accounts", 2);
	const std::int32_t ids[]{1, 2}, amounts[]{100, 200};
	engine.statement(owner, statement).bind(1, pg::int_array(ids), limits);
	engine.statement(owner, statement).bind(2, pg::int_array(amounts), limits);
	auto bulk = wait(engine, run(engine, statement, 0, tx));
	check(bulk->results.at(0).rows ==
	          std::vector<std::vector<std::optional<std::string>>>{{"1", "100"}, {"2", "200"}},
	      "Named UNNEST update differs");
	engine.close_job(owner, bulk->id);
	engine.close_statement(owner, statement);
	auto saved =
	    wait(engine, engine.savepoint(owner, tx, "section_save", pg::Kind::savepoint, 0, false));
	engine.close_job(owner, saved->id);
	files.write(
	    "-- name: fail\nSELECT 1/0;\n-- name: check_rows\nSELECT count(*) FROM section_accounts;");
	statement = engine.prepare_section(owner, connection, "sections.sql", "fail", 0);
	auto failed = wait(engine, run(engine, statement, 0, tx), false);
	check(failed->error == pg::Error::sql && failed->results[0].sqlstate == "22012",
	      "Named transaction failure lost SQLSTATE");
	engine.close_job(owner, failed->id);
	engine.close_statement(owner, statement);
	auto recovered =
	    wait(engine, engine.savepoint(owner, tx, "section_save", pg::Kind::rollback_to, 0, false));
	engine.close_job(owner, recovered->id);
	statement = engine.prepare_section(owner, connection, "sections.sql", "check_rows", 0);
	auto counted = wait(engine, run(engine, statement, 0, tx));
	check(counted->results[0].rows[0][0] == "2",
	      "Named section did not recover after savepoint rollback");
	engine.close_job(owner, counted->id);
	engine.close_statement(owner, statement);
	auto finish = wait(engine, engine.finish(owner, tx, true, 0, false));
	engine.close_job(owner, finish->id);
	engine.close_job(owner, tx);

	// Missing files/names and capture budgets retain explicit asynchronous diagnostics.
	files.write("-- name: selected\nSELECT 1;");
	for (auto [filename, section, message] :
	     std::vector<std::tuple<std::string, std::string, const char *>>{
	         {"missing.sql", "selected", "SQL file not found"},
	         {"sections.sql", "missing", "SQL section not found"}}) {
		auto s = engine.prepare_section(owner, connection, filename, section, 0);
		auto failure = wait(engine, run(engine, s), false);
		check(failure->error == pg::Error::file && failure->message == message &&
		          failure->completed == 0,
		      "Deferred section diagnostic differs");
		engine.close_job(owner, failure->id);
		engine.close_statement(owner, s);
	}
	for (const auto &sql : {std::string("SET standard_conforming_strings=off"),
	                        std::string("SET standard_conforming_strings=on")}) {
		auto job = wait(engine, engine.query(owner, connection, sql, 0, 0, 0, false));
		engine.close_job(owner, job->id);
		if (sql.ends_with("off")) {
			auto s = engine.prepare_section(owner, connection, "sections.sql", "selected", 0);
			auto rejected = wait(engine, run(engine, s), false);
			check(rejected->error == pg::Error::invalid && rejected->completed == 0,
			      "Nonstandard strings were accepted");
			engine.close_job(owner, rejected->id);
			engine.close_statement(owner, s);
		}
	}
	// A queued job sees an edit before capture, and cancellation avoids reading it at all.
	files.write("-- name: selected\nSELECT 10;");
	before = engine.connection(owner, connection).lanes[0]->snapshot().sql_commands;
	auto blocker = engine.query(owner, connection, "SELECT pg_sleep(0.15)", 0, 0, 0, false);
	dispatched(engine, connection, before);
	statement = engine.prepare_section(owner, connection, "sections.sql", "selected", 0);
	id = run(engine, statement);
	files.write("-- name: selected\nSELECT 20;");
	wait(engine, blocker);
	auto queued = wait(engine, id);
	check(queued->results[0].rows[0][0] == "20", "Queued job read before its worker turn");
	engine.close_job(owner, blocker);
	engine.close_job(owner, id);
	blocker = engine.query(owner, connection, "SELECT pg_sleep(0.15)", 0, 0, 0, false);
	id = run(engine, statement);
	engine.job(owner, id)->cancel.store(true);
	wait(engine, blocker);
	auto cancelled = wait(engine, id, false);
	check(cancelled->status == pg::Status::cancelled && cancelled->results.empty(),
	      "Queued section cancellation dispatched SQL");
	engine.close_job(owner, blocker);
	engine.close_job(owner, id);
	engine.close_statement(owner, statement);
	// Streaming remains bounded and uses the same captured selection.
	files.write("-- name: selected\nSELECT generate_series(1,25);");
	statement = engine.prepare_section(owner, connection, "sections.sql", "selected", 0);
	engine.statement(owner, statement).add(limits);
	id = engine.execute_stream(owner, statement, 4, 0, 0, 0, false);
	engine.close_statement(owner, statement);
	auto stream_job = engine.job(owner, id);
	std::size_t rows = 0;
	const auto deadline = pg::Clock::now() + std::chrono::seconds(15);
	while (!pg::terminal(stream_job->status.load()) || stream_job->stream->available()) {
		if (stream_job->stream->next()) {
			rows += stream_job->stream->current()->rows.size();
			stream_job->stream->release();
		}
		engine.poll();
		check(pg::Clock::now() < deadline, "Section stream deadline");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	wait(engine, id);
	check(rows == 25, "Named section stream lost rows");
	engine.close_job(owner, id);
}

void lifecycle_and_limits(const char *profile) {
	Files files;
	files.write("-- name: selected\nSELECT pg_sleep(1);\n-- name: last\nSELECT 2;");
	for (const bool unload : {false, true}) {
		pg::Engine engine({}, files.root);
		auto connection = engine.connect(owner, profile, 1);
		ready(engine, connection);
		auto s = engine.prepare_section(owner, connection, "sections.sql", "selected", 0);
		auto before = engine.connection(owner, connection).lanes[0]->snapshot().sql_commands;
		auto id = run(engine, s);
		auto job = engine.job(owner, id);
		dispatched(engine, connection, before);
		if (unload) {
			engine.unload(owner);
		} else {
			job->cancel.store(true);
			engine.close_statement(owner, s);
		}
		const auto deadline = pg::Clock::now() + std::chrono::seconds(15);
		while (!pg::terminal(job->status.load())) {
			auto dispatch = engine.poll();
			check(dispatch.jobs.empty() && dispatch.streams.empty(),
			      "Unloaded VM received section completion");
			check(pg::Clock::now() < deadline, "Section cancellation/unload deadline");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		check(job->status == pg::Status::uncertain && job->completed == 0,
		      "Dispatched cancellation lost uncertainty");
		if (!unload) {
			wait(engine, id, false);
			engine.close_job(owner, id);
		}
		engine.poll();
		std::weak_ptr<pg::Job> released = job;
		job.reset();
		while (!released.expired()) {
			engine.poll();
			check(pg::Clock::now() < deadline, "Closed/unloaded section job leaked");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	pg::Limits limits;
	limits.batch_bytes = 32;
	pg::Engine engine(limits, files.root);
	auto connection = engine.connect(owner, profile, 1);
	ready(engine, connection);
	auto statement = engine.prepare_section(owner, connection, "sections.sql", "last", 0);
	auto job = wait(engine, run(engine, statement), false);
	check(job->error == pg::Error::limit &&
	          job->message == "SQL section capture exceeds batch byte limit" && job->completed == 0,
	      "Named file capture exceeded memory budget");
	engine.close_job(owner, job->id);
	engine.close_statement(owner, statement);
}

} // namespace

int main(int argc, char **argv) {
	try {
		if (argc == 2 && std::string(argv[1]) == "--integration") {
			const auto profile = std::getenv("OMP_PG_TEST_SERVICE");
			if (!profile) {
				std::cout << "Set OMP_PG_TEST_SERVICE for SQL section integration\n";
				return 77;
			}
			integration(profile);
			lifecycle_and_limits(profile);
		} else {
			unit();
		}
		std::cout << checks << " SQL section checks passed\n";
		return 0;
	} catch (const std::exception &failure) {
		std::cerr << "SQL section tests failed: " << failure.what() << std::endl;
		return 1;
	}
}
