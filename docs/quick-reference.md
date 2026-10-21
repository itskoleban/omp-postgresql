# Choosing the right API operation

Read [installation](installation.md) and [your first query](getting-started.md) first.
This page chooses an operation; the [native reference](native-reference.md) supplies exact
signatures, defaults and bounds. All SQL work is asynchronous. A positive job handle means
local acceptance; inspect terminal status and results in its completion callback.

| Use | Entry point | What to do next |
| --- | --- | --- |
| Open an operator-defined service profile | `pg_connect` | Retain the connection; wait for its `OnPgConnection` event with `PG_CONNECTED` before submitting. |
| Submit one trusted SQL literal with no parameters | `pg_query` | Inspect the returned job. Use binding for player-supplied values. |
| Execute parameterized SQL, once or repeatedly | `pg_prepare` | Bind every parameter, capture with `pg_add`, then submit with `pg_execute`. Preparing is logical; SQL errors arrive later. |
| Load operator-controlled SQL from a file | `pg_prepare_file` | Same bind/add/execute lifecycle; an optional final section selects `-- name: ...`. File/parser errors arrive in the job. |
| Capture the current bindings | `pg_add` | A successful add copies one snapshot, clears temporary bindings and fixes types. Bind every parameter again for the next snapshot. |
| Submit one statement's staged snapshots | `pg_execute` | One job, one SQL execution/result per snapshot. Acceptance consumes snapshots; local rejection preserves them. |
| Submit different statements in one job | `pg_batch_create`, `pg_batch_add`, `pg_batch_execute` | Transfer staged snapshots into the batch; inspect each operation. A batch adds ordering, not atomicity. |
| Read many rows with bounded delivery | `pg_stream_execute` | Use exactly one staged snapshot; drain/release blocks through `pg_stream_next`/`pg_stream_release`. Rows are provisional until final completion. |
| Make a group of operations transactional | `pg_begin` / `pg_begin_ex` | Retain this BEGIN job as the transaction handle. Wait for confirmation, then pass it to dependent jobs on its reserved lane. |
| Finish successful transaction work | `pg_commit` | Wait for the new finish job to confirm. Earlier SQL confirmation is provisional; COMMIT can fail or be uncertain. |
| Abandon a known transaction | `pg_rollback` | Wait for finish completion; release both finish and BEGIN jobs. Reconcile any uncertain COMMIT rather than replaying it. |
| Recover from an expected SQL error inside a transaction | `pg_savepoint`, `pg_rollback_to`, `pg_release_savepoint` | Chain each operation from the previous confirmation. Only a confirmed rollback-to clears the required-failure state. |
| Select diagnostic severity/destination | `pg_log_config` | Check local rejection. True accepts a component-global policy; it does not confirm file opening or durable delivery. |

For one account, one bind/add/execute produces one SQL execution. Three adds followed by
one execute produce three executions in one job. To update three accounts in one SQL
execution, use one array snapshot with an explicit [UNNEST statement](saving.md).
Validate populated lengths, unique IDs, affected rows and concurrency/version policy.
Neither snapshots nor batches are atomic outside an explicit transaction.

## Pick a completion route

Leave `callback` empty for `OnPgComplete(job, context)`. A nonempty custom callback replaces
that completion route and receives `(job, copied_extra_arguments...)`; `context` is not
added implicitly. With variadic arguments, provide the preceding arguments positionally.
The [callback guide](callbacks.md) shows exact job and player-generation guards.

`notify=false` suppresses row and completion callbacks; it does not make execution
synchronous or release the job. Poll without blocking, and close only after terminal
completion has been dispatched. Streaming blocks keep `OnPgRows(job, context)` when
notifications are enabled; only the final handler is customizable.

## Release the right resource

| Resource | Owner and release rule |
| --- | --- |
| Statement | The script closes it with `pg_statement_close`; accepted jobs own their snapshots and survive closure. Closing after rejection deliberately discards unsent work. |
| Heterogeneous batch | Close with `pg_batch_close`; accepted jobs survive. Statements consumed by `pg_batch_add` remain independently owned. |
| Ordinary job and results | Inspect first, then `pg_job_close` after dispatch, normally inside its completion callback. Callback return does not release results. |
| Streaming block | Release each selected block; close the terminal job separately. Bound per-tick work, and cancel an abandoned consumer. |
| BEGIN job / transaction | Keep through COMMIT/ROLLBACK dispatch. Closing a query or statement does not end the transaction. |
| Connection | Stop new submissions, drain/reconcile accepted work, then close. Shutdown/unload is not a confirmed final save. |

Read [errors and diagnostics](debugging.md), [transactions](transactions.md) and
[reconnection/shutdown](troubleshooting.md) before building production save paths.
`PG_FAILED` can follow earlier committed batch items; cancellation does not undo SQL;
`PG_UNCERTAIN` needs application reconciliation. Limits and backpressure are described in
[security](security.md). The published RC remains a testing release within its documented
coverage; these operation choices do not certify runtime or production safety.
