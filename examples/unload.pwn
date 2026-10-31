#define FILTERSCRIPT
#include <open.mp>
#include <omp_postgresql>

new Database;
forward OnUnloadCustom(job, const text[]);

main() {}

public OnFilterScriptInit() {
	Database = pg_connect("game");
	return 1;
}

public OnFilterScriptExit() {
	print("PG UNLOAD EXIT");
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection == Database && connection_state == PG_CONNECTED) {
		new statement = pg_prepare(Database, "SELECT pg_sleep(3)");
		if (!statement || !pg_add(statement) || !pg_execute(statement, 0, 0, 0, true,
			"OnUnloadCustom", "s", "owned until unload")) {
			print("PG UNLOAD FAILED");
			return 1;
		}
		pg_statement_close(statement);
		print("PG UNLOAD PENDING");
	}
	return 1;
}

public OnPgComplete(job, context) {
#pragma unused context
	pg_job_close(job);
	print("PG UNLOAD CALLBACK");
	return 1;
}

public OnUnloadCustom(job, const text[]) {
#pragma unused text
	pg_job_close(job);
	print("PG UNLOAD CALLBACK");
	return 1;
}
