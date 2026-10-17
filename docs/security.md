# Security, concurrency, and operating limits

## Trust model

Players supply **values**, never SQL, service names, or file paths. The operator manages SQL,
profiles, database objects, and files. Handle ownership protects scripts against accidental
misuse; it does not turn arbitrary native AMX code into a security sandbox. Configure a
PostgreSQL role without administrative privileges and grant only the permissions it needs.

The component does not authenticate players or hash passwords. Use a separate cryptographic
library for gamemode authentication, preferably with Argon2id. Stored hashes are ordinary
bound SQL data.

Parameters remain separate from SQL, with explicit OIDs and NULL values. No binder concatenates
player values into SQL; arrays use parameter encoding rather than interpolated SQL syntax.
There are no escaping functions. Text cannot contain NUL; hexadecimal BYTEA input preserves
all bytes, including zero.

libpq resolves passwords through service profiles, passfiles, or certificates. The component
retains only the profile name; libpq may retain connection material throughout the session.
There is no guarantee of cryptographic memory erasure, swap protection, or core-dump removal.
The component does not print SQL, parameters, passwords, or PostgreSQL diagnostics.
Explicit messages returned to Pawn may contain data values in constraint errors, so filter
them before logging. Also review the PostgreSQL server's logging policy.

## Ordering and transactions

A PGconn belongs to one worker from creation through PQfinish. Workers never invoke callbacks
or access AMX memory. Terminal publication is atomic: results are immutable after the
release/acquire transition. Dispatch extracts the completion queue before invoking callbacks,
allowing reentry. VM generations prevent old work from targeting a reused AMX address.

FIFO lanes preserve statement submission and completion order within each lane. There is no
global ordering across lanes or logical connections. Keep each entity on one lane, or use
optimistic concurrency (`WHERE version=$n`) or PostgreSQL locks. Pool size does not change
during a connection's lifetime. Transactions retain a physical session and cannot migrate
after reconnection.

Queries are never retried automatically, including SELECT. Only the transport reconnects,
using progressive backoff. Affected pending jobs fail without dispatch; the running job may
have earlier confirmed statements. The remaining batch is not replayed. A timeout or observed
cancellation during execution closes the session, but PostgreSQL may have confirmed the
operation before the interruption. Reconcile with idempotency and a subsequent query.
Cancellation does not mean PQcancel succeeded or prove a rollback. Uncertain commits require
application-level reconciliation.

## Backpressure and configuration

Environment variables are read once when the component is created. Values must be positive
integers; invalid settings prevent component loading. `RECONNECT_ATTEMPTS` also accepts zero
to disable additional attempts. Limits are global unless stated otherwise.

| Variable `OMP_PG_*` | Default | Maximum | Scope |
| --- | ---: | ---: | --- |
| MAX_CONNECTIONS | 8 | 64 | Logical connections, including those closing. |
| MAX_LANES | 8 | 16 | Lanes per connection. |
| MAX_STATEMENTS | 64 | 1024 | Live statement and heterogeneous batch handles combined. |
| MAX_JOBS | 128 | 4096 | Pending jobs, retained results, and transaction finishing reservations. |
| MAX_BATCH_COUNT | 1024 | 65536 | Operations per staged statement/batch. |
| MAX_BATCH_BYTES | 1048576 | 67108864 | Captured parameters per statement/batch; heterogeneous SQL/operation copies also count. |
| MAX_SQL_BYTES | 262144 | 4194304 | SQL text or file contents. |
| MAX_PARAMETER_BYTES | 262144 | 4194304 | Each encoded bound value. |
| MAX_RESULT_BYTES | 2097152 | 67108864 | Estimated accumulated data/object size per retained job, or per streaming block. |
| MAX_RESULT_ROWS | 10000 | 1000000 | Rows per non-stream statement result. |
| CONNECT_MS | 10000 | 120000 | Each connection attempt. |
| QUERY_MS | 30000 | 3600000 | Each statement, including preparation. |
| TRANSACTION_MS | 60000 | 3600000 | Total time from confirmed BEGIN. |
| RECONNECT_ATTEMPTS | 3 | 10 | Additional attempts after the initial attempt; minimum 0. |
| RECONNECT_BASE_MS | 100 | 60000 | Default backoff: 100, 200, then 400 ms. |

All other minima are 1. Values and ceilings above match `src/config.cpp`; the fixed Pawn
output/array/name limits are independent of these overrides. See the
[native reference](native-reference.md) before increasing engine limits. Named SQL inputs
also charge their captured source and selected text against MAX_BATCH_BYTES per job;
their file/section-count limits remain in force.

Each BEGIN reserves capacity for its eventual COMMIT/ROLLBACK. Normal jobs cannot consume
that capacity. `MAX_JOBS` includes these reservations; a limit of 1 does not allow BEGIN.

Callbacks must release their jobs when no longer needed. Rejected `pg_execute` calls preserve
the batch for a later **enqueue attempt**, which is different from replaying SQL already sent.
Reaching a result limit after dispatch produces an uncertain result and closes the session.
Use LIMIT, selective RETURNING, key-based pagination, or bulk statements that do not return
every modified row.

Streaming retains at most one pending block, one selected block, and one producer block,
each subject to the byte limit. Its total row count is not limited by MAX_RESULT_ROWS.
The producer waits when the channel is full, checking cancellation, shutdown, and the
query/transaction deadline. Pawn consumers never wait for that producer; drain a bounded
number of blocks per tick. Cancel a stream that the script no longer intends to consume.
Rows remain provisional until SQL confirmation. An interrupted trailing delivery after
confirmation reports job failure separately from a confirmed SQL operation; do not replay it.

Limits bound retained objects but are not a strict RSS limit. libpq may allocate memory for
a single large row or message before delivering it through single-row mode. Staging up to
64 parameters per statement consumes additional memory: `MAX_PARAMETER_BYTES` is per value,
while `MAX_BATCH_BYTES` covers snapshots. Capturing and sending can require temporary copies.
Size the limits together and apply server constraints to very large values. Unlimited results
and full-table exports within callbacks are unsupported.

## Shutdown and files

Named SQL files are operator-controlled code. `pg_prepare_section` validates the entire
file's lexical structure and fails closed on malformed/duplicate/empty sections; it never
falls back to another query. It requires standard-conforming strings and applies both
file-size and per-job capture limits. This is not SQL authorization or application validation:
keep parameters, least-privilege roles and query deadlines. Atomic file replacement avoids
mixed in-place edits; cross-file/worker snapshots and racing administrator symlink changes
are outside the guarantee. See [the complete section contract](sql-sections.md).

`pg_connection_close` and AMX unloading signal cancellation; ticks collect finished workers.
Component shutdown stops all workers before destroying their queues. Socket polling in
intervals of up to 10 ms detects the stop request. DNS resolution, authentication providers,
libpq profile loading, and filesystem operations may block within their libraries. These do
not block the game thread, but may delay the final server join. For strict environments,
use hostaddr together with host to preserve TLS identity verification, local storage, and
bounded authentication policies. Shutdown has no maximum latency independent of the OS.

SQL file loading canonicalizes the root and path, requires a regular `.sql` file, and limits
read size. Do not place the root in player-writable storage. An administrator changing links
between canonicalization and opening can cause a TOCTOU race; this is not a security boundary
between hostile OS users. The extended protocol rejects multiple SQL commands; a regular
expression is not used to parse SQL.
