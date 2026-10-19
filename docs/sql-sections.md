# Named SQL sections

`pg_prepare_file(connection, const filename[], parameters=0, bool:prepared=true, const section[]="")`
creates an owner-bound logical statement and returns its handle, or `PG_INVALID` on local
rejection (`pg_last_error` explains the rejection). Parameter count, binding, immutable
`pg_add` snapshots, handle limits and `pg_statement_close` match `pg_prepare`.
Closing a statement does not invalidate accepted jobs. File errors are asynchronous job
errors; receiving a statement/job handle is not a database confirmation.

```pawn
new load = pg_prepare_file(Database, "accounts.sql", 1, true, "load_account");
if (load && pg_bind_string(load, 1, "alice") && pg_add(load)) {
    new job = pg_execute(load); // Inspect in OnPgComplete, then pg_job_close(job).
    if (!job) { /* Read pg_last_error; snapshots remain available for explicit retry. */ }
}
if (load) pg_statement_close(load);
```

This short fragment discards any unsent snapshots when it closes the statement. A caller
planning a later enqueue attempt must retain that statement until submission succeeds or
the application explicitly abandons it. Accepted jobs keep their own captured inputs.

## File format

Store UTF-8 files below `scriptfiles/sql`; a leading UTF-8 BOM is accepted. Paths follow
the same relative `.sql`, canonical confinement and size checks as `pg_prepare_file`.
Declarations occupy their own physical line:

```sql
-- name: load_account
SELECT id, username, money, score FROM accounts WHERE username = $1;

-- name: save_account
UPDATE accounts SET money = $1, score = $2 WHERE id = $3;
```

Names are **case-sensitive**, ASCII `[A-Za-z_][A-Za-z0-9_]*`, at most **64 bytes**.
`load_account`, `_load2` and `LoadAccount` are distinct valid names. Empty names,
`123_load`, `load account`, punctuation and Unicode names are rejected without repair.
Spaces/tabs may precede `--`, surround the name, or separate `--` from `name:`.
The lowercase keyword and colon are literal: `-- name : x` and `-- name x` are malformed.
There can be at most **1,024 sections per file**. Every section must have non-comment SQL;
each may have one terminating semicolon. Extra semicolons/commands are rejected.
Only comments/whitespace may precede the first section. Ordinary comments such as
`-- namespace: example` are allowed; an inline `SELECT 1; -- name: x` is an ordinary comment.
Unicode dash lookalikes (including `— name: x`) are rejected as declarations, never accepted.

The complete file is indexed before selection. Duplicate names, malformed declarations,
empty sections, extra commands and unclosed quotes/comments reject it even in an unselected
section. No fallback section is executed. An absent requested name is a distinct error.
Unselected sections are **never submitted to PostgreSQL**. Their SQL syntax, permissions
and semantics are not validated by the server; the selected SQL receives normal PostgreSQL
validation. Existing restrictions on transaction/protocol commands apply to selected SQL.

The bounded lexer recognizes doubled quotes, `E'...'` escapes (including newline
continuations), quoted identifiers, nested block comments, line comments, `$$` and tagged
dollar quotes, UTF-8, LF and CRLF. Apparent markers/semicolons inside those constructs
are data. `$1`, casts, arrays, CTEs, JOINs and RETURNING retain PostgreSQL semantics.
This is not a full grammar parser: SQL bodies using internal unquoted semicolons
(for example `BEGIN ATOMIC` bodies) are outside this format; use dollar-quoted bodies
where permitted by the existing API. It neither rewrites SQL nor checks application safety.

Named sections require the session's `standard_conforming_strings=on` (PostgreSQL's default),
checked by the worker before using the captured SQL. Use explicit `E'...'` for backslash
escapes. This keeps the lexer's quote boundaries consistent with the server's
[lexical rules](https://www.postgresql.org/docs/17/sql-syntax-lexical.html).
`pg_prepare` and `pg_prepare_file` keep their existing behavior.

## Capture, reuse and concurrency

Preparing, binding, adding and queueing never open the file on the open.mp thread.
The worker reads/indexes it when the job reaches its turn. An edit while the job is queued
can therefore affect that job. After capture, all its homogeneous executions use the same
selected SQL bytes; later edits cannot change them. A later explicit job reloads the file.
`prepared=true` reuses the lane's existing unnamed plan when selected SQL and OIDs match;
it does not retain the file index between jobs. There is no global cache.

Heterogeneous batches capture every named file/section before dispatching their first
operation. References to the **same relative filename string** share one read/index in that
job; repeated references to the same section also share its extracted SQL. Different path
aliases are separate inputs. Different workers/jobs may observe different file versions.
This is not a filesystem transaction: deploy operator-controlled files by atomic replacement,
and do not modify an open file in place or race symlink changes. Whole-file snapshots across
separate files or workers are not promised.

The worker bounds each source file by `OMP_PG_MAX_SQL_BYTES` (default 262,144). The sum of
named source contents and distinct extracted SQL byte lengths per job is bounded by
`OMP_PG_MAX_BATCH_BYTES` (default 1,048,576), in addition to existing parameter/batch limits.
Index/container overhead is additional and bounded by the file and section-count limits;
this is not a byte-exact process memory ceiling. Exceeding capture capacity fails before SQL
dispatch. One job with many different large files may need splitting or smaller inputs.
Reading/indexing is linear in input size apart from map indexing; name lookup is logarithmic
in section count. The selected SQL is copied once per distinct selection per job. The index
and captures are released when worker execution exits; parameter/result lifetimes are unchanged.

Named statements work with `pg_batch_add`, transactions/savepoints and streaming through the
existing APIs. A heterogeneous capture error can reject a later item before earlier items
execute: its `pg_job_error_index` identifies that item, while earlier items remain
`PG_ITEM_NOT_EXECUTED`. SQL errors after dispatch preserve normal partial-result rules.
Sections do not add atomicity; use a transaction. No interrupted/uncertain write is replayed.

## Errors and examples

Invalid Pawn addresses/arguments and invalid requested names are local rejections.
Missing/inaccessible files, unsafe paths, file-size/encoding/read failures, malformed markers,
duplicate/missing/empty sections and unterminated lexical constructs produce `PG_ERROR_FILE`
with bounded owner-readable source diagnostics. Section-count/capture-budget exhaustion produces
`PG_ERROR_LIMIT`. A nonstandard string setting produces `PG_ERROR_INVALID`.
PostgreSQL syntax/permission errors remain `PG_ERROR_SQL` with per-result SQLSTATE;
connection failures, cancellation and UNCERTAIN results keep their existing contracts.
There is no filesystem fallback or alternate-query execution on error.

See [accounts.sql](../examples/sql/accounts.sql) for load, individual save, delete and bulk
save statements. [sections.pwn](../examples/sections.pwn) demonstrates actual asynchronous
loading, statement reuse, individual/bulk updates, two snapshots, verification and COMMIT.
It creates session-local temporary tables on lane 0, drops them on COMMIT and leaves other
sessions untouched. A production gamemode owns its persistent schema separately.

Compile it with the same official includes/compiler as the other examples. Copy the SQL
to the server's `scriptfiles/sql/accounts.sql` and use a disposable service profile. The
existing x86 CI compiles it again from the extracted package and runs it in a fresh server,
requiring `PG SECTION PASS`. Consult [validation](validation.md) for actual run evidence
and outstanding RC gates, and [performance](performance.md) for measurement limits.

## Unified file-preparation entry point

Prefer `pg_prepare_file(connection, "accounts.sql", 1, true, "load_account")` for a section.
Its final section defaults to `""`, preserving whole-file execution. The old four-argument
compiled `pg_prepare_file` call remains valid. `pg_prepare_section` has been removed;
migrate to the unified form and recompile affected AMX files (see [migration](prestable-cleanup.md)). A requested section never falls back to whole-file SQL.
The grammar, file capture, errors, UTF-8/path limits and worker behavior described above
apply unchanged. The [section example](../examples/sections.pwn) exercises named selection
through the unified interface. Whole-file selection is covered by the C++ and native tests.

## Original source locations

Parser diagnostics name the sanitized relative file, offending valid section, and original
1-based file line/byte column when known. CRLF is one newline; LF and CR work; tabs count
one byte; the initial UTF-8 BOM consumes no displayed column. Multibyte UTF-8 consumes its
encoded byte width. Positions point at the declaration, extra token/semicolon, or opening
quote/comment, never at a fabricated execution line. Missing file/section, invalid UTF-8/NUL
and no-section errors carry no line. Invalid/overlong declaration names are not echoed.
Filename labels are bounded to 160 bytes plus an ellipsis, unsafe bytes become `?`, and
absolute/unsafe paths are replaced by `<sql-file>`. These labels go only to the owning VM.

Messages verified by the lexer tests (each from its own fixture):

```text
accounts.sql:3:1: Duplicate SQL section 'save'; first declared at line 1
accounts.sql:2:4: Malformed SQL section declaration; expected '-- name: identifier' (also '--name:identifier')
accounts.sql:1:1: Empty SQL section 'empty'; add one SQL statement after the declaration
accounts.sql:4:11: SQL section must contain exactly one statement in section 'other'; remove the extra statement or semicolon
accounts.sql:2:8: Unterminated SQL quoted string in section 'x'; close the opening quote
accounts.sql: SQL section 'Save' not found; check the case-sensitive declaration name
```

Read parser/file/capture failures with `pg_job_error`, `pg_job_message` and
`pg_job_error_index`, then close the completed job. Invalid **requested** section arguments
remain local `pg_last_error` failures before a job exists. Whole-file mode bypasses the
named-section lexer. `--name:save_account` stays valid; `-- name : save_account` stays malformed.

PostgreSQL-originated statement positions are **not mapped** in this feature. The current
prepared/direct/stream receive paths retain SQLSTATE and server messages but do not retain
per-result submitted-text character mappings. PostgreSQL positions count characters, while
our parser columns count bytes; internal-query positions are a different source. No guessed
file location is attached. Use `pg_result_sqlstate` / `pg_result_message` for the original
server diagnostic. Automatic logs never copy these messages or parser filenames/sections.
