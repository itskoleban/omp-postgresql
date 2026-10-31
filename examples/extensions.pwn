// Regression gamemode for the additive API; uses only queries and temporary state.
#include <open.mp>
#include <omp_postgresql>
#pragma dynamic 16384

enum {
	STEP_QUERY = 1,
	STEP_BATCH,
	STEP_BEGIN,
	STEP_SAVEPOINT,
	STEP_EXPECT_FAILURE,
	STEP_RECOVER,
	STEP_RELEASE,
	STEP_COMMIT,
	STEP_STREAM
};

new Database;
new Transaction;
new bool:Started;
new bool:Failed;
new StreamRows;
new StreamSum;
forward ExtensionWatchdog();

main() {}

public OnGameModeInit() {
	Database = pg_connect("game", 2);
	if (!Database) {
		Fail("connect was rejected");
	}
	SetTimer("ExtensionWatchdog", 15000, false);
	return 1;
}

public OnGameModeExit() {
	if (Database) {
		pg_connection_close(Database);
	}
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection != Database || Failed) {
		return 1;
	}
	if (connection_state == PG_CONNECTED && !Started) {
		Started = true;
		Accept(pg_query(Database, "SELECT 42 AS answer", 0, 0, STEP_QUERY));
	} else if (connection_state == PG_CONNECTION_FAILED) {
		Fail("connection failed");
	}
	return 1;
}

stock Fail(const message[]) {
	new detail[256];
	pg_last_error(detail);
	if (detail[0]) {
		printf("PG EXTENSION DETAIL: %s", detail);
	}
	Failed = true;
	printf("PG EXTENSION FAILED: %s", message);
	if (Database) {
		pg_connection_close(Database);
	}
}

stock Accept(job) {
	if (!job) {
		new message[256];
		pg_last_error(message);
		Fail(message);
	}
}

stock SubmitBatch() {
	new batch = pg_batch_create(Database);
	new statement = pg_prepare(Database,
	    "SELECT $1::bytea AS payload, ($2::bigint[])[1]::text AS exact_value, ($2::bigint[])[2] IS NULL AS absent", 2);
	new bytes[] = {0, 127, 128, 255};
	new invalid[] = {256};
	new values[72], nulls[] = {0, 1, 0};
	// Pass each slot's capacity explicitly; sizeof on an indexed cell is not its width.
	strcat(values, "9223372036854775807", 24);
	strcat(values[48], "-9223372036854775808", 24);
	if (!batch || !statement || pg_bind_bytes(statement, 1, invalid) ||
	    !pg_bind_bytes(statement, 1, bytes) ||
	    !pg_bind_array(statement, 2, values, 3, 24, PG_INT64, nulls) ||
	    !pg_add(statement) || !pg_batch_add(batch, statement)) {
		Fail("typed batch construction failed");
	} else {
		pg_statement_close(statement);
		statement = pg_prepare(Database, "SELECT 42 AS answer");
		if (!statement || !pg_add(statement) || !pg_batch_add(batch, statement)) {
			Fail("second batch item failed");
		} else {
			Accept(pg_batch_execute(batch, 0, 0, STEP_BATCH));
		}
	}
	if (statement) {
		pg_statement_close(statement);
	}
	if (batch) {
		pg_batch_close(batch);
	}
}

stock Drain(job) {
	// Bound work per tick; a pending block triggers another OnPgRows callback.
	for (new block = 0; block < 4 && pg_stream_next(job); block++) {
		new rows = pg_result_rows(job);
		if (rows > 32 || pg_result_column_type(job, 0, 0) != PG_INT) {
			Fail("stream block metadata is invalid");
			return;
		}
		for (new row = 0; row < rows; row++) {
			new value;
			if (!pg_result_int(job, 0, row, 0, value)) {
				Fail("stream row cannot be read");
				return;
			}
			StreamRows++;
			StreamSum += value;
		}
		pg_stream_release(job);
	}
}

public OnPgRows(job, context) {
	if (context == STEP_STREAM && !Failed) {
		Drain(job);
	}
	return 1;
}

public OnPgComplete(job, context) {
	if (Failed) {
		return 1;
	}
	if (context == STEP_EXPECT_FAILURE) {
		new sqlstate[6];
		if (pg_job_status(job) != PG_FAILED || pg_job_error_index(job) != 0 ||
		    pg_job_item_status(job, 0) != PG_ITEM_FAILED ||
		    !pg_result_sqlstate(job, 0, sqlstate) || strcmp(sqlstate, "22012")) {
			Fail("expected SQL failure was not identified");
		} else {
			Accept(pg_rollback_to(Transaction, "recover", STEP_RECOVER));
		}
		pg_job_close(job);
		return 1;
	}
	if (pg_job_status(job) != PG_CONFIRMED) {
		Fail("an extension job failed");
		return 1;
	}
	switch (context) {
		case STEP_QUERY: {
			new value, name[16], latency[32];
			if (pg_job_size(job) != 1 || pg_job_error_index(job) != -1 ||
			    !pg_result_int(job, 0, 0, 0, value) || value != 42 ||
			    !pg_result_column_name(job, 0, 0, name) || strcmp(name, "answer") ||
			    !pg_job_latency(job, latency)) {
				Fail("query shortcut or metadata failed");
			} else {
				SubmitBatch();
			}
		}
		case STEP_BATCH: {
			new bytes[4], written, exact[24], bool:absent;
			new small[2] = {55, 66}, unchanged = 99;
			if (pg_job_size(job) != 2 || pg_result_count(job) != 2 ||
			    pg_job_item_status(job, 1) != PG_ITEM_CONFIRMED ||
			    pg_result_bytes(job, 0, 0, 0, small, unchanged) || small[0] != 55 ||
			    small[1] != 66 || unchanged != 99 ||
			    !pg_result_bytes(job, 0, 0, 0, bytes, written) || written != 4 ||
			    bytes[0] != 0 || bytes[2] != 128 || bytes[3] != 255 ||
			    !pg_result_string(job, 0, 0, 1, exact) || strcmp(exact, "9223372036854775807") ||
			    !pg_result_bool(job, 0, 0, 2, absent) || !absent) {
				Fail("typed arrays or binary output validation failed");
			} else {
				Transaction = pg_begin_ex(Database, PG_REPEATABLE_READ, false, 0, STEP_BEGIN);
				Accept(Transaction);
			}
		}
		case STEP_BEGIN: {
			Accept(pg_savepoint(Transaction, "recover", STEP_SAVEPOINT));
		}
		case STEP_SAVEPOINT: {
			Accept(pg_query(Database, "SELECT 1/0", 0, Transaction, STEP_EXPECT_FAILURE));
		}
		case STEP_RECOVER: {
			Accept(pg_release_savepoint(Transaction, "recover", STEP_RELEASE));
		}
		case STEP_RELEASE: {
			Accept(pg_commit(Transaction, STEP_COMMIT));
		}
		case STEP_COMMIT: {
			pg_job_close(Transaction);
			Transaction = 0;
			new statement = pg_prepare(Database, "SELECT i FROM generate_series(1,257) i");
			if (!statement || !pg_add(statement)) {
				Fail("stream construction failed");
			} else {
				Accept(pg_stream_execute(statement, 32, 0, 0, STEP_STREAM));
			}
			if (statement) {
				pg_statement_close(statement);
			}
		}
		case STEP_STREAM: {
			Drain(job);
			new total[32];
			if (StreamRows != 257 || StreamSum != 33153 ||
			    !pg_lane_stat(Database, 0, PG_STAT_ROWS_RECEIVED, total) || strval(total) < 257) {
				Fail("stream completion or statistics failed");
			} else {
				print("PG EXTENSION PASS");
				pg_connection_close(Database);
			}
		}
	}
	if (job != Transaction) {
		pg_job_close(job);
	}
	return 1;
}

public ExtensionWatchdog() {
	if (StreamRows != 257 && !Failed) {
		Fail("watchdog expired");
	}
	return 1;
}
