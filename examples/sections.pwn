// Copy sql/accounts.sql to scriptfiles/sql/accounts.sql in a disposable test server.
// All temporary fixtures and this transaction belong to lane 0.
#include <open.mp>
#include <omp_postgresql>
#pragma dynamic 8192

enum {
	STEP_BEGIN = 1,
	STEP_CREATE,
	STEP_INSERT,
	STEP_LOAD,
	STEP_SAVE,
	STEP_BULK,
	STEP_VERIFY,
	STEP_COMMIT
};

new Database;
new Transaction;
new LoadStatement;
new bool:Started;

main() {}

public OnGameModeInit() {
	Database = pg_connect("game", 1);
	if (!Database) Fail("connection rejected");
	return 1;
}

public OnGameModeExit() {
	if (LoadStatement) pg_statement_close(LoadStatement);
	if (Database) pg_connection_close(Database);
	// VM unload collects pending jobs; a disconnected session cannot commit this transaction.
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection != Database) return 1;
	if (connection_state == PG_CONNECTED && !Started) {
		Started = true;
		Transaction = pg_begin(Database, 0, STEP_BEGIN);
		if (!Transaction) Fail("BEGIN rejected");
	} else if (connection_state == PG_CONNECTION_FAILED) {
		Fail("connection failed");
	}
	return 1;
}

public OnPgComplete(job, context) {
	if (pg_job_status(job) != PG_CONFIRMED) {
		new message[4097];
		pg_job_message(job, message);
		printf("PG SECTION FAILED step=%d status=%d: %s", context, pg_job_status(job), message);
		if (job != Transaction) pg_job_close(job);
		pg_connection_close(Database);
		return 1;
	}
	switch (context) {
		case STEP_BEGIN: {
			SubmitSql("CREATE TEMP TABLE accounts(id integer PRIMARY KEY, username text UNIQUE, money integer, score integer) ON COMMIT DROP", STEP_CREATE);
		}
		case STEP_CREATE: {
			SubmitSql("INSERT INTO accounts VALUES(1,'alice',0,0),(2,'bob',0,0)", STEP_INSERT);
		}
		case STEP_INSERT: {
			LoadStatement = pg_prepare_file(Database, "accounts.sql", 1, .section = "load_account");
			if (!LoadStatement || !pg_bind_string(LoadStatement, 1, "alice") || !pg_add(LoadStatement)) {
				Fail("load snapshot rejected");
			} else if (!pg_execute(LoadStatement, 0, Transaction, STEP_LOAD)) {
				Fail("load rejected");
			}
		}
		case STEP_LOAD: {
			new id;
			if (pg_result_rows(job) != 1 || !pg_result_int(job, 0, 0, 0, id) || id != 1) {
				Fail("load result differs");
				return 1;
			}
			// Select another named statement through the same unified file interface.
			new statement = pg_prepare_file(Database, "accounts.sql", 3, true, "save_account");
			if (!statement || !pg_bind_int(statement, 1, 77) || !pg_bind_int(statement, 2, 9) ||
				!pg_bind_int(statement, 3, id) || !pg_add(statement) ||
				!pg_execute(statement, 0, Transaction, STEP_SAVE)) Fail("individual save rejected");
			if (statement) pg_statement_close(statement);
		}
		case STEP_SAVE: {
			new affected[24];
			if (!pg_result_affected(job, 0, affected) || strcmp(affected, "1")) {
				Fail("individual save count differs");
				return 1;
			}
			new ids[] = {1, 2}, money[] = {100, 200}, score[] = {10, 20};
			new statement = pg_prepare_file(Database, "accounts.sql", 3, true, "save_all_accounts");
			if (!statement || !pg_bind_int_array(statement, 1, ids) ||
				!pg_bind_int_array(statement, 2, money) || !pg_bind_int_array(statement, 3, score) ||
				!pg_add(statement) || !pg_execute(statement, 0, Transaction, STEP_BULK)) Fail("bulk save rejected");
			if (statement) pg_statement_close(statement);
		}
		case STEP_BULK: {
			new affected[24];
			if (pg_job_completed(job) != 1 || !pg_result_affected(job, 0, affected) || strcmp(affected, "2")) {
				Fail("bulk save count differs");
				return 1;
			}
			// Reuse the logical statement; two snapshots are two independent SQL executions.
			if (!pg_bind_string(LoadStatement, 1, "alice") || !pg_add(LoadStatement) ||
				!pg_bind_string(LoadStatement, 1, "bob") || !pg_add(LoadStatement) ||
				!pg_execute(LoadStatement, 0, Transaction, STEP_VERIFY)) Fail("verification rejected");
		}
		case STEP_VERIFY: {
			if (pg_result_count(job) != 2) {
				Fail("expected two results");
				return 1;
			}
			for (new i = 0; i < 2; i++) {
				new money, score;
				if (pg_result_rows(job, i) != 1 || !pg_result_int(job, i, 0, 2, money) ||
					!pg_result_int(job, i, 0, 3, score) || money != (i + 1) * 100 || score != (i + 1) * 10) {
					Fail("captured account values differ");
					return 1;
				}
			}
			pg_statement_close(LoadStatement);
			LoadStatement = 0;
			if (!pg_commit(Transaction, STEP_COMMIT)) Fail("COMMIT rejected");
		}
		case STEP_COMMIT: {
			pg_job_close(Transaction);
			Transaction = 0;
			print("PG SECTION PASS");
			pg_connection_close(Database);
		}
	}
	if (job != Transaction) pg_job_close(job);
	return 1;
}

stock SubmitSql(const sql[], context) {
	if (!pg_query(Database, sql, 0, Transaction, context)) Fail("fixture query rejected");
}

stock Fail(const reason[]) {
	new message[256];
	pg_last_error(message);
	printf("PG SECTION FAILED: %s (%s)", reason, message);
	if (Database) pg_connection_close(Database);
}
