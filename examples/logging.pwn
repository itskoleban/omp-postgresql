// Copy sql/accounts.sql to scriptfiles/sql/accounts.sql. Uses a disposable service profile.
// This deliberately misspells a valid section name to demonstrate a parser failure.
#include <open.mp>
#include <omp-postgresql>

new Database;
new bool:Started;
main() {}

public OnGameModeInit() {
	if (!pg_log_config(PG_LOG_DEBUG, PG_LOG_BOTH, "./logs/postgresql.log")) {
		ReportLocal("logging configuration");
	}
	Database = pg_connect("game", 1);
	if (!Database) ReportLocal("connection");
	return 1;
}

public OnGameModeExit() {
	if (Database) pg_connection_close(Database);
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection != Database || Started) return 1;
	if (connection_state == PG_CONNECTED) {
		Started = true;
		new statement = pg_prepare_file(Database, "accounts.sql", 0, true, "load_acount");
		if (!statement || !pg_add(statement)) ReportLocal("prepare/add");
		else if (!pg_execute(statement, 0, 0, 0, true, "ReportParser", "s", "load_acount")) {
			ReportLocal("submission");
		}
		if (statement) pg_statement_close(statement);
	} else if (connection_state == PG_CONNECTION_FAILED) {
		print("PG LOGGING FAILED: connection startup; inspect pg_connection_message securely");
	}
	return 1;
}

forward ReportParser(job, const requested[]);
public ReportParser(job, const requested[]) {
	new message[512];
	pg_job_message(job, message);
	printf("Requested section %s: %s", requested, message);
	if (pg_job_status(job) != PG_FAILED || pg_job_error(job) != PG_ERROR_FILE ||
		strfind(message, "accounts.sql: SQL section 'load_acount' not found") != 0) {
		print("PG LOGGING FAILED: unexpected parser diagnostic");
	} else {
		print("PG LOGGING PASS");
	}
	pg_job_close(job); // Every completed non-transaction job is explicitly released.
	pg_connection_close(Database);
	return 1;
}

stock ReportLocal(const operation[]) {
	new message[256];
	pg_last_error(message);
	printf("PG LOGGING FAILED: %s: %s", operation, message);
}
