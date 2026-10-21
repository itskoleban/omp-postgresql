# Transactions, transfers and atomic game operations

[transfer.pwn](../examples/transfer.pwn) creates two temporary bank accounts, moves 25 units,
and verifies balances 75/75 after COMMIT. It keeps every operation on lane 0; all fixtures
disappear when that physical session closes. This teaches transaction control without
modifying a live economy. In production use persistent tables, restricted roles and an
application operation identifier for reconciliation.

## A transaction owns a lane

`pg_begin` returns a job that is also the transaction handle. The lane is reserved as soon
as BEGIN is accepted; wait for its `PG_CONFIRMED` callback before submitting dependent SQL.
Pass that transaction to every query/execute/batch. It selects the reserved lane regardless
of the lane argument. Other transactions and unrelated jobs cannot use the reserved lane;
other lanes can progress, with no global ordering guarantee.

Keep the BEGIN job. COMMIT and ROLLBACK return **new** job handles. Once a finish is accepted,
no further work may be added to that transaction. After the finish callback, close its job
and the BEGIN job. Closing BEGIN early fails while it still reserves the lane; closing a
statement, a savepoint job or a query result does not end the transaction. Check local
rejection of every submission, including finish jobs. A reservation includes finishing
capacity but cannot make a failed/not-ready connection accept network work.

The transfer locks the two account rows in increasing ID order, confirms both exist, and
uses guarded parameterized UPDATEs. Check affected rows: zero is a declined business
operation, not a PostgreSQL error. Roll back rather than crediting after an unsuccessful
debit. A nonnegative CHECK constraint protects stored balances; a bigint intermediate
guards int32 addition. Real transfers also need authorization, positive amount validation,
different account IDs, currency/precision rules and consistent lock order across all writers.

## Commit a group only after its writes confirm

The following fragments form one callback chain, not a standalone gamemode. `Database`
is ready; the caller has prepared a statement on that same connection with one or more bounded UPDATE snapshots
as in [saving](saving.md), each expected to affect exactly one account. Calling
`StartAtomicSave(statement)` transfers ownership of that statement to this chain. Do not
submit it elsewhere. Run one chain at a time on this connection, and keep its identity
until completion/cleanup; the callbacks do not look up a possibly replaced `Database`.

<!-- pawn-example: transaction.start -->
```pawn
new LastTransactionError[512]; // Private diagnostic buffer, never sent to players.
new RetiredTransaction;       // Rejected finish awaiting connection shutdown.
forward OnSaveBegin(transaction, statement);
forward OnSaveWrites(job, transaction);
forward OnSaveFinish(job, transaction, bool:commit);

stock StartAtomicSave(statement) {
    new transaction = pg_begin(Database, 0, 0, true, "OnSaveBegin", "i", statement);
    if (!transaction) {
        pg_last_error(LastTransactionError);
        pg_statement_close(statement); // No SQL was accepted.
    }
    return transaction;
}
```

Only the confirmed BEGIN callback submits the captured writes. A locally rejected execute
has not sent them, so close the statement and explicitly roll back the open transaction.

<!-- pawn-example: transaction.begin -->
```pawn
public OnSaveBegin(transaction, statement) {
    if (pg_job_status(transaction) != PG_CONFIRMED) {
        printf("BEGIN not confirmed: status=%d error=%d",
            pg_job_status(transaction), pg_job_error(transaction));
        pg_statement_close(statement);
        pg_job_close(transaction); // Failed BEGIN releases its reservation at dispatch.
        return 1;
    }
    new job = pg_execute(statement, 0, transaction, 0, true, "OnSaveWrites", "i", transaction);
    if (!job) pg_last_error(LastTransactionError);
    pg_statement_close(statement); // Accepted jobs own the snapshots.
    if (!job) FinishAtomicSave(transaction, false);
    return 1; // Keep the BEGIN job until the finish callback.
}
```

Even confirmed SQL may update zero rows. Validate every result before choosing COMMIT;
any failed/cancelled/uncertain write or unexpected count chooses ROLLBACK instead.

<!-- pawn-example: transaction.writes -->
```pawn
public OnSaveWrites(job, transaction) {
    new count[24], expected = pg_job_size(job);
    new bool:commit = pg_job_status(job) == PG_CONFIRMED && expected > 0 &&
        pg_job_completed(job) == expected && pg_result_count(job) == expected;
    for (new result = 0; commit && result < expected; result++) {
        if (!pg_result_affected(job, result, count) || strcmp(count, "1")) commit = false;
    }
    if (!commit) printf("Writes rejected: status=%d error=%d", pg_job_status(job), pg_job_error(job));
    pg_job_close(job);
    FinishAtomicSave(transaction, commit);
    return 1;
}
```

The finish call only submits work. Report a committed save **only** in its confirmed
completion callback; a confirmed ROLLBACK means the save was discarded.

<!-- pawn-example: transaction.finish -->
```pawn
stock FinishAtomicSave(transaction, bool:commit) {
    new finish;
    if (commit) finish = pg_commit(transaction, 0, true, "OnSaveFinish", "ib", transaction, true);
    else finish = pg_rollback(transaction, 0, true, "OnSaveFinish", "ib", transaction, false);
    if (!finish) {
        pg_last_error(LastTransactionError);
        RetiredTransaction = transaction; // Retain ownership: no finish callback will arrive.
        pg_connection_close(pg_job_connection(transaction));
    }
}

public OnSaveFinish(job, transaction, bool:commit) {
    new status = pg_job_status(job);
    if (status == PG_CONFIRMED) {
        if (commit) print("Save committed.");
        else print("Save rolled back.");
    } else {
        printf("Finish not confirmed: status=%d error=%d", status, pg_job_error(job));
        // Preserve an application operation ID and reconcile an uncertain COMMIT; no replay.
    }
    pg_job_close(job);
    pg_job_close(transaction); // Finish dispatch has released the reservation.
    return 1;
}
```

For the rejected-finish branch, start a normal repeating application timer **before** calling
`StartAtomicSave` (for example, `SetTimer("OnTransactionCleanup", 1000, true)`), and keep
it until cleanup finishes. Its public can be this fragment:

<!-- pawn-example: transaction.cleanup -->
```pawn
forward OnTransactionCleanup();
public OnTransactionCleanup() {
    if (RetiredTransaction && pg_job_close(RetiredTransaction)) RetiredTransaction = 0;
    return 1; // One attempt per timer tick; never spin or sleep waiting for shutdown.
}
```

Closure can initially fail while the BEGIN still reserves the lane. The component keeps a
closed connection through its final `PG_DISCONNECTED` callback and reaps it on a later tick;
then the retained, already dispatched BEGIN can be released. Stop new work on that connection
after finish rejection. Do not replace/overwrite `RetiredTransaction` with another operation.
This example deliberately abandons the connection when a finish cannot be enqueued; it does
not claim a confirmed rollback or replay any work. Capture job messages/SQLSTATE in a private
diagnostic record before closure when needed; see [debugging](debugging.md). A real transfer
also needs the row locking, business validation and operation ledger described above.

## Errors, savepoints and rejected COMMIT

A confirmed UPDATE inside a transaction is provisional. COMMIT can still fail, for example
when a deferred constraint is checked. `PG_FAILED` with `PG_ERROR_SQL` and a retained
SQLSTATE means the finish was rejected. The engine reconciles an idle server session or
discards a session whose state is unsuitable for reuse. It never silently retries the work.

After an ordinary required operation fails, the transaction cannot simply commit its earlier
work. Finish with ROLLBACK, or recover with `pg_rollback_to` a previously confirmed savepoint.
The latter is demonstrated in [extensions.pwn](../examples/extensions.pwn): confirm BEGIN,
confirm savepoint, observe an expected SQL error, confirm rollback-to, release savepoint,
then continue/commit. Savepoint names follow the [native contract](native-reference.md).
The simple transfer tutorial closes its connection on an unexpected error, abandoning its
temporary transaction; production code should explicitly finish and retain reconciliation
state. Do not send BEGIN/COMMIT/SAVEPOINT as ordinary `pg_query` strings.

`pg_begin_ex` explicitly selects isolation/read-only policy. `pg_begin` inherits PostgreSQL
defaults. Serializable isolation may produce `40001`; only retry a whole logical operation
after a **known** failure and a deliberate application policy. A timeout/cancelled or lost
COMMIT response can be `PG_UNCERTAIN`. Query a persistent operation ledger using the same
operation ID before deciding what happened. An uncertain operation is not a confirmed
failure and must not be blindly replayed. A confirmed COMMIT's durability still depends on
PostgreSQL's `synchronous_commit`, storage and replication policy.

## Properties and clans

[bootstrap.pwn](../examples/bootstrap.pwn) demonstrates loading clans/properties/vehicles
and validating all rows before world activation. Loading data is different from purchasing
a property. For a persistent purchase, serialize/lock the buyer's balance and property row,
then use a guarded `UPDATE properties SET owner_id=$1 WHERE id=$2 AND owner_id IS NULL
RETURNING id` within the same transaction as the debit. Zero returned rows means another
buyer won or the ID is missing. Roll back the debit; do not decide ownership from an earlier
unlocked SELECT. Constraints and transaction policy must protect all writers, including
administrative tools. This SQL pattern is guidance, not an additional executed example or
a public plugin feature.
