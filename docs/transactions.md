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
