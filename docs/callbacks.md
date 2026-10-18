# Custom asynchronous callbacks

For new applications, specify a public callback when submitting a job:

```pawn
forward OnAccountLoaded(job, playerid, session);
public OnAccountLoaded(job, playerid, session) {
    if (playerid < 0 || playerid >= MAX_PLAYERS || !IsPlayerConnected(playerid) ||
        Token[playerid] != session) {
        pg_job_close(job);
        return 1;
    }
    if (pg_job_status(job) == PG_CONFIRMED) {
        // Validate result count, row count, NULLs and every getter before applying data.
    } else {
        // FAILED, CANCELLED and UNCERTAIN arrive here too. Never replay an uncertain write.
    }
    pg_job_close(job);
    return 1;
}
// After binding and pg_add; check the returned handle for local rejection.
new job = pg_execute(statement, 0, 0, 0, true, "OnAccountLoaded", "ii", playerid, Token[playerid]);
```

This fragment assumes a session-token array. The complete
[account example](../examples/accounts.pwn) also tracks the exact pending job, invalidates
sessions on disconnect/readiness loss and keeps its external authentication boundary.

## One optional tail on every job-submitting native

`pg_query`, `pg_execute`, `pg_batch_execute`, `pg_stream_execute`, `pg_begin`,
`pg_begin_ex`, `pg_savepoint`, `pg_rollback_to`, `pg_release_savepoint`, `pg_commit`
and `pg_rollback` append the same arguments after their existing `notify` parameter:

```pawn
const callback[] = "", const format[] = "", {Float,_}:...
```

All preceding parameter names, order and defaults remain intact. See the
[native reference](native-reference.md) for complete declarations. With no extra arguments,
use a positional callback name or a named argument, for example
`pg_execute(statement, .callback = "OnFinished")`. Pawn rejects positional arguments after
named arguments, so calls with variadic extras must supply the preceding parameters
positionally, as in the account example above. Named file selection also works:
`pg_prepare_file(connection, "accounts.sql", 1, .section = "load_account")`.

| Submission | Completion |
| --- | --- |
| Nonempty callback, `notify=true` | `Callback(job, copied_argument_1, ...)`, exactly once. |
| Empty callback, `notify=true` | Existing `OnPgComplete(job, context)`. |
| `notify=false` | Neither completion nor row callback; poll and release explicitly. |

The job handle is always the first argument. A custom callback **does not** receive
`context` implicitly. Include it explicitly in `format` and the additional arguments if
needed. For streams, the original context still goes to `OnPgRows(job, context)`.
Return values from callbacks do not determine SQL success or trigger retry.

## Formats and bounds

| Format | Argument and copy semantics |
| --- | --- |
| `i` | Signed 32-bit Pawn cell, copied by value. |
| `f` | Pawn Float cell, all bits preserved; callback declares `Float:`. |
| `b` | Boolean cell, validated as 0 or 1; callback declares `bool:`. |
| `s` | UTF-8 string, packed or unpacked input; owned copy, unpacked callback argument. |

The format contains exactly one character per additional argument. It excludes the job
handle. Scalar Pawn varargs are passed by reference by the compiler; the native validates
and dereferences them before returning. Changing a source variable or local string later
cannot change the callback's values. Arrays and reference/output arguments are deliberately
unsupported: they require an explicit length/copy contract and would increase retained
and VM heap budgets. Unknown format characters are rejected before submission.

Fixed limits: 16 extra arguments, 31 ASCII identifier bytes in the public name,
1024 UTF-8 bytes per string, and 4096 aggregate payload bytes per job. Payload counts
the name and each string with its terminator, plus four bytes per scalar. Allocator and
descriptor overhead are additional but bounded by the argument/name/string counts.
Expanded unpacked strings consume up to roughly 16 KiB of transient Pawn heap at dispatch;
the VM must have sufficient stack/heap space. A push failure restores the original frame.

Metadata uses the existing global job limit; no descriptor registry or independent queue
can grow without accepted jobs. The default 128 jobs can retain at most 512 KiB of logical
callback payload, plus bounded C++ overhead. The configured 4096-job ceiling permits at
most 16 MiB of logical callback payload. `notify=false` retains no descriptor. Metadata is
released on dispatch (including failure), owner unload, rejection or engine destruction,
even while an application retains the completed job's SQL results.

## Rejection, dispatch and lifetime

Invalid names, addresses, formats, counts, encodings and limits reject locally with 0 and
`pg_last_error`; statement/batch snapshots remain staged. A notified callback must exist
in the owning VM at submission. A same-named public in another VM does not qualify.
With `notify=false`, supplied arguments are still validated/copied, but public lookup is
unnecessary and the copy is discarded before acceptance.

No callback is installed after submission. The owned descriptor travels with the job
before enqueueing; even immediate worker completion cannot race its configuration.
The existing tick dispatcher resolves the public again in the same VM generation.
Workers never access the descriptor, AMX addresses or Pawn code. No lookup cache survives
unload/reload. All terminal job statuses follow the same completion path.

If a public disappears between submission and dispatch, there is no fallback notification.
The owning script's local error becomes `Completion callback public no longer exists`.
The job remains available for polling and explicit closure. A callback execution/push error
is reported through the SDK and a local diagnostic. As with other local errors, copy it
before another native clears it. No failure implicitly closes a retained job or rolls back
a transaction. Applications requiring recovery from callback failure should retain their
accepted job identifiers and have a bounded polling/watchdog path.

The full stack, heap, instruction/frame state, registers and parameter/reset counters are
restored after normal return, partial push, runtime error or unsupported `sleep`. Pawn
continuations from callbacks are discarded; an existing suspended `main()` is preserved.
A VM that unloads cannot receive later work. Closing another dispatched job from an earlier
callback suppresses the later callback, as in the legacy contract. Closing the current job
inside its callback is safe; arguments remain owned for the duration of the call.

Keep a successful BEGIN job until its COMMIT/ROLLBACK dispatch releases the reservation.
A confirmed query inside a transaction is provisional. Failed/uncertain COMMIT and confirmed
ROLLBACK retain their existing meanings. Custom callbacks never enable automatic replay.

## Streaming scope

Only final completion is customizable in this change. Blocks continue through
`OnPgRows(job, context)` with the existing selection/release/backpressure contract. Rows do
not establish final success. A consumer still drains blocks when notifications are disabled.
The [callback regression gamemode](../examples/callbacks.pwn) demonstrates a custom final
handler and legacy row handler inside a transaction, including all eleven submission natives.
The [extension example](../examples/extensions.pwn) retains the original streaming path.

## Complete gameplay example

[custom.pwn](../examples/custom.pwn) supports `/pgload` and `/pgsave` for an RCON administrator
in a disposable test server. It loads account and clan data concurrently on two lanes,
saves account/property data separately, tracks four independent pending slots per player,
checks job/session identity and never retries an uncertain save automatically. It is an
interactive SQL/callback example, not an authentication system. Apply the following schema
as the operator, then seed the administrator's exact player name and property ownership:

```sql
CREATE TABLE tutorial_custom_accounts (
    id integer PRIMARY KEY, username text UNIQUE NOT NULL, score integer NOT NULL
);
CREATE TABLE tutorial_custom_clans (id integer PRIMARY KEY, name varchar(64) NOT NULL);
CREATE TABLE tutorial_custom_properties (
    id integer PRIMARY KEY,
    owner_id integer NOT NULL REFERENCES tutorial_custom_accounts(id), label text NOT NULL
);
INSERT INTO tutorial_custom_accounts VALUES (1, 'YourExactPlayerName', 0);
INSERT INTO tutorial_custom_clans VALUES (1, 'Example clan');
INSERT INTO tutorial_custom_properties VALUES (1, 1, 'Original property');
```

These two saves are independent; use a transaction when they must be atomic. The
[transfer tutorial](../examples/transfer.pwn) demonstrates explicit rollback/reconciliation.
Interactive player disconnect/slot reuse and the external authentication provider require
operator testing; compilation and unattended VM regressions do not establish those flows.

## Compatibility and migration

The component accepts both the exact old argument count and the complete new tail
(two strings and 0..16 varargs). A lone tail argument or malformed byte count is rejected
before argument memory is read. Unchanged native names and leading positions let old
compiled AMX programs continue using `OnPgComplete`. Newly compiled unchanged source emits
empty default strings and has the same behavior. A new AMX using these signatures requires
this component version; older binaries reject the extended argument count. Keep include and
component packages together. The C++ Engine is internal and has no binary ABI guarantee.

`pg_prepare_file(connection, filename, parameters, prepared, section="")` accepts the old
four-cell call and the new five-cell call. Empty section means whole-file behavior; a
nonempty section selects exactly that identifier. `pg_prepare_section` remains operational
through the same creation path. Grammar, capture timing and error categories are unchanged;
see [SQL sections](sql-sections.md).
