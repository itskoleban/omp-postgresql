// Qualification fixture only: real Pawn VM, two PostgreSQL lanes, bounded samples.
// GetTickCount has millisecond resolution. Callback latency is end-to-end; timer
// lateness is scheduling delay, never the component's tick execution time.
#include <open.mp>
#include <omp_postgresql>
#pragma dynamic 16384

#define JOBS (64)
#define CASES (12)
#define REPETITIONS (7)
#define TIMER_SAMPLES (128)
#define PROBE_INTERVAL (10)

new Database;
new bool:Started;
new bool:Failed;
new Round;
new Order;
new Case;
new Level;
new Destination;
new BatchStart;
new BatchSubmission;
new Completed;
new Submitted[JOBS];
new Submission[JOBS];
new Latency[JOBS];
new Handles[JOBS];
new bool:Seen[JOBS];
new TimerLateness[TIMER_SAMPLES];
new TimerCount;
new TimerDue;
new ProbeTimer;
new bool:Probing;
new const Levels[] = {PG_LOG_OFF, PG_LOG_WARN, PG_LOG_DEBUG, PG_LOG_TRACE};

forward BeginBatch();
forward FinishBatch();
forward TimerProbe();
forward Watchdog();
main() {}

public OnGameModeInit() {
	pg_log_config(PG_LOG_OFF);
	Database = pg_connect("game", 2);
	if (!Database) Fail("connect");
	SetTimer("Watchdog", 60000, false);
	return 1;
}

public OnGameModeExit() {
	if (ProbeTimer) KillTimer(ProbeTimer);
	if (Database) pg_connection_close(Database);
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection != Database || Failed) return 1;
	if (connection_state == PG_CONNECTED && !Started) {
		Started = true;
		SetTimer("BeginBatch", 100, false);
	} else if (connection_state == PG_CONNECTION_FAILED) Fail("connection startup");
	return 1;
}

public BeginBatch() {
	if (Failed) return 1;
	// A fixed cyclic permutation balances case order across repetitions.
	Case = (Order + Round * 5) % CASES;
	Level = Levels[Case / 3];
	Destination = Case % 3;
	new path[64];
	if (Destination != PG_LOG_CONSOLE) {
		format(path, sizeof path, "./logs/pg-perf-%d-%d.log", Level, Destination);
	}
	if (!pg_log_config(Level, Destination, path)) return Fail("log configuration");
	Completed = TimerCount = 0;
	for (new i; i < JOBS; i++) Seen[i] = false;
	printf("PG_PERF_BEGIN,%d,%d,%d,%d", Case, Level, Destination, Round);
	Probing = true;
	TimerDue = GetTickCount() + PROBE_INTERVAL;
	ProbeTimer = SetTimer("TimerProbe", PROBE_INTERVAL, false);
	BatchStart = GetTickCount();
	for (new i; i < JOBS; i++) {
		Submitted[i] = GetTickCount();
		// Same repeatable mixed workload for every mode: 56 successes and eight
		// division-by-zero errors, so WARN also has actual diagnostic events.
		if (i % 8 == 0) {
			Handles[i] = pg_query(Database, "SELECT 1 / 0", i % 2, 0, i);
		} else {
			Handles[i] = pg_query(Database, "SELECT 1", i % 2, 0, i);
		}
		Submission[i] = GetTickCount() - Submitted[i];
		if (!Handles[i]) return Fail("submission");
	}
	BatchSubmission = GetTickCount() - BatchStart;
	return 1;
}

public OnPgComplete(job, context) {
	new entered = GetTickCount();
	if (Failed) {
		pg_job_close(job);
		return 1;
	}
	if (context < 0 || context >= JOBS || Seen[context] || Handles[context] != job) {
		return Fail("callback identity");
	}
	Latency[context] = entered - Submitted[context];
	Seen[context] = true;
	if (context % 8 == 0) {
		new sqlstate[6];
		if (pg_job_status(job) != PG_FAILED || pg_job_error(job) != PG_ERROR_SQL ||
			pg_result_count(job) != 1 || !pg_result_sqlstate(job, 0, sqlstate) ||
			strcmp(sqlstate, "22012")) return Fail("expected SQL error");
	} else {
		new value;
		if (pg_job_status(job) != PG_CONFIRMED || pg_result_count(job) != 1 ||
			!pg_result_int(job, 0, 0, 0, value) || value != 1) return Fail("result");
	}
	if (!pg_job_close(job)) return Fail("job close");
	Completed++;
	// Keep timer probes alive for another 100 ms. Printing is deferred until
	// every measured callback has entered and all job handles have been closed.
	if (Completed == JOBS) SetTimer("FinishBatch", 100, false);
	return 1;
}

public TimerProbe() {
	ProbeTimer = 0;
	if (!Probing || Failed) return 1;
	if (TimerCount == TIMER_SAMPLES) return Fail("timer sample bound");
	new late = GetTickCount() - TimerDue;
	TimerLateness[TimerCount++] = late > 0 ? late : 0;
	TimerDue = GetTickCount() + PROBE_INTERVAL;
	ProbeTimer = SetTimer("TimerProbe", PROBE_INTERVAL, false);
	return 1;
}

public FinishBatch() {
	if (Failed) return 1;
	Probing = false;
	if (ProbeTimer) KillTimer(ProbeTimer);
	ProbeTimer = 0;
	if (!TimerCount) return Fail("no timer samples");
	printf("PG_PERF,batch,%d,%d,%d,%d,0,%d,%d,0", Case, Level, Destination,
		Round, BatchSubmission, GetTickCount() - BatchStart);
	for (new i; i < JOBS; i++) {
		printf("PG_PERF,job,%d,%d,%d,%d,%d,%d,%d,0", Case, Level, Destination,
			Round, i, Submission[i], Latency[i]);
	}
	for (new i; i < TimerCount; i++) {
		printf("PG_PERF,timer,%d,%d,%d,%d,%d,0,0,%d", Case, Level, Destination,
			Round, i, TimerLateness[i]);
	}
	Order++;
	if (Order == CASES) {
		Order = 0;
		Round++;
	}
	if (Round > REPETITIONS) {
		pg_log_config(PG_LOG_OFF);
		pg_connection_close(Database);
		Database = 0;
		print("PG PERFORMANCE PASS");
	} else SetTimer("BeginBatch", 100, false);
	return 1;
}

public Watchdog() {
	return Fail("watchdog");
}

stock Fail(const operation[]) {
	Failed = true;
	printf("PG PERFORMANCE FAILED: %s", operation);
	return 1;
}
