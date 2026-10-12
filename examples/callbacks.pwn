// Isolated regression gamemode. The sleep and divide-by-zero below are deliberate:
// an asynchronous callback must not replace main()'s suspended continuation.
#include <open.mp>
#include <omp-postgresql>
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
			Finishing = 1;
			if (!pg_connection_close(Database)) {
				Fail("connection close was rejected");
			}
		}
		default: {
			Fail("unexpected callback context");
		}
	}
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
