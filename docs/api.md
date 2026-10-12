# Pawn API contract 0.1

[omp-postgresql.inc](../pawn/omp-postgresql.inc) is the authoritative source of signatures and
default arguments. Handles are positive; 0 is invalid. Each handle belongs to the VM that created
it and is never recycled. Do not share handles between scripts. All Pawn calls return immediately:
they validate, copy, enqueue, or read memory. Callbacks run on the main thread when a tick
processes completed work.

Asynchronous callbacks must return without `sleep`. If a callback attempts to suspend, the
component reports the error, discards that continuation, and restores the VM's previous state.
An already suspended `main()` retains its continuation. Do not keep references to local
variables for use on another tick.

## Return values and local errors

`pg_connect`, `pg_prepare`, `pg_prepare_file`, `pg_execute`, `pg_begin`, `pg_commit`, and
`pg_rollback` return a positive handle or 0. Boolean operations return true/false.
`pg_job_status`, `pg_connection_state`, and `pg_result_column` return -1 on error; the column
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

`pg_prepare_file` returns the same kind of logical statement, but reads the SQL file each time
the batch executes. Reading and canonicalization run in the worker. Paths must be relative
`.sql` paths under `scriptfiles/sql`. Files must be UTF-8; an initial BOM is accepted.
File errors appear in the job. This is not a migration system.

Binding indices start at 1. Rebinding replaces that value before `pg_add`.
`pg_add` requires every parameter, copies an immutable snapshot, clears all temporary bindings,
and fixes the statement's parameter types. NULL requires an explicit OID; a missing binding
is not NULL. Parameter types cannot change between batches of the same statement.
Bindings are never executed implicitly without `pg_add`.

| Native                 | Representation                                                               |
| ---------------------- | ---------------------------------------------------------------------------- |
| `pg_bind_int`          | int32, OID 23.                                                               |
| `pg_bind_int64`        | Strict signed decimal int64 string covering the full range, OID 20.          |
| `pg_bind_float`        | Finite IEEE float32 Pawn Float, OID 700; no double-precision guarantee.      |
| `pg_bind_bool`         | 0/1, OID 16.                                                                 |
| `pg_bind_null`         | Explicit supported OID, listed below.                                        |
| `pg_bind_string`       | UTF-8 without NUL; PG_TEXT by default, or UUID, JSONB, or BYTEA.             |
| `pg_bind_int_array`    | Pawn array with explicit count or sizeof, up to 16384 elements.              |
| `pg_bind_string_array` | Flat unpacked slots with count and width in cells, up to 262144 cells total. |

The supported NULL types are PG_BOOL, PG_BYTEA, PG_INT64, PG_INT, PG_TEXT, PG_FLOAT,
PG_INT_ARRAY, PG_TEXT_ARRAY, PG_UUID, and PG_JSONB.

BYTEA uses PostgreSQL hexadecimal input such as `\x00ff...`, rather than text containing NUL.
PostgreSQL validates UUID and JSONB values. Arrays use PostgreSQL's text parameter format,
with quotes and backslashes escaped. `"NULL"` in a text array is text, not SQL NULL.
The initial array binders do not support individual NULL elements; an entire array may be
NULL through `pg_bind_null`.

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

## Jobs and results

`OnPgComplete(job,context)` runs once for each job with notify=true while the VM remains loaded.
Closing another completed job before its callback runs suppresses that notification.
Context is a copied cell, not a pointer. For players, use a session token and validate it in
the callback; a player ID may have been reused. Unloading the AMX cancels and collects its
resources without callbacks. Workers never retain references to Pawn memory.

States are QUEUED (accepted), RUNNING, CONFIRMED (successful response), FAILED (known failure),
UNCERTAIN (sent without a definitive response, or interrupted result), and CANCELLED (not sent).
Within a transaction, CONFIRMED means the statement succeeded, **not that it is durable**;
wait for COMMIT confirmation. COMMIT durability also depends on PostgreSQL configuration,
including `synchronous_commit`, replication, and storage. Reconcile uncertain writes using
an idempotency key or version check; never replay them blindly.

| Native               | Contract                                                                      |
| -------------------- | ----------------------------------------------------------------------------- |
| `pg_job_status`      | Available while a job is pending.                                             |
| `pg_job_connection`  | Logical connection identifier, also available while pending.                  |
| `pg_job_completed`   | Statements with successful responses before failure; not a row count.         |
| `pg_job_error`       | General error category, `PG_ERROR_*`.                                         |
| `pg_job_message`     | Explanation of the general job failure.                                       |
| `pg_job_duration`    | Execution microseconds as a decimal string, excluding queue wait.             |
| `pg_result_count`    | Available statement results, including a failed SQL result.                   |
| `pg_result_rows`     | Row count of the selected result.                                             |
| `pg_result_columns`  | Column count of the selected result.                                          |
| `pg_result_column`   | First column matching the name, or -1. Prefer unique aliases.                 |
| `pg_result_is_null`  | Detects SQL NULL.                                                             |
| `pg_result_string`   | Text preserving BIGINT/NUMERIC precision. SQL NULL fails.                     |
| `pg_result_int`      | Strict int32 conversion; overflow and NULL return false.                      |
| `pg_result_float`    | Finite float32 conversion; higher-precision data may be rounded.              |
| `pg_result_bool`     | Accepts only PostgreSQL's t/f representation.                                 |
| `pg_result_affected` | Affected rows as a decimal string; 0 if the command has no count.             |
| `pg_result_sqlstate` | PostgreSQL SQLSTATE for the selected statement.                               |
| `pg_result_message`  | PostgreSQL diagnostic for the selected statement; may contain sensitive data. |
| `pg_job_close`       | Releases the job and all results after its terminal completion is dispatched. |
| `pg_cancel`          | Requests cancellation; does not guarantee that no effects occurred.           |

`pg_cancel(job)` returns true when marking a pending or running job. It returns false without
an error if the job is already terminal; an invalid handle returns false with a local error.
If the worker observes cancellation before starting, the job becomes `PG_CANCELLED` without
sending SQL. Cancellation observed after starting may produce `PG_UNCERTAIN`; earlier
statements in the same batch may already be confirmed. A job may still become `PG_CONFIRMED`
if it finishes before observing the request. If the worker cancels a required transaction
operation, a subsequent COMMIT cannot report success; roll back or finish the transaction.

Result, row, and column indices start at zero. Scalar readers modify their output reference
only on success. `RETURNING` is a normal result; there is no universal insert_id.
A SELECT returning zero rows still has columns. Commands with no returned rows have zero columns.
Retained results count toward the job limit. Without callbacks, close the job after the tick
that dispatches its terminal completion. Do not close the BEGIN job while its transaction
still reserves the lane.

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

The total transaction deadline closes the session and rolls back unconfirmed work. Finishing
the transaction handle releases the logical reservation. A lost connection during COMMIT
may leave its result UNCERTAIN.

Transaction-control SQL, COPY, DO, and CALL are rejected to protect this contract.
Savepoints, prepared transactions/2PC, COPY, and public result streaming are not implemented
in this version. SQL and database objects are trusted operator inputs. Do not grant the role
access to unsafe functions that can change critical session policies.
