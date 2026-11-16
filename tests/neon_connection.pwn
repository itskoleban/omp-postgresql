#include <open.mp>
#include <omp_postgresql>

new Connection;
new PoolSize = 1;
new ReadyAt;
new Results;
new Backend[2];
new bool:Submitted;

main() {}

public OnGameModeInit() {
	pg_log_config(PG_LOG_OFF);
	StartPool();
	return 1;
}

public OnGameModeExit() {
	ClosePool();
	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (!Connection || connection != Connection) return 1;
	if (connection_state == PG_CONNECTION_FAILED) {
		Fail("connection");
	} else if (connection_state == PG_CONNECTED && !Submitted) {
		if (pg_connection_ready(connection) != PoolSize) {
			Fail("readiness");
			return 1;
		}
		Submitted = true;
		printf("PG NEON READY lanes=%d elapsed_ms=%d", PoolSize, GetTickCount() - ReadyAt);
		for (new lane; lane < PoolSize; ++lane) {
			if (!pg_query(connection, "SELECT 1, pg_backend_pid()", lane, 0, lane)) {
				Fail("submission");
				break;
			}
		}
	}
	return 1;
}

public OnPgComplete(job, context) {
	if (!Connection || pg_job_connection(job) != Connection) {
		pg_job_close(job);
		return 1;
	}
	new answer;
	new bool:ok = context >= 0 && context < PoolSize && Backend[context] == 0 &&
		pg_job_status(job) == PG_CONFIRMED && pg_result_rows(job) == 1 &&
		pg_result_int(job, 0, 0, 0, answer) && answer == 1 &&
		pg_result_int(job, 0, 0, 1, Backend[context]) && Backend[context] > 0;
	ok = pg_job_close(job) && ok;
	if (!ok) {
		Fail("query");
	} else if (++Results == PoolSize) {
		if (PoolSize == 2 && Backend[0] == Backend[1]) {
			Fail("backend identity");
			return 1;
		}
		ClosePool();
		printf("PG NEON PASS lanes=%d", PoolSize);
		if (PoolSize == 1) {
			PoolSize = 2;
			StartPool();
		}
	}
	return 1;
}

stock StartPool() {
	Results = 0;
	Backend[0] = Backend[1] = 0;
	Submitted = false;
	ReadyAt = GetTickCount();
	Connection = pg_connect("game", PoolSize);
	if (!Connection) Fail("request");
}

stock ClosePool() {
	if (Connection) pg_connection_close(Connection);
	Connection = 0;
}

stock Fail(const phase[]) {
	printf("PG NEON FAIL lanes=%d phase=%s", PoolSize, phase);
	ClosePool();
}
