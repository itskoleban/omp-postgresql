// Test fixtures only. Numeric lane samples and libpq connection errors contain
// no SQL, passwords or hashes. UTC/monotonic harness times correlate the logs.
forward QaLaneSnapshot();
new QaConnection, QaLanes, QaTimer;
new QaLastState[8], QaLastAttempts[8];

stock StartLaneDiagnostics(connection, lanes) {
	QaConnection = connection;
	QaLanes = lanes;
	for (new lane; lane < lanes; lane++) {
		QaLastState[lane] = -1;
		QaLastAttempts[lane] = -1;
	}
	QaTimer = SetTimer("QaLaneSnapshot", 100, true);
	QaLaneSnapshot();
}
stock StopLaneDiagnostics() {
	QaLaneSnapshot();
	KillTimer(QaTimer);
}
public QaLaneSnapshot() {
	for (new lane; lane < QaLanes; lane++) {
		new lane_state = pg_lane_state(QaConnection, lane), attempts[32];
		pg_lane_stat(QaConnection, lane, PG_STAT_CONNECTION_ATTEMPTS, attempts);
		new attempt = strval(attempts);
		if (lane_state == QaLastState[lane] && attempt == QaLastAttempts[lane]) continue;
		QaLastState[lane] = lane_state;
		QaLastAttempts[lane] = attempt;
		printf("QA event=lane tick_ms=%d lane=%d state=%d attempts=%d pending=%d active=%d",
			GetTickCount(), lane, lane_state, attempt, pg_lane_pending(QaConnection, lane), pg_lane_active(QaConnection, lane));
		// The default Pawn stack is smaller than this diagnostic buffer. Keep it
		// in fixture data storage, including when sampling during VM shutdown.
		static message[4097];
		if (pg_connection_message(QaConnection, lane, message) && message[0])
			printf("QA diagnostic tick_ms=%d lane=%d libpq=%s", GetTickCount(), lane, message);
	}
	return 1;
}
