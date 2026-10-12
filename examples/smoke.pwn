#include <open.mp>
#include <omp-postgresql>
#pragma dynamic 8192

new Database;
new Transaction;
new Started;
enum {
	STEP_BEGIN = 1,
	STEP_CREATE,
	STEP_INSERT,
	STEP_BULK,
	STEP_COMMIT,
	STEP_DONE
};

main() {}

public OnGameModeInit() {
	Database = pg_connect("game", 2);
	if (!Database) {
		ReportLocalError();
	}
	return 1;
}

public OnGameModeExit() {
	if (Database) {
		pg_connection_close(Database);
	}
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection != Database) {
		return 1;
	}
	if (connection_state == PG_CONNECTED && !Started) {
		Started = 1;
		Transaction = pg_begin(Database, 0, STEP_BEGIN);
		if (!Transaction) {
			ReportLocalError();
		}
	}
	if (connection_state == PG_CONNECTION_FAILED) {
		new message[4097];
		pg_connection_message(connection, 0, message);
		// Test mode only. Production diagnostics can contain sensitive information.
		printf("PG SMOKE CONNECTION FAILED: %s", message);
	}
	return 1;
}

public OnPgComplete(job, context) {
	if (pg_job_status(job) != PG_CONFIRMED) {
		new message[4097];
		pg_job_message(job, message);
		printf("PG SMOKE FAILED step=%d status=%d: %s", context, pg_job_status(job), message);
		if (job != Transaction) {
			pg_job_close(job);
		}
		pg_connection_close(Database);
		return 1;
	}
	switch (context) {
		case STEP_BEGIN: {
			SubmitSql(
			    "CREATE TEMP TABLE omp_pg_smoke(id integer PRIMARY KEY, money integer) ON COMMIT DROP",
			    STEP_CREATE);
		}
		case STEP_CREATE: {
			new statement =
			    pg_prepare(Database, "INSERT INTO omp_pg_smoke VALUES($1,$2) RETURNING id", 2);
			for (new i = 1; i <= 20; i++) {
				if (!pg_bind_int(statement, 1, i) || !pg_bind_int(statement, 2, i * 10) ||
				    !pg_add(statement)) {
					ReportLocalError();
					pg_statement_close(statement);
					return 1;
				}
			}
			if (!pg_execute(statement, 0, Transaction, STEP_INSERT)) {
				ReportLocalError();
			}
			pg_statement_close(statement); // Accepted jobs own their SQL and parameters.
		}
		case STEP_INSERT: {
			if (pg_job_completed(job) != 20 || pg_result_count(job) != 20) {
				print("PG SMOKE FAILED: expected 20 executions");
				pg_connection_close(Database);
				return 1;
			}
			new ids[20], money[20];
			for (new i = 0; i < sizeof(ids); i++) {
				ids[i] = i + 1;
				money[i] = 500 + i;
			}
			new statement = pg_prepare(
			    Database,
			    "UPDATE omp_pg_smoke AS a SET money=v.money FROM unnest($1::int[], $2::int[]) AS v(id,money) WHERE a.id=v.id RETURNING a.id",
			    2);
			if (!pg_bind_int_array(statement, 1, ids) || !pg_bind_int_array(statement, 2, money) ||
			    !pg_add(statement) || !pg_execute(statement, 0, Transaction, STEP_BULK)) {
				ReportLocalError();
			}
			pg_statement_close(statement);
		}
		case STEP_BULK: {
			if (pg_job_completed(job) != 1 || pg_result_rows(job) != 20) {
				print("PG SMOKE FAILED: expected one bulk execution / 20 rows");
				pg_connection_close(Database);
				return 1;
			}
			if (!pg_commit(Transaction, STEP_COMMIT)) {
				ReportLocalError();
			}
		}
		case STEP_COMMIT: {
			pg_job_close(Transaction);
			Transaction = 0;
			print("PG SMOKE PASS");
			pg_connection_close(Database);
		}
	}
	if (job != Transaction) {
		pg_job_close(job);
	}
	return 1;
}

stock SubmitSql(const sql[], context) {
	new statement = pg_prepare(Database, sql, 0, false);
	if (!statement || !pg_add(statement) || !pg_execute(statement, 0, Transaction, context)) {
		ReportLocalError();
	}
	if (statement) {
		pg_statement_close(statement);
	}
}

stock ReportLocalError() {
	new message[256];
	pg_last_error(message);
	printf("PG SMOKE LOCAL FAILURE: %s", message);
}
