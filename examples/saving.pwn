// Three save strategies, variable array counts, immutable snapshots and file loading.
#include <open.mp>
#include <omp-postgresql>

enum { CREATE = 1, SEED, INDIVIDUAL, SNAPSHOTS, BULK, VERIFY };
new Database;
new SaveStatement;
new bool:Started;
new bool:Failed;
new SaveTimer;
new bool:SnapshotsDue;
forward bool:Stage(id, score);
forward PeriodicSave();

main() {}

public OnGameModeInit() {
	Database = pg_connect("game");
	if (!Database) Fail("connect rejected");
	else SaveTimer = SetTimer("PeriodicSave", 250, true);
	return 1;
}

public OnGameModeExit() {
	KillTimer(SaveTimer);
	if (SaveStatement) pg_statement_close(SaveStatement);
	if (Database) pg_connection_close(Database);
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection != Database || Failed) return 1;
	if (connection_state == PG_CONNECTED && !Started) {
		Started = true;
		Accept(pg_query(Database, "CREATE TEMP TABLE tutorial_saves(id integer PRIMARY KEY, score integer NOT NULL)", 0, 0, CREATE));
	} else if (connection_state == PG_CONNECTION_FAILED) Fail("connection failed");
	return 1;
}

public OnPgComplete(job, context) {
	if (Failed) {
		pg_job_close(job);
		return 1;
	}
	if (pg_job_status(job) != PG_CONFIRMED) {
		// Earlier snapshots can have committed. Never replay an uncertain save blindly.
		Fail("save failed; reconcile account IDs and captured versions");
	} else switch (context) {
		case CREATE: Accept(pg_query(Database, "INSERT INTO tutorial_saves VALUES(1,0),(2,0),(3,0)", 0, 0, SEED));
		case SEED: {
			SaveStatement = pg_prepare(Database, "UPDATE tutorial_saves SET score=$2 WHERE id=$1", 2);
			if (!SaveStatement || !Stage(1, 10)) Fail("individual snapshot rejected");
			else Accept(pg_execute(SaveStatement, 0, 0, INDIVIDUAL));
		}
		case INDIVIDUAL: {
			SnapshotsDue = true; // The timer submits one bounded wave after this callback.
		}
		case SNAPSHOTS: {
			if (pg_job_completed(job) != 3 || pg_result_count(job) != 3) Fail("expected three SQL executions");
			else {
				// Arrays have capacity 4, but only count=3 entries belong to this save.
				new ids[4] = {1,2,3,0}, scores[4] = {100,200,300,0};
				new statement = pg_prepare(Database,
					"UPDATE tutorial_saves AS a SET score=v.score FROM unnest($1::integer[],$2::integer[]) AS v(id,score) WHERE a.id=v.id", 2);
				if (!statement || !pg_bind_int_array(statement, 1, ids, 3) ||
					!pg_bind_int_array(statement, 2, scores, 3) || !pg_add(statement)) Fail("bulk parameters rejected");
				else Accept(pg_execute(statement, 0, 0, BULK));
				if (statement) pg_statement_close(statement);
			}
		}
		case BULK: {
			new affected[24];
			if (pg_job_completed(job) != 1 || !pg_result_affected(job, 0, affected) || strcmp(affected, "3")) Fail("bulk count differs");
			else {
				new statement = pg_prepare_file(Database, "tutorial-select.sql");
				if (!statement || !pg_add(statement)) Fail("file statement rejected");
				else Accept(pg_execute(statement, 0, 0, VERIFY));
				if (statement) pg_statement_close(statement);
			}
		}
		case VERIFY: {
			if (pg_result_rows(job) != 3) Fail("save row count differs");
			for (new row = 0; row < 3 && !Failed; row++) {
				new id, score;
				if (!pg_result_int(job, 0, row, 0, id) || !pg_result_int(job, 0, row, 1, score) ||
					id != row+1 || score != id*100) Fail("saved data differs");
			}
			if (!Failed) {
				print("PG SAVING PASS");
				KillTimer(SaveTimer);
				pg_connection_close(Database);
			}
		}
	}
	pg_job_close(job);
	return 1;
}

public PeriodicSave() {
	if (!SnapshotsDue || Failed || pg_connection_state(Database) != PG_CONNECTED) return 1;
	SnapshotsDue = false; // No duplicate wave while the accepted save is in flight.
	// Use account IDs copied before disconnect, never a later player's slot data.
	for (new id = 1; id <= 3; id++) {
		if (!Stage(id, id * 20)) {
			Fail("snapshot rejected");
			break;
		}
	}
	if (!Failed) Accept(pg_execute(SaveStatement, 0, 0, SNAPSHOTS));
	return 1;
}

stock bool:Stage(id, score) {
	return pg_bind_int(SaveStatement, 1, id) && pg_bind_int(SaveStatement, 2, score) && pg_add(SaveStatement);
}

stock Accept(job) {
	if (!job) Fail("submission rejected");
}

stock Fail(const reason[]) {
	new detail[256];
	pg_last_error(detail);
	printf("PG SAVING FAILED: %s (%s)", reason, detail);
	Failed = true;
	KillTimer(SaveTimer);
	if (Database) pg_connection_close(Database);
}
