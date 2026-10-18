# Native reference 0.1.0 experimental

This reference covers all 69 declarations in [the public include](../pawn/omp-postgresql.inc).
Read [the execution contract](api.md) first. Signatures below preserve Pawn tags, reference
arguments and defaults. This is an internal source review, not an independent Astra audit.

## Shared arguments, ownership and errors

Every handle must belong to the calling VM generation. Connections, statements, batches and
jobs are distinct handle types; a transaction is specifically the job returned by BEGIN.
Closing an object invalidates that handle. Accepted jobs own their SQL/parameter snapshots;
closing a statement or batch cannot cancel those jobs. VM unload collects its resources.

`lane`, `item`, `result`, `row` and `column` are zero-based; binding `index` is one-based.
`transaction=0` means no explicit transaction. A nonzero transaction must be a confirmed
BEGIN on the same connection; it selects its reserved lane regardless of the `lane` argument.
`context` is an opaque copied cell. `notify=false` suppresses callbacks, not asynchronous
execution or the need to release jobs. There is no cross-lane ordering guarantee.

All submissions require the entire pool connected, an available lane/reservation and job
capacity. Failure returns 0 and sets the VM's local diagnostic; acceptance is not SQL success.
Worker errors appear in the job. Statement creation does not require the pool to be ready
and does not contact PostgreSQL. It still requires a valid connection handle.

All calls validate handle type/owner and Pawn addresses. Unless stated otherwise, failures
return 0/false with `pg_last_error`; state/index readers listed below return -1. Every ordinary
call clears the previous local diagnostic; copy it immediately after a rejected operation.
`pg_last_error` preserves it on successful copying. A failure of that getter replaces it with
the getter's diagnostic. No local diagnostic contains a numeric `PG_ERROR_*` category;
those categories belong to asynchronous jobs. Allocation failures are contained at the native
boundary and also return the native's error sentinel.

Text output is unpacked. `size` is capacity in cells, including the terminator, from 1 to
2,097,152. Too-small or invalid output fails before writes. Scalar output references change
only on success. Text inputs accept packed or unpacked byte strings; SQL and bound text must
be valid UTF-8 without embedded NUL. `pg_result_bytes` has its own binary capacity rules.

Terminal means the worker published CONFIRMED, FAILED, UNCERTAIN or CANCELLED; a later tick
dispatches completion. Terminal inspection can precede dispatch. Only `pg_job_close` and
`pg_job_latency` additionally require that completion dispatch has occurred, including for
`notify=false`. A selected stream block permits result reads before terminal status.

Limits are process-wide unless explicitly per object. Defaults: 8 connections, 8 lanes per
connection, 64 combined statement/batch handles, 128 retained jobs including results,
1,024 operations and 1 MiB per staged batch, 256 KiB SQL/file and encoded parameter limits,
2 MiB retained result bytes per job and 10,000 rows per non-stream result. BEGIN also reserves
a finishing job slot. [Configuration](security.md#backpressure-and-configuration) lists
`OMP_PG_*` overrides and ceilings.
Counts/encoded bytes, rather than just player counts, determine capacity.

The call examples are expressions for use inside a callback or function, with live handles
and checked preconditions. Assume `output[4097]`, `counter[32]`, `sqlstate[6]`, `name[128]`,
`integers[] = {1,2,3}`, `bytes[] = {0,127,255}`, `values[72]` containing three terminated
24-cell text slots, `nulls[] = {0,1,0}`, `written`, `value`, `Float:real` and `bool:flag`.
Always check returns in application code. Runnable, checked flows are in
[smoke](../examples/smoke.pwn), [extensions](../examples/extensions.pwn),
[bootstrap](../examples/bootstrap.pwn) and [sections](../examples/sections.pwn).

## Connections and lanes

### pg_connect

```pawn
native pg_connect(const profile[], pool_size = 1);
```

**Contract:** `profile` is a 1..128-byte ASCII service name using letters, digits, `_` or `-`,
not a connection string. `pool_size` is 1..MAX_LANES. Credentials/TLS come from libpq profiles.
**Return/errors:** New connection handle, or 0 for invalid name/count or connection capacity.
Connection/authentication failures arrive asynchronously through connection state/message.
**Example:** `pg_connect("game", 2);`

### pg_connection_close

```pawn
native bool:pg_connection_close(connection);
```

**Contract:** Start pool shutdown and cancellation without joining workers. Reject new jobs.
Keep the handle through the final DISCONNECTED callback; collect it on the following tick.
Statements/batches/retained jobs still need their own close calls.
**Return/errors:** true for a valid handle, including another close while it remains live;
false for an invalid/foreign/collected handle. Does not confirm rollback of uncertain SQL.
**Example:** `pg_connection_close(connection);`

### pg_connection_state

```pawn
native pg_connection_state(connection);
```

**Contract:** Read aggregate `PG_DISCONNECTED`, `PG_CONNECTING`, `PG_CONNECTED`,
`PG_RECONNECTING`, `PG_CONNECTION_FAILED` or `PG_CLOSING`. CONNECTED requires all lanes ready.
**Return/errors:** State, or -1 for invalid ownership/handle. This is a current sample.
**Example:** `pg_connection_state(connection);`

### pg_connection_ready

```pawn
native pg_connection_ready(connection);
```

**Contract:** Count connected physical sessions; partial readiness does not permit submission.
**Return/errors:** 0..pool_size; 0 also indicates local failure, distinguish with last error.
**Example:** `pg_connection_ready(connection);`

### pg_connection_message

```pawn
native bool:pg_connection_message(connection, lane, output[], size = sizeof output);
```

**Contract:** Copy the selected lane's last connection diagnostic, empty after success.
Validate `lane` against this pool. Diagnostics may contain sensitive connection information.
**Return/errors:** true when copied, false for invalid lane/handle/output or insufficient size.
**Example:** `pg_connection_message(connection, 0, output);`

### pg_lane_state

```pawn
native pg_lane_state(connection, lane);
```

**Contract:** Read one physical session's `PG_*` connection state, independently of aggregate state.
**Return/errors:** State, or -1 for invalid connection/owner/lane.
**Example:** `pg_lane_state(connection, 0);`

### pg_lane_pending

```pawn
native pg_lane_pending(connection, lane);
```

**Contract:** Sample queued jobs on the lane, excluding its running job.
**Return/errors:** Nonnegative count; 0 also indicates invalid handle/lane. This gauge can change immediately.
**Example:** `pg_lane_pending(connection, 0);`

### pg_lane_active

```pawn
native pg_lane_active(connection, lane);
```

**Contract:** Sample the running-job gauge, 0 or 1; it does not count SQL executions in that job.
**Return/errors:** Gauge, or 0 with a diagnostic for invalid handle/lane.
**Example:** `pg_lane_active(connection, 0);`

### pg_lane_stat

```pawn
native bool:pg_lane_stat(connection, lane, metric, output[], size = sizeof output);
```

**Contract:** Copy a cumulative unsigned counter as decimal text. `metric` is one of the ten
`PG_STAT_*` constants: completed/confirmed/failed/uncertain/cancelled jobs, connection attempts,
reconnects, SQL commands, rows received or execution microseconds. Counters reset with the pool.
SQL commands count dispatches, excluding preparation; they do not confirm effects.
**Return/errors:** true when copied; false for invalid metric/lane/handle/output. A 21-cell
buffer holds a full uint64 counter. Separate samples are not an atomic multi-counter snapshot.
**Example:** `pg_lane_stat(connection, 0, PG_STAT_SQL_COMMANDS, counter);`

## Query and statement creation

### pg_query

```pawn
native pg_query(connection, const sql[], lane = 0, transaction = 0, context = 0, bool:notify = true);
```

**Contract:** Submit exactly one zero-parameter SQL operation, without a statement handle.
Nonempty UTF-8 SQL must fit MAX_SQL_BYTES and the ordinary-command policy. Use binders for values.
**Return/errors:** Job handle or 0 for local validation/submission failure. SQL syntax, permissions
and server errors appear in the job; PostgreSQL rejects multiple commands in extended protocol.
**Example:** `pg_query(connection, "SELECT 1");`

### pg_prepare

```pawn
native pg_prepare(connection, const sql[], parameters = 0, bool:prepared = true);
```

**Contract:** Create a logical statement with exactly 0..64 parameters and SQL up to MAX_SQL_BYTES.
`prepared=true` reuses the lane's most recent matching unnamed plan; false uses direct parameters.
Call bind/add before execute, including add for zero parameters. Close after reuse is complete.
**Return/errors:** Statement handle or 0 for invalid SQL/count/owner or shared statement capacity.
SQL syntax/types are validated by PostgreSQL at execution, not by this creation call.
**Example:** `pg_prepare(connection, "SELECT $1::integer", 1);`

### pg_prepare_file

```pawn
native pg_prepare_file(connection, const filename[], parameters = 0, bool:prepared = true);
```

**Contract:** Same statement lifecycle as prepare. Store a 1..1024-byte filename now; the worker
resolves a regular relative `.sql` file inside `scriptfiles/sql`, reads at most MAX_SQL_BYTES,
accepts UTF-8/BOM and validates before dispatch. A homogeneous job shares one read across
snapshots. A heterogeneous batch reads each ordinary file operation when it is reached;
unlike named sections, these operations have no job-wide file preflight/index.
**Return/errors:** Statement handle or 0 for local filename/count/handle limits. Unsafe paths,
missing/oversized/invalid files fail the accepted job. Changes before worker capture are visible;
later executions reload. Deploy files atomically from trusted storage.
**Example:** `pg_prepare_file(connection, "load_account.sql", 1);`

### pg_prepare_section

```pawn
native pg_prepare_section(connection, const filename[], const section[], parameters = 0, bool:prepared = true);
```

**Contract:** Prepare one named section through the same bind/add/execute path. Filename rules
match prepare*file; `section` is case-sensitive ASCII `[A-Za-z*][A-Za-z0-9_]\*`, 1..64 bytes.
Worker validates the whole file, up to 1,024 sections, and captures named inputs before any
operation in that job. One index per filename string per job; later jobs reload. Requires
`standard_conforming_strings=on`. See [section grammar/errors/capture limits](sql-sections.md).
**Return/errors:** Statement handle or 0 for local argument/capacity errors. Missing, duplicate,
empty/malformed sections and file errors fail the job; no alternate section executes.
**Example:** `pg_prepare_section(connection, "accounts.sql", "load_account", 1);`

## Binding and immutable snapshots

Every binder replaces only the selected temporary binding, using index 1..the declared
parameter count. Data is copied before return; Pawn buffers may then be reused. A rejected
bind preserves its previous value. Bindings alone never execute. Encoded values must fit
MAX_PARAMETER_BYTES. All functions below return true on success, false with a local error
on invalid handles/indices, encoding, type, dimensions or limits. PostgreSQL performs final
input-format/constraint validation during execution. See [types](types.md) for every OID.

### pg_bind_int

```pawn
native bool:pg_bind_int(statement, index, value);
```

**Contract:** Copy a signed int32 Pawn cell as PG_INT (OID 23).
**Return/errors:** Binder rules above; no narrowing from an int64 string is performed.
**Example:** `pg_bind_int(statement, 1, 42);`

### pg_bind_int64

```pawn
native bool:pg_bind_int64(statement, index, const decimal[]);
```

**Contract:** Strict signed decimal text in [-9223372036854775808, 9223372036854775807], OID 20.
**Return/errors:** Binder rules; overflow, plus sign, whitespace, exponent or trailing text fails.
The native reads at most 32 bytes; all valid int64 representations fit.
**Example:** `pg_bind_int64(statement, 1, "9223372036854775807");`

### pg_bind_float

```pawn
native bool:pg_bind_float(statement, index, Float:value);
```

**Contract:** Finite float32, PG_FLOAT (OID 700), encoded for float32 round-trip precision.
**Return/errors:** Binder rules; NaN and infinity fail. Use typed decimal text for float64 precision.
**Example:** `pg_bind_float(statement, 1, 1.25);`

### pg_bind_bool

```pawn
native bool:pg_bind_bool(statement, index, bool:value);
```

**Contract:** Boolean PG_BOOL (OID 16); only cell 0 or 1 is accepted.
**Return/errors:** Binder rules; arbitrary nonzero values are rejected rather than normalized.
**Example:** `pg_bind_bool(statement, 1, true);`

### pg_bind_null

```pawn
native bool:pg_bind_null(statement, index, type);
```

**Contract:** Explicit SQL NULL with one supported scalar or array OID. `type` has no default.
**Return/errors:** Binder rules; unknown OIDs fail. NULL is a bound value, distinct from missing.
**Example:** `pg_bind_null(statement, 1, PG_TEXT);`

### pg_bind_string

```pawn
native bool:pg_bind_string(statement, index, const value[], type = PG_TEXT);
```

**Contract:** UTF-8 text for TEXT, SMALLINT, DOUBLE, NUMERIC, UUID, JSONB, BYTEA, DATE, TIME,
TIMESTAMP, TIMESTAMPTZ or INTERVAL. This binder does not accept INT, INT64, FLOAT, BOOL or array
OIDs; use their specific binders. An empty string is valid transport and is not NULL.
**Return/errors:** Binder rules; unsupported OID/UTF-8 fails locally. Type syntax is server-validated.
**Example:** `pg_bind_string(statement, 1, "123.456789", PG_NUMERIC);`

### pg_bind_int_array

```pawn
native bool:pg_bind_int_array(statement, index, const values[], count = sizeof values);
```

**Contract:** Copy exactly `count` int32 cells, 0..16,384, as PG_INT_ARRAY. No per-element NULLs.
**Return/errors:** Binder rules and encoded byte limit; count 0 succeeds with an empty array.
**Example:** `pg_bind_int_array(statement, 1, integers);`

### pg_bind_string_array

```pawn
native bool:pg_bind_string_array(statement, index, const values[], count, width);
```

**Contract:** TEXT[] from flat string slots at `values[i * width]`; count 0..16,384, width >=1,
count\*width <=262,144 cells. Each slot needs termination within width. No per-element NULLs.
**Return/errors:** Binder rules; quoting/escaping and encoded parameter bytes also count.
**Example:** `pg_bind_string_array(statement, 1, values, 3, 24);`

### pg_bind_bytes

```pawn
native bool:pg_bind_bytes(statement, index, const bytes[], count = sizeof bytes);
```

**Contract:** BYTEA from exactly `count` cells in 0..255, without a terminator. Count is
nonnegative and `2+2*count` must fit MAX_PARAMETER_BYTES. Embedded zero is ordinary data.
**Return/errors:** Binder rules; count 0 sends empty BYTEA, not NULL.
**Example:** `pg_bind_bytes(statement, 1, bytes);`

### pg_bind_array

```pawn
native bool:pg_bind_array(statement, index, const values[], count, width, element_type, const nulls[]);
```

**Contract:** Nullable typed array from flat textual slots with the same dimensions as the
string-array binder. `element_type` is a supported scalar OID, not its array OID. `nulls[i]`
must be 0 or 1; a 1 sends SQL NULL without reading that slot. BIGINT is range-checked locally.
**Return/errors:** Binder rules; invalid NULL mask, unsupported scalar or bad BIGINT fails.
Other formats are server-validated. Zero count sends an empty array, with positive width.
**Example:** `pg_bind_array(statement, 1, values, 3, 24, PG_INT64, nulls);`

### pg_add

```pawn
native bool:pg_add(statement);
```

**Contract:** Require every binding, capture an immutable parameter set, then clear temporary
bindings. First successful add fixes OIDs for the lifetime of the statement, including reuse.
For zero parameters, each add still creates one execution. No SQL is sent by add.
**Return/errors:** true on snapshot; false for missing binding, changed OIDs or batch count/bytes.
Failure preserves staging. Twenty adds followed by execute mean one job with twenty operations.
**Example:** `pg_add(statement);`

### pg_execute

```pawn
native pg_execute(statement, lane = 0, transaction = 0, context = 0, bool:notify = true);
```

**Contract:** Submit all added snapshots FIFO on one lane, stopping at first error. On acceptance,
consume staging; the statement remains reusable/closable and retains fixed OIDs.
**Return/errors:** Job handle or 0 for empty staging/submission failure. Rejection preserves
snapshots. No implicit atomicity outside a transaction; successful items can affect many rows.
**Example:** `pg_execute(statement, 0, transaction, 1);`

### pg_statement_close

```pawn
native bool:pg_statement_close(statement);
```

**Contract:** Release statement, bindings and unsent snapshots. Accepted jobs continue.
This does not explicitly remove the session's unnamed plan.
**Return/errors:** true on close; false for invalid/foreign/already closed statement.
**Example:** `pg_statement_close(statement);`

## Heterogeneous batches

### pg_batch_create

```pawn
native pg_batch_create(connection);
```

**Contract:** Create empty staging on a valid connection, sharing MAX_STATEMENTS with statements.
**Return/errors:** Batch handle or 0 for invalid owner/connection or capacity. No SQL is submitted.
**Example:** `pg_batch_create(connection);`

### pg_batch_add

```pawn
native bool:pg_batch_add(batch, statement);
```

**Contract:** Transfer all added snapshots on the same connection into ordered operations.
Keep statement SQL/mode, parameters and section metadata. Consume source staging only on success;
source statement remains reusable with its OIDs. Temporary unadded bindings are not transferred.
**Return/errors:** true or false for empty source, wrong handles/connection or batch limits.
SQL/section copies and operation overhead count toward bytes. Rejection preserves both objects.
**Example:** `pg_batch_add(batch, statement);`

### pg_batch_execute

```pawn
native pg_batch_execute(batch, lane = 0, transaction = 0, context = 0, bool:notify = true);
```

**Contract:** Submit flattened operations as one FIFO job; stop on first failure. Consume accepted
operations, keep the empty batch handle for reuse/close. Named inputs preflight before any SQL.
**Return/errors:** Job handle or 0 for empty batch/submission failure, preserving rejected staging.
A batch alone is not atomic; inspect each item and confirmed prefix before retrying anything.
**Example:** `pg_batch_execute(batch, 0, transaction, 2);`

### pg_batch_close

```pawn
native bool:pg_batch_close(batch);
```

**Contract:** Release staging/handle; accepted jobs continue unaffected.
**Return/errors:** true on close; false for invalid/foreign/already closed batch.
**Example:** `pg_batch_close(batch);`

## Streaming

### pg_stream_execute

```pawn
native pg_stream_execute(statement, chunk_rows = 128, lane = 0, transaction = 0, context = 0, bool:notify = true);
```

**Contract:** Submit exactly one added snapshot; chunk_rows is 1..1,024. Statement consumption and
rejection match execute. Occupy the lane through SQL and block publication. Blocks obey the
byte limit/backpressure/deadline contract; total streamed rows are not MAX_RESULT_ROWS-limited.
**Return/errors:** Job handle or 0 for invalid chunk count, wrong snapshot count or submission
failure. Rows remain provisional until final SQL confirmation; drain/release blocks and close job.
**Example:** `pg_stream_execute(statement, 64, 0, transaction, 3);`

### pg_stream_next

```pawn
native bool:pg_stream_next(job);
```

**Contract:** Nonblocking selection of the next block, replacing/releasing the old selection
only when a new block exists. Read selected rows with result index 0. Drain after completion too.
**Return/errors:** true when selected; false without local error if none is ready, preserving
selection. False is not end-of-stream. Invalid/foreign/non-stream job fails with a diagnostic.
**Example:** `pg_stream_next(job);`

### pg_stream_release

```pawn
native bool:pg_stream_release(job);
```

**Contract:** Release the current selected block, allowing producer progress. Does not close job.
**Return/errors:** true when released; false without error when no block is selected. Invalid
or non-stream job fails with a diagnostic. Never retain selected-row references after release.
**Example:** `pg_stream_release(job);`

## Transactions and savepoints

Every operation here returns a job handle or 0 for local failure. Await each required
confirmation before dependent work. Savepoint/final jobs are separate from the BEGIN job;
close them after dispatch, and close BEGIN after its lane reservation is released.

### pg_begin

```pawn
native pg_begin(connection, lane = 0, context = 0, bool:notify = true);
```

**Contract:** Immediately reserve a free lane, behind previously queued jobs, using session
default isolation/read-only settings. The returned job also becomes the transaction ID.
**Return/errors:** BEGIN job or 0 for submission/reservation/capacity errors. Failed BEGIN
releases the reservation at dispatch. Do not use or close an active BEGIN prematurely.
**Example:** `pg_begin(connection, 0, 4);`

### pg_begin_ex

```pawn
native pg_begin_ex(connection, isolation, bool:read_only, lane = 0, context = 0, bool:notify = true);
```

**Contract:** BEGIN with explicit PG_READ_COMMITTED, PG_REPEATABLE_READ or PG_SERIALIZABLE
and READ ONLY/READ WRITE. `isolation` and `read_only` have no defaults. Other rules match begin.
**Return/errors:** BEGIN job or 0 for unsupported isolation/submission failure. PostgreSQL
can still reject execution or later serialize/abort a transaction; no automatic replay occurs.
**Example:** `pg_begin_ex(connection, PG_SERIALIZABLE, true, 0, 5);`

### pg_savepoint

```pawn
native pg_savepoint(transaction, const name[], context = 0, bool:notify = true);
```

**Contract:** Create a savepoint on the confirmed BEGIN lane. Name is ASCII
`[A-Za-z_][A-Za-z0-9_]*`, 1..63 bytes, quoted as an identifier. Creation cannot recover a
failed transaction; savepoint names follow PostgreSQL's transaction-local lifetime.
**Return/errors:** Job or 0 for invalid name/transaction/submission. Server failure is a job error.
**Example:** `pg_savepoint(transaction, "before_save", 6);`

### pg_rollback_to

```pawn
native pg_rollback_to(transaction, const name[], context = 0, bool:notify = true);
```

**Contract:** Roll back to a previously confirmed savepoint, retaining that savepoint. Same
name rules. Only successful server confirmation clears the transaction's recoverable error state.
**Return/errors:** Job or 0 locally; nonexistent savepoint fails asynchronously. A failed
rollback-to does not make subsequent COMMIT safe. The transaction reservation remains.
**Example:** `pg_rollback_to(transaction, "before_save", 7);`

### pg_release_savepoint

```pawn
native pg_release_savepoint(transaction, const name[], context = 0, bool:notify = true);
```

**Contract:** Release a savepoint, using the same name rules. This does not COMMIT or release
the lane and cannot recover a failed transaction.
**Return/errors:** Job or 0 locally; nonexistent savepoint/failed transaction are worker errors.
**Example:** `pg_release_savepoint(transaction, "before_save", 8);`

### pg_commit

```pawn
native pg_commit(transaction, context = 0, bool:notify = true);
```

**Contract:** Mark a confirmed BEGIN as ending immediately; reject further transaction submissions.
After a required failure, execute ROLLBACK and report FAILED/PG_ERROR_TRANSACTION. Dispatch
of the finish job releases the reservation. Lost COMMIT response can mean UNCERTAIN.
**Return/errors:** Finish job or 0 for invalid/unconfirmed/already ending transaction or submission
failure. Local rejection restores the ending marker. Confirmed SQL durability follows server policy.
Deferred constraints can reject COMMIT after earlier successful statements: the finish job
is FAILED/PG_ERROR_SQL with its SQLSTATE, without replay. An idle server session is immediately
reusable after dispatch; another post-error session state is discarded before recovery.
**Example:** `pg_commit(transaction, 9);`

### pg_rollback

```pawn
native pg_rollback(transaction, context = 0, bool:notify = true);
```

**Contract:** End the confirmed transaction through a separate ROLLBACK job; same ending/reservation
rules as commit. Cancellation/connection loss need outcome inspection, not presumed confirmation.
**Return/errors:** Finish job or 0 for invalid/unconfirmed/already ending transaction/submission.
**Example:** `pg_rollback(transaction, 10);`

## Job inspection and release

### pg_job_status

```pawn
native pg_job_status(job);
```

**Contract:** Sample PG_QUEUED, PG_RUNNING, PG_CONFIRMED, PG_FAILED, PG_UNCERTAIN or PG_CANCELLED,
including before completion dispatch. Confirmation inside a transaction is provisional until COMMIT.
**Return/errors:** State, or -1 for invalid/foreign/closed job. No call waits for completion.
**Example:** `pg_job_status(job);`

### pg_job_close

```pawn
native bool:pg_job_close(job);
```

**Contract:** Release a terminal, dispatched job and its results/stream blocks. Retained jobs
consume MAX_JOBS. Closing a completed job before its scheduled callback suppresses that callback.
**Return/errors:** true on release; false for invalid owner/handle, pending/undispatched completion,
or BEGIN still reserving its lane. `notify=false` jobs also need tick dispatch before close.
**Example:** `pg_job_close(job);`

### pg_cancel

```pawn
native bool:pg_cancel(job);
```

**Contract:** Mark a queued/running job for worker cancellation. Before dispatch this can be
CANCELLED; after SQL dispatch it can be UNCERTAIN. A racing completion can still be CONFIRMED.
Cancellation does not release the job or confirm that no earlier batch writes occurred.
**Return/errors:** true when marked; false without error if already terminal; false with local
error for invalid job. Inspect final state/items and finish/rollback the transaction as appropriate.
**Example:** `pg_cancel(job);`

### pg_job_completed

```pawn
native pg_job_completed(job);
```

**Contract:** Terminal count of SQL operations with confirmed success, not affected rows or
durable commits. Can be positive on a failed/uncertain batch or incomplete stream delivery.
**Return/errors:** Count; 0 also indicates invalid/pending job with a local error.
**Example:** `pg_job_completed(job);`

### pg_job_size

```pawn
native pg_job_size(job);
```

**Contract:** Immutable submitted operation count, available while queued/running. BEGIN,
savepoint and finish jobs each have one item. One bulk UPDATE is one item regardless of rows.
**Return/errors:** Positive count, or 0 for invalid/foreign job.
**Example:** `pg_job_size(job);`

### pg_job_item_status

```pawn
native pg_job_item_status(job, item);
```

**Contract:** Terminal `PG_ITEM_NOT_EXECUTED`, CONFIRMED, FAILED, UNCERTAIN or CANCELLED for
zero-based item. Earlier successful items remain confirmed; queued cancellation can mark all cancelled.
**Return/errors:** Item status; -1 for pending job, invalid item/handle/owner. Not a result index mapping.
**Example:** `pg_job_item_status(job, 0);`

### pg_job_error_index

```pawn
native pg_job_error_index(job);
```

**Contract:** Terminal zero-based failing/interrupted item; some job-wide or pre-dispatch failures
identify no item. Named-input preflight can identify a later item before any SQL was sent.
**Return/errors:** Index or -1 when none identified; -1 with local error on pending/invalid job.
**Example:** `pg_job_error_index(job);`

### pg_job_error

```pawn
native pg_job_error(job);
```

**Contract:** Terminal general category: PG_ERROR_NONE, INVALID, LIMIT, NOT_READY, SQL,
CONNECTION, TIMEOUT, CANCELLED, FILE, TRANSACTION or INTERNAL. Inspect job message too.
**Return/errors:** Category; 0 is both PG_ERROR_NONE and the local-failure sentinel. Verify
status/readiness or last error before interpreting zero. This is separate from SQLSTATE.
**Example:** `pg_job_error(job);`

### pg_job_message

```pawn
native bool:pg_job_message(job, output[], size = sizeof output);
```

**Contract:** Copy terminal general worker diagnostic, empty on success. PostgreSQL's detailed
statement message, if retained, is read with result_message. Treat both as potentially sensitive.
**Return/errors:** true when copied; false for pending/invalid job or text-output failure.
**Example:** `pg_job_message(job, output);`

### pg_job_duration

```pawn
native bool:pg_job_duration(job, microseconds[], size = sizeof microseconds);
```

**Contract:** Copy terminal worker execution microseconds as signed decimal text, excluding queue
and tick dispatch. A job cancelled before starting can have duration 0.
**Return/errors:** true or false for pending/invalid job/output. A 21-cell buffer fits int64 text.
**Example:** `pg_job_duration(job, counter);`

### pg_job_queue_time

```pawn
native bool:pg_job_queue_time(job, microseconds[], size = sizeof microseconds);
```

**Contract:** Copy terminal acceptance-to-worker-start microseconds. For jobs discarded from a
queue, records time until discard, even though no SQL started. Decimal string avoids cell overflow.
**Return/errors:** true or false for pending/invalid job/output.
**Example:** `pg_job_queue_time(job, counter);`

### pg_job_latency

```pawn
native bool:pg_job_latency(job, microseconds[], size = sizeof microseconds);
```

**Contract:** Copy acceptance-to-completion-dispatch microseconds. Includes queue, execution and
tick delay, but excludes callback body time. Available in OnPgComplete or after tick dispatch
when polling with notify=false.
**Return/errors:** true or false for pending/undispatched/invalid job/output. Not a native-call benchmark.
**Example:** `pg_job_latency(job, counter);`

### pg_job_connection

```pawn
native pg_job_connection(job);
```

**Contract:** Read originating logical connection ID even while queued/running. It can identify
a connection already closed/collected; it is not a new live reference to that connection.
**Return/errors:** Positive ID, or 0 for invalid/foreign job.
**Example:** `pg_job_connection(job);`

## Results, columns and values

Result readers require terminal status unless a stream block is selected; in that case only
result 0 exists and selected metadata/affected counts may be provisional. An error can occur
without any PostgreSQL result, so use result_count before indexing. Retained results contain
the attempted prefix, including a retained SQL failure, rather than every unattempted item.
All readers retain the job; explicitly close it when finished. Text output rules above apply.

### pg_result_count

```pawn
native pg_result_count(job);
```

**Contract:** Number of retained statement results, or 1 for a selected stream block. Final
stream results contain metadata, not the whole row set. A local/transport failure may have zero.
**Return/errors:** Count; 0 with local error for pending job without a selected block or invalid job.
**Example:** `pg_result_count(job);`

### pg_result_rows

```pawn
native pg_result_rows(job, result = 0);
```

**Contract:** Rows in one retained result/selected block, not total affected/streamed rows.
**Return/errors:** Count; 0 with local error for invalid result/handle or unavailable results.
**Example:** `pg_result_rows(job);`

### pg_result_columns

```pawn
native pg_result_columns(job, result = 0);
```

**Contract:** Column count; SELECT with zero rows still has columns, ordinary command results may not.
**Return/errors:** Count; 0 also means invalid/unavailable result with a local error.
**Example:** `pg_result_columns(job);`

### pg_result_column

```pawn
native pg_result_column(job, result, const name[]);
```

**Contract:** Exact, case-sensitive search for the first matching column name; input up to 1,024
bytes. Prefer unique SQL aliases. All three arguments are required; no default result here.
**Return/errors:** Zero-based column or -1 without error if absent; -1 with error for invalid input/result.
**Example:** `pg_result_column(job, 0, "id");`

### pg_result_column_name

```pawn
native bool:pg_result_column_name(job, result, column, output[], size = sizeof output);
```

**Contract:** Copy exact selected column name, including aliases chosen by PostgreSQL.
**Return/errors:** true when copied; false for invalid/unavailable result/column/output.
**Example:** `pg_result_column_name(job, 0, 0, name);`

### pg_result_column_type

```pawn
native pg_result_column_type(job, result, column);
```

**Contract:** PostgreSQL column OID in a Pawn cell, not the binder's declared input type.
SQL expressions/casts can change it; user-defined OIDs need not be one of the PG\_\* constants.
**Return/errors:** OID, or 0 with local error for invalid/unavailable metadata/column/result.
**Example:** `pg_result_column_type(job, 0, 0);`

### pg_result_is_null

```pawn
native bool:pg_result_is_null(job, result, row, column);
```

**Contract:** Test PostgreSQL NULL, distinct from empty string, zero or empty array/BYTEA.
**Return/errors:** true for NULL, false without error for a non-NULL value; false with diagnostic
for invalid/unavailable result/row/column. False alone does not validate the coordinates.
**Example:** `pg_result_is_null(job, 0, 0, 0);`

### pg_result_string

```pawn
native bool:pg_result_string(job, result, row, column, output[], size = sizeof output);
```

**Contract:** Copy full server text without numeric narrowing. Use for BIGINT, NUMERIC, float64,
temporal values, UUID and JSONB. Formatting follows server session policy. Empty string succeeds.
**Return/errors:** true when copied; false for NULL, invalid coordinates/unavailable results or output failure.
**Example:** `pg_result_string(job, 0, 0, 0, output);`

### pg_result_bytes

```pawn
native bool:pg_result_bytes(job, result, row, column, output[], &written, capacity = sizeof output);
```

**Contract:** Decode a BYTEA column with bytea_output=hex into one byte per cell, no terminator.
Capacity is 0..2,097,152 cells, at least decoded length. `written` must not overlap actual output
bytes. Preflight all addresses and encoding before writes; empty BYTEA succeeds with written=0.
**Return/errors:** true or false for wrong OID, NULL, malformed/non-hex output, coordinates,
capacity/address/overlap failure. Neither output changes on failure. Use text getter for escape format.
**Example:** `pg_result_bytes(job, 0, 0, 0, bytes, written);`

### pg_result_int

```pawn
native bool:pg_result_int(job, result, row, column, &value);
```

**Contract:** Parse entire server text as signed int32, independent of column OID. Preserve output
on failure; read larger/exact numeric values as strings instead.
**Return/errors:** true or false for NULL, overflow, non-integer text, coordinates or output address.
**Example:** `pg_result_int(job, 0, 0, 0, value);`

### pg_result_float

```pawn
native bool:pg_result_float(job, result, row, column, &Float:value);
```

**Contract:** Parse entire text as finite float32, possibly rounding higher precision. No float64
guarantee and no automatic substitution of zero for invalid values.
**Return/errors:** true or false for NULL, parse/range/nonfinite value, coordinates or output address.
**Example:** `pg_result_float(job, 0, 0, 0, real);`

### pg_result_bool

```pawn
native bool:pg_result_bool(job, result, row, column, &bool:value);
```

**Contract:** Parse native PostgreSQL `t`/`f` to true/false; text casts `true`/`false` do not match.
**Return/errors:** true for conversion, including a successfully read false; false return means
failure from NULL/non-boolean text/coordinates/output. Inspect the output reference for the value.
**Example:** `pg_result_bool(job, 0, 0, 0, flag);`

### pg_result_affected

```pawn
native bool:pg_result_affected(job, result, output[], size = sizeof output);
```

**Contract:** Copy affected rows as decimal text, "0" if the command supplies no count. This is
separate from returned row count and job item count. A selected stream block may be provisional.
**Return/errors:** true or false for invalid/unavailable result/output. No int32 narrowing.
**Example:** `pg_result_affected(job, 0, counter);`

### pg_result_sqlstate

```pawn
native bool:pg_result_sqlstate(job, result, output[], size = sizeof output);
```

**Contract:** Copy the retained PostgreSQL SQLSTATE, normally five characters on server error and
empty on success. At least six cells for a nonempty SQLSTATE. Not every failure has a result.
**Return/errors:** true even for empty text; false for invalid/unavailable result/output.
**Example:** `pg_result_sqlstate(job, 0, sqlstate);`

### pg_result_message

```pawn
native bool:pg_result_message(job, result, output[], size = sizeof output);
```

**Contract:** Copy retained PostgreSQL diagnostic, possibly containing SQL/data; filter before logging.
This complements the general job message and can be empty. No automatic truncation.
**Return/errors:** true when copied; false for invalid/unavailable result or insufficient/invalid output.
**Example:** `pg_result_message(job, 0, output);`

### pg_last_error

```pawn
native bool:pg_last_error(output[], size = sizeof output);
```

**Contract:** Copy this VM's most recent local diagnostic before calling another native. Successful
read preserves it; initially or after a successful ordinary native it is empty. Worker errors
must be read from jobs. Use an adequately sized buffer; diagnostics have no universal fixed length.
**Return/errors:** true when copied, including empty text; false for output failure, which itself
becomes the new local diagnostic. This getter never waits for PostgreSQL.
**Example:** `pg_last_error(output);`

## Callbacks and example selection

```pawn
forward OnPgConnection(connection, connection_state);
forward OnPgComplete(job, context);
forward OnPgRows(job, context);
```

Callbacks execute on ticks in the owning VM only, never on workers. Connection transitions
between ticks can coalesce. OnPgComplete is dispatched once for a retained notify=true job;
another callback can close it first and suppress delivery. OnPgRows can repeat while a block
is pending. Drain streams on completion, inspect final SQL outcome, then close the job.
Return without sleep; callback VM state is restored even after errors. Validate player
generation tokens in context before touching player state.

| Need                                                                                                   | Runnable example                                                                          |
| ------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------- |
| Connect, BEGIN, 20 bind/add snapshots, one array/UNNEST update, COMMIT and cleanup                     | [smoke.pwn](../examples/smoke.pwn)                                                        |
| Nullable arrays, BYTEA, heterogeneous operations, explicit BEGIN, savepoints, streaming and statistics | [extensions.pwn](../examples/extensions.pwn)                                              |
| Gate player/world availability; validate property, vehicle and clan data before activation             | [bootstrap.pwn](../examples/bootstrap.pwn)                                                |
| Account load, individual/bulk save, named-file reuse and transaction cleanup                           | [sections.pwn](../examples/sections.pwn) and [accounts.sql](../examples/sql/accounts.sql) |
| Callback failure/suspension recovery and pending-query filterscript unload                             | [callbacks.pwn](../examples/callbacks.pwn) and [unload.pwn](../examples/unload.pwn)       |

These complementary examples already cover the requested lifecycle. A seventh combined
gamemode would duplicate fixture/state machines and make each contract harder to isolate;
the guide uses this map rather than adding another example. Temporary tables in these tests
belong to one physical session; production tables belong in an operator-managed schema.
Keep related temporary-table work on its lane; a pool does not share temporary tables.
