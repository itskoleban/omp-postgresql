// Isolated regression gamemode. The sleep and divide-by-zero below are deliberate:
// an asynchronous callback must not replace main()'s suspended continuation.
#include <open.mp>
#include <omp_postgresql>
#pragma dynamic 8192

#define CROSS_COUNT (24)
#define CROSS_CONTEXT (100)

enum {
	STEP_BASE = 1,
	STEP_SLEEP,
	STEP_FAULT,
	STEP_AFTER_FAULT,
	STEP_VERIFY
};

new Database;
new Started;
new Failed;
new Finishing;
new MainCycles;
new SleepEntered;
new SleepResumed;
new FaultEntered;
new FaultResumed;
new FaultDivisor;
new CrossJobs[CROSS_COUNT];
new bool:CrossSeen[CROSS_COUNT];
new bool:CrossClosed[CROSS_COUNT];
new CrossCallbacks;
new CrossClosures;
new CustomTransaction;
new CustomRows;
new CustomSleepResumed;
new CustomFaultResumed;

forward OnCustomCopied(job, first, second, Float:number, bool:flag, const text[]);
forward OnCustomEmpty(job);
forward OnCustomFailure(job);
forward OnCustomSleep(job);
forward OnCustomFault(job);
forward OnCustomTx(job, phase);

forward StartCrossClose();
forward Watchdog();

main() {
	for (;;) {
		sleep 5;
		MainCycles++;
	}
}

public OnGameModeInit() {
	Database = pg_connect("game", 2);
	if (!Database) {
		Fail("connection was rejected");
	}
	SetTimer("Watchdog", 10000, false);
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
		Started = 1;
		Submit(STEP_BASE);
	} else if (connection_state == PG_CONNECTION_FAILED) {
		Fail("connection failed");
	} else if (connection_state == PG_DISCONNECTED) {
		if (!Finishing || pg_connection_state(connection) != PG_DISCONNECTED) {
			Fail("final connection callback has an invalid handle");
			return 1;
		}
		Database = 0;
		print("PG CALLBACK PASS");
	}
	return 1;
}

stock bool:CheckResult(job) {
	new value;
	if (pg_job_status(job) != PG_CONFIRMED || pg_result_count(job) != 1 ||
	    pg_result_rows(job) != 1 || !pg_result_int(job, 0, 0, 0, value) || value != 1) {
		Fail("callback result or handle is invalid");
		return false;
	}
	return true;
}

public OnPgComplete(job, context) {
	if (Failed) {
		return 1;
	}
	if (context >= CROSS_CONTEXT && context < CROSS_CONTEXT + CROSS_COUNT) {
		new index = context - CROSS_CONTEXT;
		if (CrossClosed[index] || CrossSeen[index] || job != CrossJobs[index]) {
			Fail("closed or duplicate job received a callback");
			return 1;
		}
		if (!CheckResult(job)) {
			return 1;
		}
		CrossSeen[index] = true;
		CrossCallbacks++;
		if (!pg_job_close(job)) {
			Fail("current job could not be closed");
			return 1;
		}
		// A successful close before the other callback proves both terminations
		// were collected by the same main-thread dispatch.
		for (new i = 0; i < CROSS_COUNT; i++) {
			if (CrossSeen[i] || CrossClosed[i]) {
				continue;
			}
			if (pg_job_status(CrossJobs[i]) == PG_CONFIRMED && pg_job_close(CrossJobs[i])) {
				CrossClosed[i] = true;
				CrossClosures++;
			}
		}
		if (CrossCallbacks + CrossClosures == CROSS_COUNT) {
			if (!CrossClosures) {
				Fail("no completions shared a tick; lower the test server tick rate");
				return 1;
			}
			Submit(STEP_VERIFY);
		}
		return 1;
	}

	if (!CheckResult(job)) {
		return 1;
	}
	if (!pg_job_close(job)) {
		Fail("completed job could not be closed");
		return 1;
	}
	switch (context) {
		case STEP_BASE: {
			Submit(STEP_SLEEP);
		}
		case STEP_SLEEP: {
			SleepEntered = 1;
			Submit(STEP_FAULT);
			sleep 1;
			SleepResumed = 1;
			Fail("unsupported callback continuation resumed");
		}
		case STEP_FAULT: {
			if (!SleepEntered || SleepResumed) {
				Fail("callback sleep replaced the main continuation");
				return 1;
			}
			FaultEntered = 1;
			Submit(STEP_AFTER_FAULT);
			new value = 10 / FaultDivisor; // Expected AMX_ERR_DIVIDE.
			printf("Unexpected divide result: %d", value);
			FaultResumed = 1;
			Fail("expected callback runtime fault did not occur");
		}
		case STEP_AFTER_FAULT: {
			if (!FaultEntered || FaultResumed || SleepResumed) {
				Fail("callback state was not restored after an error");
				return 1;
			}
			SetTimer("StartCrossClose", 100, false);
		}
		case STEP_VERIFY: {
			if (MainCycles < 2 || SleepResumed || FaultResumed) {
				Fail("main continuation did not survive asynchronous callbacks");
				return 1;
			}
			StartCustom();
		}
		default: {
			Fail("unexpected callback context");
		}
	}
	return 1;
}

stock StartCustom() {
	new first = 42, second = -7, Float:number = 2.5, bool:flag = true;
	new text[] = "copied argument";
	if (!pg_query(Database, "SELECT 1::integer", 0, 0, -999, true,
	    "OnCustomCopied", "iifbs", first, second, number, flag, text)) {
		Fail("custom callback submission rejected");
	}
	first = 0; second = 0; number = 0.0; flag = false; text[0] = EOS;
	if (first || second || number != 0.0 || flag || text[0]) Fail("source overwrite failed");
}

public OnCustomCopied(job, first, second, Float:number, bool:flag, const text[]) {
	if (numargs() != 6 || first != 42 || second != -7 || number != 2.5 ||
	    !flag || strcmp(text, "copied argument") || !CheckResult(job)) {
		Fail("custom copied arguments changed");
	}
	if (!pg_job_close(job) || !pg_query(Database, "SELECT 1::integer", .callback = "OnCustomEmpty")) {
		Fail("custom empty callback submission failed");
	}
	return 1;
}

public OnCustomEmpty(job) {
	if (numargs() != 1 || !CheckResult(job) || !pg_job_close(job) ||
	    !pg_query(Database, "SELECT 1/0", 0, 0, 0, true, "OnCustomFailure")) {
		Fail("custom no-argument callback failed");
	}
	return 1;
}

public OnCustomFailure(job) {
	if (pg_job_status(job) != PG_FAILED || pg_job_error(job) != PG_ERROR_SQL ||
	    !pg_job_close(job) || !pg_query(Database, "SELECT 1::integer", 0, 0, 0, true, "OnCustomSleep")) {
		Fail("SQL failure did not reach custom callback");
	}
	return 1;
}

public OnCustomSleep(job) {
	if (!CheckResult(job) || !pg_job_close(job) ||
	    !pg_query(Database, "SELECT 1::integer", 0, 0, 0, true, "OnCustomFault")) {
		Fail("custom sleep setup failed");
	}
	sleep 1; // Deliberate regression: this continuation must be discarded.
	CustomSleepResumed = 1;
	Fail("custom callback sleep resumed");
	return 1;
}

public OnCustomFault(job) {
	if (!CheckResult(job) || !pg_job_close(job) || CustomSleepResumed ||
	    !pg_begin_ex(Database, PG_READ_COMMITTED, false, 0, 0, true, "OnCustomTx", "i", 0)) {
		Fail("custom fault setup failed");
	}
	new value = 10 / FaultDivisor; // Deliberate AMX_ERR_DIVIDE.
	printf("Unexpected custom divide result: %d", value);
	CustomFaultResumed = 1;
	return 1;
}

public OnPgRows(job, context) {
	if (context != -123) {
		Fail("unexpected stream row context");
		return 1;
	}
	while (pg_stream_next(job)) {
		CustomRows += pg_result_rows(job);
		if (!pg_stream_release(job)) Fail("custom stream block release failed");
	}
	return 1;
}

public OnCustomTx(job, phase) {
	if (CustomSleepResumed || CustomFaultResumed || pg_job_status(job) != PG_CONFIRMED) {
		Fail("custom transaction operation failed");
		return 1;
	}
	if (phase == 0 || phase == 8) CustomTransaction = job;
	else if (!pg_job_close(job)) Fail("custom transaction job close failed");
	new next;
	switch (phase) {
		case 0: next = pg_savepoint(CustomTransaction, "custom", 0, true, "OnCustomTx", "i", 1);
		case 1: next = pg_rollback_to(CustomTransaction, "custom", 0, true, "OnCustomTx", "i", 2);
		case 2: next = pg_release_savepoint(CustomTransaction, "custom", 0, true, "OnCustomTx", "i", 3);
		case 3, 4, 5: {
			new stmt = pg_prepare(Database, "SELECT generate_series(1,3)::integer");
			if (!stmt || !pg_add(stmt)) { Fail("custom statement staging failed"); return 1; }
			if (phase == 3) next = pg_execute(stmt, 0, CustomTransaction, 0, true, "OnCustomTx", "i", 4);
			else if (phase == 4) {
				new batch = pg_batch_create(Database);
				if (!batch || !pg_batch_add(batch, stmt)) { Fail("custom batch staging failed"); return 1; }
				next = pg_batch_execute(batch, 0, CustomTransaction, 0, true, "OnCustomTx", "i", 5);
				if (!pg_batch_close(batch)) Fail("custom batch close failed");
			} else next = pg_stream_execute(stmt, 1, 0, CustomTransaction, -123, true, "OnCustomTx", "i", 6);
			if (!pg_statement_close(stmt)) Fail("custom statement close failed");
		}
		case 6: {
			if (CustomRows != 3) Fail("custom stream lost rows");
			next = pg_commit(CustomTransaction, 0, true, "OnCustomTx", "i", 7);
		}
		case 7: {
			if (!pg_job_close(CustomTransaction)) Fail("custom committed BEGIN close failed");
			next = pg_begin(Database, 0, 0, true, "OnCustomTx", "i", 8);
		}
		case 8: next = pg_rollback(CustomTransaction, 0, true, "OnCustomTx", "i", 9);
		case 9: {
			if (!pg_job_close(CustomTransaction)) Fail("custom rolled back BEGIN close failed");
			Finishing = 1;
			next = pg_connection_close(Database);
		}
	}
	if (!next) Fail("custom operation submission failed");
	return 1;
}

public StartCrossClose() {
	if (Failed) {
		return 1;
	}
	for (new i = 0; i < CROSS_COUNT; i++) {
		CrossJobs[i] = Submit(CROSS_CONTEXT + i, i % 2);
		if (!CrossJobs[i]) {
			return 1;
		}
	}
	return 1;
}

public Watchdog() {
	if (Database && !Failed) {
		Fail("test timed out");
	}
	return 1;
}

stock Submit(context, lane = 0) {
	new statement = pg_prepare(Database, "SELECT 1::integer AS value");
	if (!statement) {
		Fail("statement preparation was rejected");
		return 0;
	}
	new job;
	if (pg_add(statement)) {
		job = pg_execute(statement, lane, 0, context);
	}
	if (!job) {
		Fail("query was rejected");
	}
	pg_statement_close(statement);
	return job;
}

stock Fail(const reason[]) {
	if (Failed) {
		return;
	}
	Failed = 1;
	new detail[256];
	pg_last_error(detail);
	printf("PG CALLBACK FAILED: %s (%s)", reason, detail);
}
