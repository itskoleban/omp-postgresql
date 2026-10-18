# Troubleshooting asynchronous work

First decide **where** the operation failed. A positive handle is local acceptance; only
terminal inspection establishes its outcome. Keep player-facing messages generic and
filter operator diagnostics, which can contain SQL, row data, database names or addresses.

| Symptom | Inspect | Next action |
| --- | --- | --- |
| Component does not load | Architecture, dependency loader output, invalid `OMP_PG_*` settings | Follow [installation](installation.md) and [dependencies](../DEPENDENCIES.md). Match x86 binaries and install the remaining CRT/system libraries. |
| `pg_connect` returns 0 | Immediately `pg_last_error` | Supply a service name, not a connection string; check name, pool size and handle limits. |
| Connection never reaches ready | `OnPgConnection`, lane state/message, ready count | Check the server process environment, libpq profile/password permissions, CA/hostname, PostgreSQL HBA rules and all pool lanes. A partly connected pool rejects submissions. |
| Submission returns 0 | Immediately copy `pg_last_error` before another native | Check readiness, ownership, lane reservation, bindings and capacity. No accepted job exists for this rejection. |
| Statement exists but execution fails | Job category/message, then available result SQLSTATE | Preparation is logical; server validation and SQL-file I/O happen later on the worker. |
| No callback with `notify=false` | Poll status on a timer/tick | No row/completion callback is requested. Dispatch still occurs internally; close only after that dispatch. |
| Job status is terminal but close fails | Local error from `pg_job_close` | A worker can publish between ticks. Retry close on a later tick, or release inside `OnPgComplete`; never spin. BEGIN can also remain reserved. |
| Output getter returns false | Local error; NULL and destination size | Size includes text terminator; buffers never silently truncate. False BOOLEAN data is a successful getter with output=false. |
| SQLSTATE is absent | General job category/message and result count | A local/file/transport failure need not have a PostgreSQL result. Never assume result index equals failing item. |
| Data arrives out of order | Explicit lane choices and transactions | FIFO is per lane; multiple lanes have no shared order. Use versions or locks for shared records. |
| Writes may have happened after cancel | Terminal job and item outcomes | Reconcile by operation ID/version. Cancellation cannot undo earlier confirmed statements. |
| Streaming stalls | Consumption/release rate, query deadline, lane pending/active | Drain a bounded number of blocks per tick, release selections, and cancel abandoned streams. A false `pg_stream_next` does not prove EOF. |
| Results or jobs hit limits | Retained jobs/results and configured ceilings | Release completed jobs, narrow SELECT/RETURNING, paginate/stream and bound submissions before raising limits. |
| File/section load fails | `PG_ERROR_FILE`/`PG_ERROR_LIMIT` and job message | Check `scriptfiles/sql`, relative `.sql` path, UTF-8, permissions, markers and complete-file structure. No fallback section is executed. |
| SQL changes while a file job waits | Deployment timing | Capture starts in the worker, not at prepare/add/enqueue. Atomically replace files; later jobs reload them. See [section capture](sql-sections.md#capture-reuse-and-concurrency). |

## Inspect jobs without inventing a result

Inside `OnPgComplete`, read `pg_job_status`, `pg_job_error`, `pg_job_error_index` and
`pg_job_completed` before closing. Read the general message with a sufficient buffer.
Enumerate `0 .. pg_result_count(job)-1` to inspect each retained result's SQLSTATE/message.
Some confirmed operations have no returned rows; a failed SQL result can still carry useful
metadata. A named-input preflight failure may identify an item that never dispatched while
all earlier items are NOT_EXECUTED. Do not index results directly by error_index unless the
actual available result inventory establishes that mapping.

`PG_CONFIRMED` is a successful SQL response, not durable transaction success before COMMIT.
`PG_FAILED` says the job failed, but earlier batch items may have committed. `PG_CANCELLED`
means this job did not send SQL. `PG_UNCERTAIN` means the worker cannot establish the complete
outcome; stop automated replay and reconcile. Streaming delivery can fail after its SQL item
was confirmed, so inspect item status as well as job status.

## Observe existing diagnostics

`pg_lane_pending` and `pg_lane_active` are approximate samples. `pg_lane_stat` returns
cumulative decimal strings; SQL command dispatches are not commit counts.
`pg_job_queue_time` measures acceptance to worker start, `pg_job_duration` worker execution,
and `pg_job_latency` acceptance to completion dispatch. All time strings are microseconds.
They do not measure your full callback body or server tick latency. See
[performance definitions](performance.md) before interpreting measurements. Avoid logging
every row/query on a busy server, and do not treat finite process-memory samples as bounds.

If every reconnect attempt is exhausted, close the failed logical connection and create a
new one under an explicit application policy. Existing statements belong to their original
connection, so recreate them for the new handle. Session-local tables, transactions and
physical preparations do not survive reconnect; accepted writes are never replayed by the
component. Collect old jobs and reconcile their outcomes independently of the new pool.

## Pre-stable cleanup

Use `pg_prepare_file(connection, filename, parameters, prepared, section)` for named SQL.
The removed `pg_prepare_section` requires source migration and AMX recompilation; other
leading argument/default positions remain supported. See [migration](prestable-cleanup.md)
and [internal diagnostics](debugging.md).
