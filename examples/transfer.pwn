// A small bank transfer, using a reserved lane and constraints. See docs/transactions.md.
#include <open.mp>
#include <omp_postgresql>

enum { CREATE = 1, SEED, BEGIN, LOCK, DEBIT, CREDIT, COMMIT, VERIFY, ROLLBACK };
new Database;
new Transaction;
new bool:Started;
new bool:Failed;
forward bool:OneAffected(job);

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
		Accept(pg_query(Database, "CREATE TEMP TABLE tutorial_bank(id integer PRIMARY KEY, money integer NOT NULL CHECK(money>=0))", 0, 0, CREATE));
	} else if (connection_state == PG_CONNECTION_FAILED) Fail("connection failed");
	return 1;
}

public OnPgComplete(job, context) {
	new status = pg_job_status(job);
	if (context == COMMIT || context == ROLLBACK) {
		// Finish has been dispatched: both job handles can now be released.
		if (Transaction) pg_job_close(Transaction);
		Transaction = 0;
		if (context == COMMIT && status == PG_CONFIRMED) {
			Accept(pg_query(Database, "SELECT id,money FROM tutorial_bank ORDER BY id", 0, 0, VERIFY));
		} else Fail("finish not confirmed; reconcile an uncertain COMMIT instead of retrying");
	} else if (Failed || status != PG_CONFIRMED) {
		Fail("transaction operation failed");
	} else switch (context) {
		case CREATE: Accept(pg_query(Database, "INSERT INTO tutorial_bank VALUES(1,100),(2,50)", 0, 0, SEED));
		case SEED: {
			Transaction = pg_begin(Database, 0, BEGIN);
			Accept(Transaction);
		}
		case BEGIN: {
			// Deterministic lock order reduces opposite-direction transfer deadlocks.
			Accept(pg_query(Database, "SELECT id FROM tutorial_bank WHERE id IN(1,2) ORDER BY id FOR UPDATE", 0, Transaction, LOCK));
		}
		case LOCK: {
			if (pg_result_rows(job) != 2) Rollback("account missing");
			else ChangeBalance(1, -25, DEBIT);
		}
		case DEBIT: {
			if (!OneAffected(job)) Rollback("insufficient funds or missing sender");
			else ChangeBalance(2, 25, CREDIT);
		}
		case CREDIT: {
			if (!OneAffected(job)) Rollback("recipient missing or balance limit");
			else Accept(pg_commit(Transaction, COMMIT));
		}
		case VERIFY: {
			new sender, recipient;
			if (pg_result_rows(job) != 2 || !pg_result_int(job, 0, 0, 1, sender) ||
				!pg_result_int(job, 0, 1, 1, recipient) || sender != 75 || recipient != 75) Fail("transfer differs");
			else {
				print("PG TRANSFER PASS");
				pg_connection_close(Database);
			}
		}
	}
	if (job != Transaction) pg_job_close(job); // Retain BEGIN until finishing.
	return 1;
}

stock ChangeBalance(id, delta, context) {
	new statement = pg_prepare(Database,
		"UPDATE tutorial_bank SET money=money+$2 WHERE id=$1 AND money::bigint+$2 BETWEEN 0 AND 2147483647", 2);
	if (!statement || !pg_bind_int(statement, 1, id) || !pg_bind_int(statement, 2, delta) || !pg_add(statement)) Fail("balance snapshot rejected");
	else Accept(pg_execute(statement, 0, Transaction, context));
	if (statement) pg_statement_close(statement);
}

stock bool:OneAffected(job) {
	new count[24];
	return pg_result_affected(job, 0, count) && !strcmp(count, "1");
}

stock Rollback(const reason[]) {
	printf("Transfer declined: %s", reason);
	Accept(pg_rollback(Transaction, ROLLBACK));
}

stock Accept(job) {
	if (!job) Fail("submission rejected");
}

stock Fail(const reason[]) {
	new detail[256];
	pg_last_error(detail);
	printf("PG TRANSFER FAILED: %s (%s)", reason, detail);
	Failed = true;
	// Abandoning this disposable tutorial closes the session, rolling back live work.
	if (Database) pg_connection_close(Database);
}
