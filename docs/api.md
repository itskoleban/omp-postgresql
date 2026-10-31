# Pawn API contract 0.1

[omp_postgresql.inc](../pawn/omp_postgresql.inc) is the authoritative source of signatures and
default arguments. Handles are positive; 0 is invalid. Each handle belongs to the VM that created
it and is never recycled. Do not share handles between scripts. All Pawn calls return immediately:
they validate, copy, enqueue, or read memory. Callbacks run on the main thread when a tick
processes completed work.

Use the [per-native reference](native-reference.md) for all 69 exact signatures, defaults,
argument limits, return/error sentinels, ownership rules and call examples. This guide
explains how those operations fit together.

For a first working query, begin with [getting started](getting-started.md). The
[example catalogue](examples.md) links practical flows and their external prerequisites.

Asynchronous callbacks must return without `sleep`. If a callback attempts to suspend, the
component reports the error, discards that continuation, and restores the VM's previous state.
An already suspended `main()` retains its continuation. Do not keep references to local
variables for use on another tick.

## Return values and local errors

Handle-creating and job-submitting natives return a positive handle or 0. Boolean operations
return true/false. `pg_job_status`, `pg_connection_state`, `pg_lane_state`,
`pg_job_item_status`, `pg_job_error_index`, and `pg_result_column` return -1 on error; the column
lookup also returns -1 when no column matches. Counters return 0 on error, so read
`pg_last_error` to distinguish an error from a valid zero.

Every native except `pg_last_error` clears the previous local error. Read it immediately.
Rejection does not consume bindings or a pending batch. Insufficient output buffers fail
without truncating or partially modifying the output.

`pg_last_error(output,size)` copies the most recent local diagnostic. Asynchronous errors are
available through the job instead. Text output always uses unpacked Pawn strings. `size` is
measured in cells, includes the terminator, and must be between 1 and 2097152. The complete
value must fit. Text input accepts packed or unpacked strings containing UTF-8 bytes.
The Pawn output limit is independent of `MAX_RESULT_BYTES`; increasing the engine limit
does not increase the size of a Pawn output buffer.

## Connections

| Native                                               | Contract                                                                                    |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| `pg_connect(profile,pool_size=1)`                    | Service name of up to 128 alphanumeric, `_`, or `-` bytes. Returns before connecting.       |
| `pg_connection_state(connection)`                    | DISCONNECTED, CONNECTING, CONNECTED, RECONNECTING, CONNECTION_FAILED, or CLOSING.           |
| `pg_connection_ready(connection)`                    | Number of connected physical sessions. Jobs require the entire pool to be ready.            |
| `pg_connection_message(connection,lane,output,size)` | Last lane diagnostic; empty after connecting. Filter sensitive content before logging.      |
| `pg_connection_close(connection)`                    | Starts shutdown, rejects new jobs, and cancels pending work. Does not join workers in Pawn. |
| `pg_lane_state(connection,lane)`                     | Physical session state; validates the zero-based lane index.                                |
| `pg_lane_pending(connection,lane)`                   | Approximate number of queued jobs.                                                          |
| `pg_lane_active(connection,lane)`                    | Approximate running-job count, 0 or 1.                                                      |
| `pg_lane_stat(connection,lane,metric,output,size)`   | Cumulative `PG_STAT_*` counter as a decimal string.                                         |

Statistics count jobs, SQL executions, and rows separately. `PG_STAT_SQL_COMMANDS` counts
execution dispatches, excluding preparation, and is not a confirmation count. Atomic samples
can describe slightly different instants while work is running. Counters reset with the connection.

`OnPgConnection(connection,connection_state)` reports changes observed on a tick. Transient
states between ticks may be coalesced. After reconnection attempts are exhausted, close the
failed connection and create a new one to retry. Callbacks are not looked up in other VMs.

The handle remains valid during its final `PG_DISCONNECTED` callback and is collected on the
next tick. Closing a connection does not release its owner's statements or retained results;
call `pg_statement_close` and `pg_job_close` when those objects are no longer needed.
The identifier returned by `pg_job_connection` may refer to a connection already collected.

## Prepare, bind, add, and execute

`pg_prepare(connection,sql,parameters=0,prepared=true)` creates a logical statement. Declare
the exact parameter count, from 0 to 64. PostgreSQL validates `$1..$N` placeholders when the
statement executes. Each execution contains one SQL statement.

With `prepared=true`, each session keeps its most recent unnamed prepared statement and
reuses it when SQL and OIDs match. Preparing different SQL replaces it, limiting retained plans
to one per lane. Reconnection or direct execution invalidates that preparation.
`prepared=false` uses PQsendQueryParams for SQL that is better executed without a retained plan.

A physical preparation attempt replaces the previous unnamed preparation even if it fails;
the component clears its session cache before the attempt. SQLSTATE `0A000` or `26000` from
prepared execution also invalidates the cache. The current job fails without retrying.
The next explicit execution prepares the SQL again. This neither replays a write automatically
nor guarantees that the underlying SQL problem has been resolved.

`pg_prepare_file` returns the same kind of logical statement, but reads the SQL file in the
worker when execution reaches it. A homogeneous job shares one read across its snapshots;
a heterogeneous job reads each ordinary file operation separately. Unlike named inputs,
ordinary file operations have no job-wide preflight. Reading and canonicalization run in
the worker. Paths must be relative `.sql` paths under `scriptfiles/sql`. Files must be UTF-8;
an initial BOM is accepted.
File errors appear in the job. This is not a migration system.

With a nonempty `section`, `pg_prepare_file(connection,filename,parameters=0,prepared=true,section="")`
selects one named SQL statement from a file. Its 1..64-byte case-sensitive ASCII name is validated
locally; reading, full-file lexical validation and extraction happen in the worker.
It returns a statement handle or 0; owner, bind/add/execute, plan reuse and close rules
match the other prepare functions. File/section errors are deferred job diagnostics.
Each job captures its SQL once; later jobs reload it. Heterogeneous jobs capture named
inputs before their first dispatch. See [named SQL sections](sql-sections.md) for the
exact marker grammar, limits, errors, string setting, batching and runnable examples.

`pg_query(connection,sql,lane,transaction,context,notify)` submits one zero-parameter SQL
operation directly, without a public statement handle. Use binders for untrusted values.
Neither this shortcut nor a prepared statement accepts multiple commands in one SQL string.

Binding indices start at 1. Rebinding replaces that value before `pg_add`.
`pg_add` requires every parameter, copies an immutable snapshot, clears all temporary bindings,
and fixes the statement's parameter types. NULL requires an explicit OID; a missing binding
is not NULL. Parameter types cannot change between batches of the same statement.
Bindings are never executed implicitly without `pg_add`.

| Native                 | Representation                                                                                       |
| ---------------------- | ---------------------------------------------------------------------------------------------------- |
| `pg_bind_int`          | int32, OID 23.                                                                                       |
| `pg_bind_int64`        | Strict signed decimal int64 string covering the full range, OID 20.                                  |
| `pg_bind_float`        | Finite IEEE float32 Pawn Float, OID 700; no double-precision guarantee.                              |
| `pg_bind_bool`         | 0/1, OID 16.                                                                                         |
| `pg_bind_null`         | Explicit supported OID, listed below.                                                                |
| `pg_bind_string`       | UTF-8 without NUL; PG_TEXT by default, plus exact numeric and temporal types, UUID, JSONB, or BYTEA. |
| `pg_bind_int_array`    | Pawn array with explicit count or sizeof, up to 16384 elements.                                      |
| `pg_bind_string_array` | Flat unpacked slots with count and width in cells, up to 262144 cells total.                         |
| `pg_bind_bytes`        | BYTEA from integer cells in 0..255, including embedded zero bytes.                                   |
| `pg_bind_array`        | Flat textual slots, scalar element OID, and a separate 0/1 NULL mask.                                |

All sixteen supported scalar types and their array types accept explicit NULL. See the
[type and precision contract](types.md) for the full binder/OID mapping, binary output,
nullable array layout, and server normalization rules.

BYTEA uses PostgreSQL hexadecimal input such as `\x00ff...`, rather than text containing NUL.
PostgreSQL validates UUID and JSONB values. Arrays use PostgreSQL's text parameter format,
with quotes and backslashes escaped. `"NULL"` in a text array is text, not SQL NULL.
The original int/string array binders do not support individual NULL elements; use
`pg_bind_array` for nullable elements and `pg_bind_null` for an entire NULL array.

`pg_execute(statement,lane=0,transaction=0,context=0,notify=true)` requires a nonempty pending
batch. Acceptance consumes the batch and returns a job. The statement can immediately be
reused or closed. A batch of N snapshots executes N FIFO SQL statements, stops at the first
error, and is not atomic outside a transaction. `notify=false` suppresses only the callback;
execution remains asynchronous.

`pg_statement_close(statement)` releases the logical object, temporary bindings, and any
unsent batch. It returns true on success. A missing handle or one owned by another VM returns
false with a local error. Accepted jobs own their SQL and parameters and continue after the
statement closes. Physical preparation belongs to the session and follows the replacement
and invalidation rules above.

`for` and `foreach` loops use the same binders and `pg_add` for each item. The component does
not depend on YSI: a gamemode's `foreach (new playerid : Player)` can use the same body as
smoke.pwn's `for`. To execute a single bulk statement, build arrays in the loop and bind them
once. Check matching lengths before binding: UNNEST pads shorter arrays with NULL, and the
component does not infer SQL-specific length rules. With duplicate IDs in UPDATE FROM,
PostgreSQL may select one of several source rows. Deduplicate first or define the behavior
explicitly in SQL.

### Insert and delete one record

These fragments use `<open.mp>` and `<omp_postgresql>`, a ready `Database` connection,
and the `ReportLocalPgError` / `InspectPgFailure` application helpers shown in
[diagnostics](debugging.md#inspect-local-and-job-errors). They are functions to integrate
into a gamemode, not a complete program. As the schema owner, create this illustrative
table in a disposable database and grant the runtime role the required table/sequence
permissions; it is separate from the authentication tutorial's schema:

<!-- sql-example: crud.schema -->

```sql
CREATE TABLE tutorial_inline_accounts (
    id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    username text NOT NULL UNIQUE,
    score integer NOT NULL DEFAULT 0
);
```

Bind a username and score as values, including when the username contains quotes:

<!-- pawn-example: crud.insert -->

```pawn
stock InsertAccount(const username[], score) {
    new statement = pg_prepare(Database,
        "INSERT INTO tutorial_inline_accounts(username, score) VALUES($1, $2) RETURNING id", 2);
    new job;
    if (statement && pg_bind_string(statement, 1, username) &&
        pg_bind_int(statement, 2, score) && pg_add(statement)) {
        job = pg_execute(statement, .callback = "OnAccountChanged");
    }
    if (!job) ReportLocalPgError(); // Copy before close clears the local diagnostic.
    if (statement) pg_statement_close(statement);
    return job; // Positive means accepted, not inserted yet.
}
```

Delete by a stable database ID after application authorization. Do not use a player-slot
number as an account ID. This uses the same completion handler because both statements
return the affected account's `id`:

<!-- pawn-example: crud.delete -->

```pawn
stock DeleteAccount(accountid) {
    new statement = pg_prepare(Database,
        "DELETE FROM tutorial_inline_accounts WHERE id=$1 RETURNING id", 1);
    new job;
    if (statement && pg_bind_int(statement, 1, accountid) && pg_add(statement)) {
        job = pg_execute(statement, .callback = "OnAccountChanged");
    }
    if (!job) ReportLocalPgError();
    if (statement) pg_statement_close(statement);
    return job;
}
```

Each call owns a one-shot statement. Closing it after rejection discards unsent bindings
and snapshots; closing it after acceptance leaves the job's copies intact. A failed bind
never submits a partial operation. The callback checks both affected rows and returned data:

<!-- pawn-example: crud.result -->

```pawn
forward OnAccountChanged(job);
public OnAccountChanged(job) {
    if (pg_job_status(job) == PG_CONFIRMED) {
        new affected[24], accountid;
        if (pg_result_count(job) == 1 && pg_result_affected(job, 0, affected) &&
            !strcmp(affected, "1") && pg_result_rows(job) == 1 &&
            pg_result_columns(job) == 1 && !pg_result_is_null(job, 0, 0, 0) &&
            pg_result_int(job, 0, 0, 0, accountid)) {
            printf("Confirmed database change for account %d", accountid);
        } else {
            print("SQL confirmed, but the expected single account was not returned.");
        }
    } else {
        InspectPgFailure(job); // Includes FAILED, CANCELLED and UNCERTAIN; no replay.
    }
    pg_job_close(job);
    return 1;
}
```

A DELETE matching no account is successful SQL with count `"0"`; it is not proof that the
requested account existed. `RETURNING` supplies data through ordinary result readers,
as specified by PostgreSQL's [INSERT](https://www.postgresql.org/docs/17/sql-insert.html)
and [DELETE](https://www.postgresql.org/docs/17/sql-delete.html) commands. These callbacks
report database facts only. Before applying a result to a player, use the
[session-guarded SELECT](callbacks.md); for UPDATE and statement reuse see
[individual and bulk saves](saving.md). Submit dependent work from the prior callback,
not immediately after receiving its job handle.

## Heterogeneous batches

`pg_batch_create(connection)` creates an empty batch handle. `pg_batch_add(batch,statement)`
transfers every added snapshot from a statement on that same connection, preserving its SQL,
prepared/direct mode, parameters, and order. The statement keeps its fixed parameter types
and can be reused or closed. An empty source, wrong owner/connection, or limit violation
fails without consuming either object.

`pg_batch_execute(batch,lane,transaction,context,notify)` submits all flattened operations
as one job and consumes them only on acceptance. Enqueue rejection preserves the batch.
`pg_batch_close(batch)` releases unsent operations without affecting accepted jobs. Live
statements and batch handles share `MAX_STATEMENTS`; count and byte limits apply to each
batch. SQL copies and operation objects also count toward heterogeneous batch bytes.

A batch containing twenty snapshots followed by two different statements has twenty-two
operations and at most twenty-two results, with one completion callback. Operations stop
at the first failure. Earlier operations may already have committed outside an explicit
transaction; a batch itself does not provide atomicity.

## Jobs and results

For a job with `notify=true` and an empty custom callback name, `OnPgComplete(job,context)`
is dispatched once while the VM remains loaded and the public exists. A supplied custom
callback replaces `OnPgComplete`; see [custom completion handlers](callbacks.md).
Closing another completed job before its callback runs suppresses that notification.
Context is a copied cell, not a pointer. For players, use a session token and validate it in
the callback; a player ID may have been reused. Unloading the AMX cancels and collects its
resources without callbacks. Workers never retain references to Pawn memory.

States are QUEUED (accepted), RUNNING, CONFIRMED (successful response), FAILED (known failure),
UNCERTAIN (sent without a definitive response, or interrupted result), and CANCELLED
(cancellation observed before the current operation sent SQL). A batch can become CANCELLED
between statements after earlier items confirmed; that status does not undo their effects.
Within a transaction, CONFIRMED means the statement succeeded, **not that it is durable**;
wait for COMMIT confirmation. COMMIT durability also depends on PostgreSQL configuration,
including `synchronous_commit`, replication, and storage. Reconcile uncertain writes using
an idempotency key or version check; never replay them blindly.

| Native                  | Contract                                                                          |
| ----------------------- | --------------------------------------------------------------------------------- |
| `pg_job_status`         | Available while a job is pending.                                                 |
| `pg_job_connection`     | Logical connection identifier, also available while pending.                      |
| `pg_job_completed`      | Statements with successful responses before failure; not a row count.             |
| `pg_job_size`           | Total operation count, available while pending; unchanged after completion.       |
| `pg_job_item_status`    | Terminal per-operation `PG_ITEM_*` status; unattempted items remain NOT_EXECUTED. |
| `pg_job_error_index`    | Zero-based interrupted/failing operation, or -1 if none is identified.            |
| `pg_job_error`          | General error category, `PG_ERROR_*`.                                             |
| `pg_job_message`        | Explanation of the general job failure.                                           |
| `pg_job_duration`       | Execution microseconds as a decimal string, excluding queue wait.                 |
| `pg_job_queue_time`     | Acceptance to worker start, in microseconds as a decimal string.                  |
| `pg_job_latency`        | Acceptance to completion dispatch, in microseconds; requires dispatch.            |
| `pg_result_count`       | Available statement results, including a failed SQL result.                       |
| `pg_result_rows`        | Row count of the selected result.                                                 |
| `pg_result_columns`     | Column count of the selected result.                                              |
| `pg_result_column`      | First column matching the name, or -1. Prefer unique aliases.                     |
| `pg_result_column_name` | Exact selected column name.                                                       |
| `pg_result_column_type` | PostgreSQL result OID; SQL casts can change it.                                   |
| `pg_result_is_null`     | Detects SQL NULL.                                                                 |
| `pg_result_string`      | Text preserving BIGINT/NUMERIC precision. SQL NULL fails.                         |
| `pg_result_bytes`       | Decode a BYTEA hex result into cells; no partial writes or truncation.            |
| `pg_result_int`         | Strict int32 conversion; overflow and NULL return false.                          |
| `pg_result_float`       | Finite float32 conversion; higher-precision data may be rounded.                  |
| `pg_result_bool`        | Accepts only PostgreSQL's t/f representation.                                     |
| `pg_result_affected`    | Affected rows as a decimal string; 0 if the command has no count.                 |
| `pg_result_sqlstate`    | PostgreSQL SQLSTATE for the selected statement.                                   |
| `pg_result_message`     | PostgreSQL diagnostic for the selected statement; may contain sensitive data.     |
| `pg_job_close`          | Releases the job and all results after its terminal completion is dispatched.     |
| `pg_cancel`             | Requests cancellation; does not guarantee that no effects occurred.               |

`pg_cancel(job)` returns true when marking a pending or running job. It returns false without
an error if the job is already terminal; an invalid handle returns false with a local error.
If the worker observes cancellation before starting, the job becomes `PG_CANCELLED` without
sending SQL. Cancellation between statements can also produce `PG_CANCELLED`, with earlier
items still confirmed. Cancellation during an unconfirmed SQL dispatch may produce
`PG_UNCERTAIN`. Inspect item statuses and completed counts rather than inferring that the
whole job did nothing. A job may still become `PG_CONFIRMED`
if it finishes before observing the request. If the worker cancels a required transaction
operation, a subsequent COMMIT cannot report success; roll back or finish the transaction.

Result, row, and column indices start at zero. Scalar readers modify their output reference
only on success. `RETURNING` is a normal result; there is no universal insert_id.
A SELECT returning zero rows still has columns. Commands with no returned rows have zero columns.
Retained results count toward the job limit. Without callbacks, close the job after the tick
that dispatches its terminal completion. Do not close the BEGIN job while its transaction
still reserves the lane.

Except for status, connection, size, and selected streaming blocks, job/result inspection
requires terminal publication. A local or transport failure need not have a PostgreSQL
result or SQLSTATE. Inspect the job category and error index before accessing result entries.

## Transactions

`pg_begin(connection,lane,context,notify)` reserves the lane immediately and returns a job
handle that also identifies the transaction. Wait for CONFIRMED before using it with
`pg_execute`. Execution uses the BEGIN lane and ignores the supplied lane argument.
External jobs targeting that lane are rejected; other lanes continue working.
Jobs queued before BEGIN on the same lane execute first.

`pg_commit(transaction,context,notify)` and `pg_rollback` return new jobs. Once accepted, they
prevent further operations in that transaction. COMMIT after a required failure executes
ROLLBACK and reports FAILED/PG_ERROR_TRANSACTION. Once completion is dispatched, close
both the final job and the BEGIN job.

The total transaction deadline abandons the physical session. PostgreSQL rolls back an open,
uncommitted transaction when it detects the disconnect; this is not a confirmed ROLLBACK.
Dispatching a finish job releases the logical reservation. A lost connection during COMMIT
may leave its result UNCERTAIN.

PostgreSQL can reject COMMIT itself, for example with a deferred unique/foreign-key constraint,
even after every preceding statement was confirmed. That finish job is FAILED/PG_ERROR_SQL
with the original SQLSTATE; its writes are not committed. If PostgreSQL reports the session
idle, the worker clears its transaction ownership and the lane is reusable after finish
dispatch, without replay or an unnecessary reconnect. A rejected finish that leaves another
session state discards the physical connection before recovery. This differs from a lost
COMMIT response, whose effects can remain UNCERTAIN.

`pg_begin` inherits the session's default isolation and read-only settings.
`pg_begin_ex(connection,isolation,read_only,lane,context,notify)` explicitly selects
PG_READ_COMMITTED, PG_REPEATABLE_READ, or PG_SERIALIZABLE and READ ONLY/READ WRITE.

`pg_savepoint(transaction,name,context,notify)`, `pg_rollback_to`, and
`pg_release_savepoint` submit separate jobs on the reserved lane. Names have 1..63 ASCII
letters, digits, or underscores, starting with a letter or underscore; they are quoted as
identifiers. Wait for confirmation before dependent work. After a SQL error, rollback to
a previously confirmed savepoint can restore the transaction; ordinary queries and savepoint
creation/release are rejected while it requires recovery. A failed rollback-to does not
clear that state. Closing a savepoint job does not release the transaction reservation.

Transaction-control SQL, COPY, LISTEN/UNLISTEN, DO, and CALL are rejected through ordinary
query/statement execution. Prepared transactions/2PC, COPY transfer, and receiving
notifications are not implemented. SQL and database objects are trusted operator inputs.
Do not grant the role access to unsafe functions that can change critical session policies.

## Streaming

Add exactly one execution to a statement, then call
`pg_stream_execute(statement,chunk_rows,lane,transaction,context,notify)`. The block size
is 1..1024 rows, default 128. Acceptance and statement-close semantics match `pg_execute`.
A stream occupies its lane until SQL and block publication finish.

`OnPgRows(job,context)` reports that a block is available when notify=true. It may run again
on later ticks until the consumer takes that block. `pg_stream_next(job)` never waits: true
selects the next block and replaces the previous selection; false preserves the selection
and can be retried. False does not prove end-of-stream. Use result index 0 with existing
result readers while a block is selected, even before job completion.
`pg_stream_release(job)` releases the selected block and returns false without an error
if no block is selected. Both functions reject non-stream jobs.

The producer retains at most one pending block, one consumer block, and one block under
construction. Each block uses `MAX_RESULT_BYTES`; `MAX_RESULT_ROWS` applies to retained
non-stream results, not the stream's total row count. A full block channel applies worker
backpressure under the query/transaction deadline. Limit the number of blocks processed per
tick and release each promptly. libpq can still allocate a large row before the byte check.

Rows are provisional until final SQL confirmation. If a later row raises an SQL error,
already published rows do not make the operation successful. Cancellation or timeout before
the definitive response can yield UNCERTAIN. If SQL is confirmed but trailing block delivery
times out, the job is FAILED with incomplete delivery while its item remains CONFIRMED and
completed=1. Reconcile that known SQL outcome instead of replaying a write.

Drain remaining blocks in `OnPgComplete` before closing the job; completion can occur while
a block is still pending. Terminal results retain SQL metadata and affected-row counts,
without all streamed rows. Selected block metadata/affected counts may be provisional.
`notify=false` suppresses both row and completion callbacks; poll on ticks and use the same
consumer lifecycle. Cancel abandoned streams; owner unload/connection close also stops them.
See [extensions.pwn](../examples/extensions.pwn) for a bounded consumer and savepoint recovery.

## Recommended custom completion handlers

Job-submitting natives now append optional `callback`, `format` and variadic copied
arguments after `notify`. Prefer these for new application code. A custom callback gets
the job followed by its explicitly supplied arguments; no context is inserted. Omitted
callbacks preserve `OnPgComplete`; `notify=false` suppresses completion and row handlers.
See [custom callbacks](callbacks.md) for exact limits, errors, VM ownership and migration.

`pg_prepare_file(connection, filename, parameters=0, prepared=true, section="")` is the
recommended file interface for both whole-file and selected SQL. The removed
`pg_prepare_section` must migrate to this interface; recompile affected AMX files.
See [migration](prestable-cleanup.md) and [operator diagnostics](debugging.md).

## Optional component logging configuration

`pg_log_config(level, destination=PG_LOG_CONSOLE, const path[]="")` returns acceptance.
Use `PG_LOG_OFF/ERROR/WARN/INFO/DEBUG/TRACE` independently of `PG_LOG_CONSOLE/FILE/BOTH`.
Default startup remains WARN/console through `OMP_PG_LOG_LEVEL`; the last valid native call
wins globally and persists across script unload. Invalid calls leave it unchanged.
See [logging](debugging.md) for path confinement and asynchronous failure reporting, and
[source diagnostics](sql-sections.md#original-source-locations) for owner-readable parser errors.
