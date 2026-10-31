// Scalar binders, NULL, JSONB and exact BIGINT transport without a table.
#include <open.mp>
#include <omp_postgresql>

new Database;
new bool:Started;

main() {}

public OnGameModeInit() {
	Database = pg_connect("game");
	if (!Database) Fail("connect rejected");
	return 1;
}

public OnGameModeExit() {
	if (Database) pg_connection_close(Database);
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection != Database) return 1;
	if (connection_state == PG_CONNECTION_FAILED) Fail("connection failed");
	if (connection_state == PG_CONNECTED && !Started) {
		Started = true;
		new statement = pg_prepare(Database,
			"SELECT $1::integer,$2::real,$3::boolean,$4::text,$5::bigint,$6::jsonb,$7::text", 7);
		if (!statement || !pg_bind_int(statement, 1, 42) || !pg_bind_float(statement, 2, 1.5) ||
			!pg_bind_bool(statement, 3, false) || !pg_bind_string(statement, 4, "O'Brien") ||
			!pg_bind_int64(statement, 5, "9223372036854775807") ||
			!pg_bind_string(statement, 6, "{\"level\":7}", PG_JSONB) ||
			!pg_bind_null(statement, 7, PG_TEXT) || !pg_add(statement)) Fail("binding rejected");
		else if (!pg_execute(statement)) Fail("submission rejected");
		if (statement) pg_statement_close(statement);
	}
	return 1;
}

public OnPgComplete(job, context) {
	#pragma unused context
	new integer, Float:real, bool:flag, text[32], exact[24], json[64];
	if (pg_job_status(job) != PG_CONFIRMED || pg_result_rows(job) != 1 ||
		!pg_result_int(job, 0, 0, 0, integer) || integer != 42 ||
		!pg_result_float(job, 0, 0, 1, real) || real != 1.5 ||
		!pg_result_bool(job, 0, 0, 2, flag) || flag ||
		!pg_result_string(job, 0, 0, 3, text) || strcmp(text, "O'Brien") ||
		!pg_result_string(job, 0, 0, 4, exact) || strcmp(exact, "9223372036854775807") ||
		pg_result_column_type(job, 0, 5) != PG_JSONB || !pg_result_string(job, 0, 0, 5, json) ||
		!pg_result_is_null(job, 0, 0, 6)) Fail("typed result differs");
	else print("PG VALUES PASS");
	pg_job_close(job);
	pg_connection_close(Database);
	return 1;
}

stock Fail(const reason[]) {
	new detail[256];
	pg_last_error(detail);
	printf("PG VALUES FAILED: %s (%s)", reason, detail);
	if (Database) pg_connection_close(Database);
}
