// First query tutorial. Uses session-local data; see docs/getting-started.md.
#include <open.mp>
#include <omp_postgresql>

enum { CREATE = 1, INSERT, SELECT, UPDATE, DELETE, SQL_ERROR };
new Database;
new bool:Started;
new bool:Failed;

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
	if (connection != Database || Failed) return 1;
	if (connection_state == PG_CONNECTED && !Started) {
		Started = true;
		Submit("CREATE TEMP TABLE tutorial_basic(id integer PRIMARY KEY, score integer NOT NULL)", CREATE);
	} else if (connection_state == PG_CONNECTION_FAILED) Fail("connection failed");
	return 1;
}

public OnPgComplete(job, context) {
	if (Failed) {
		pg_job_close(job);
		return 1;
	}
	if (context == SQL_ERROR) {
		new sqlstate[6];
		if (pg_job_status(job) != PG_FAILED || pg_job_error(job) != PG_ERROR_SQL ||
			pg_result_count(job) != 1 || !pg_result_sqlstate(job, 0, sqlstate) || strcmp(sqlstate, "22012")) {
			Fail("expected division-by-zero diagnostic");
		} else {
			print("PG BASIC PASS");
			pg_connection_close(Database);
		}
	} else if (pg_job_status(job) != PG_CONFIRMED) {
		// A job handle proves acceptance only. Production logs must filter diagnostics.
		Fail("SQL did not confirm; inspect this job before closing it");
	} else {
		switch (context) {
			case CREATE: Submit("INSERT INTO tutorial_basic VALUES(1, 10) RETURNING id", INSERT);
			case INSERT: {
				new id;
				if (!pg_result_int(job, 0, 0, 0, id) || id != 1) Fail("INSERT RETURNING differs");
				else Submit("SELECT id, score FROM tutorial_basic ORDER BY id", SELECT);
			}
			case SELECT: {
				new score, column = pg_result_column(job, 0, "score");
				if (pg_result_rows(job) != 1 || column < 0 ||
					!pg_result_int(job, 0, 0, column, score) || score != 10) Fail("SELECT differs");
				else Submit("UPDATE tutorial_basic SET score=11 WHERE id=1", UPDATE);
			}
			case UPDATE: {
				new count[24];
				if (!pg_result_affected(job, 0, count) || strcmp(count, "1")) Fail("UPDATE count differs");
				else Submit("DELETE FROM tutorial_basic WHERE id=1 RETURNING score", DELETE);
			}
			case DELETE: {
				new score;
				if (!pg_result_int(job, 0, 0, 0, score) || score != 11) Fail("DELETE differs");
				else Submit("SELECT 1/0", SQL_ERROR);
			}
		}
	}
	if (!pg_job_close(job)) Fail("completed job was not released");
	return 1;
}

stock Submit(const sql[], context) {
	// These are trusted literal queries. Bind every user-controlled value instead.
	if (!pg_query(Database, sql, 0, 0, context)) Fail("submission rejected");
}

stock Fail(const reason[]) {
	new detail[256];
	pg_last_error(detail); // Read before any other native clears the local diagnostic.
	printf("PG BASIC FAILED: %s (%s)", reason, detail);
	Failed = true;
	if (Database) pg_connection_close(Database);
}
