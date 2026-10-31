# Internal diagnostics

Set `OMP_PG_LOG_LEVEL` before starting open.mp. The exact, case-sensitive values are
`OFF`, `ERROR`, `WARN`, `INFO`, `DEBUG`, `TRACE`. Unset means **WARN**. Empty, lowercase,
whitespace or unknown values reject component construction, like other strict settings.
This environment value controls startup console diagnostics before Pawn initializes. Restart to
change the startup value; a later successful `pg_log_config` call overrides it for this process.

Linux, for one process:

```sh
OMP_PG_LOG_LEVEL=DEBUG ./omp-server
OMP_PG_LOG_LEVEL=OFF ./omp-server
```

Windows PowerShell, in the process that starts the server:

```powershell
$env:OMP_PG_LOG_LEVEL = 'INFO'
.\omp-server.exe
# For the next launch:
$env:OMP_PG_LOG_LEVEL = 'OFF'
```

| Level          | Additional events (includes all less verbose levels)                                                                                                                |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| OFF            | No additional diagnostic records, locks, formatting or output.                                                                                                      |
| ERROR          | Exhausted lane startup/recovery, worker failure, uncertain outcomes and internal job failures.                                                                      |
| WARN (default) | Other failed/cancelled jobs, retry ordinals, admission rejection, custom callback missing, callback push/execution failure, missing owner and expired transactions. |
| INFO           | Logical connection create/close, lane connecting/ready/stopped, VM generation unload.                                                                               |
| DEBUG          | Accepted, running, terminal jobs, completion polled, final callback attempt begin/end, absent optional legacy publics.                                              |
| TRACE          | Suppressed notifications, explicit job release and row-block callback attempts.                                                                                     |

Events carry internal numeric owner-generation, connection, lane, job, transaction, kind,
source, status and error IDs where applicable. `kind`, `source`, `status` and `error` use
the zero-based enum order in the source; statuses/errors correspond to the public include.
Source 0/1/2 means inline/file/section; heterogeneous jobs retain the top-level inline
category, and their per-item source remains available through the normal execution path.
No callback context or extra argument values are logged. A callback attempt/end is not
proof the public executed successfully: check accompanying missing/failure events.
An accepted event precedes worker dequeue, but concurrent lanes can interleave records.

Durations use `steady_clock`, in microseconds:

- `queue_us`: acceptance to worker dequeue; includes any lane backlog.
- `execution_us`: worker processing after dequeue, including file capture and stream waits.
  Jobs rejected before execution have zero execution time.
- `completion_wait_us`: terminal preparation to poll, or to the actual final callback
  attempt, depending on the event. It is measured separately, rather than subtracting
  rounded durations. It includes publication/handoff work and main-thread scheduling.

`-1` means unavailable for that event. Console prefixes belong to open.mp; file lines carry writer UTC timestamps
(`YYYY-MM-DDTHH:MM:SSZ`), which represent delivery time, not SQL execution time. `completion_polled` does not mean Pawn received it.
`callback_end` means the dispatcher returned, including failure/unload paths. Existing
`pg_job_latency` remains acceptance to poll; its contract is unchanged. There is no new
slow-job threshold: compare the separate timings when investigating delays.

Illustrative output (not a captured runtime transcript):

```text
[omp-pg][INFO] lane_ready owner=1 conn=1 lane=0 job=0 tx=0 kind=0 source=0 status=0 error=0 queue_us=-1 execution_us=-1 completion_wait_us=-1 value=0
[omp-pg][ERROR] job_terminal owner=1 conn=1 lane=0 job=7 tx=3 kind=2 source=0 status=4 error=5 queue_us=10 execution_us=20000 completion_wait_us=-1 value=0
[omp-pg][WARN] log_dropped count=128 sink_failures=0
```

The second example is an uncertain COMMIT: inspect status/results and reconcile durable
application state. Never replay it automatically. For connection failure, enable INFO
temporarily and inspect `pg_connection_message` securely; verify profile/TLS settings
without pasting credentials into logs. Long queue time suggests lane backlog; long
execution can include PostgreSQL/network or stream-consumer delay. Long completion wait
points to main-thread scheduling. For missing callbacks verify the originating VM,
public declaration and generation; recover/close retained job handles explicitly.

All levels omit SQL, bound parameters, row contents, callback strings/names, profile names,
paths, PostgreSQL message text and credentials. The logger accepts no arbitrary text:
static single-line event names plus numeric fields prevent log forging. Existing explicit
message/result APIs can still contain database details; protect access to them separately.
There is no raw diagnostic mode or global file/result cache. File output uses the same
numeric records and privacy policy as the console.

## Optional runtime output control

Inside initialization, choose one policy and check local rejection. These alternatives
use `ReportLocalPgError`, defined below. For routine console diagnostics:

<!-- pawn-example: log.console -->

```pawn
if (!pg_log_config(PG_LOG_WARN, PG_LOG_CONSOLE)) ReportLocalPgError();
```

To select the default `logs/omp-postgresql.log` file:

<!-- pawn-example: log.file -->

```pawn
if (!pg_log_config(PG_LOG_WARN, PG_LOG_FILE)) ReportLocalPgError();
```

Or select both console and an allowed custom file:

<!-- pawn-example: log.both -->

```pawn
if (!pg_log_config(PG_LOG_DEBUG, PG_LOG_BOTH, "./logs/postgresql.log")) {
    ReportLocalPgError();
}
```

Each call replaces the global policy; copy the alternative your server needs.

`native bool:pg_log_config(level, destination = PG_LOG_CONSOLE, const path[] = "");`
Level and destination are independent. Constants `PG_LOG_OFF/ERROR/WARN/INFO/DEBUG/TRACE`
are 0..5; `PG_LOG_CONSOLE/FILE/BOTH` are 0..2. The **component-global** policy is last
valid call wins, serialized on the open.mp thread. Scripts are trusted administrators
of this setting. Unloading/reloading/resetting a script never restores an earlier setting;
a stale unloaded AMX cannot call a native. Re-read environment only on component startup.

True means **configuration accepted**, not file opened, messages delivered, flushed or
persisted. Invalid values/path/AMX strings return false and preserve the active configuration;
read `pg_last_error`. Console requires an empty path; file/both accept empty for the default.
Custom paths must be `[./]logs/<basename>.log`, relative to the server working directory.
Only one flat directory is authorized. Basenames are 5..96 ASCII bytes including `.log`,
letters/digits/underscore/hyphen/dot; no leading dot, repeated dots, reserved Windows device
names, roots/drives/UNC, backslashes, subdirectories, NUL/control/Unicode bytes or other
extensions. Directory/file failures are asynchronous, and therefore do not make the
already accepted native return false. No destination paths appear in automatic diagnostics.

The writer creates `logs` if absent, appends to existing regular files, and rejects links,
reparse points, devices, directories and multiple hard links. POSIX files are created mode
0600; existing files must belong to the effective user. Windows uses inherited server ACLs
and denies concurrent write/delete sharing while open. Live Windows readers must request
`FILE_SHARE_DELETE` because the writer retains DELETE access for handle-relative rotation;
readers that omit it can fail to open the active file.
Protect the server directory and all its ancestors against untrusted OS mutation; do not use
network filesystems. External file deletion/replacement disables the POSIX sink; Windows
prevents these operations while open.
Externally oversized files (over 1 MiB) are rejected without truncation. External writers
are unsupported; POSIX detects growth before each write, with no atomic size promise when
an administrator races the writer.

Each new line is at most 575 bytes, UTF-8/ASCII with LF and a UTC prefix. Before the next
line would exceed **1 MiB**, rename the current file to `<basename>.log.1`, replacing the
previous archive, and open a new active file. Retain one archive, at most 2 MiB combined
for exclusively component-managed files. Rotation deliberately replaces the oldest archive;
never use these filenames for other data. No per-event open, synchronous flush or fsync.
An OS partial write can leave an incomplete final line. This is an operational log, not a
lossless or durable audit trail.

## Inspect local and job errors

The following are application helpers, not additional natives. Include `<open.mp>` and
`<omp_postgresql>`. They keep only the most recently inspected diagnostic in private script
buffers and print fixed text/numeric metadata. Add your own access-controlled inspection
or redaction before exposing these strings; they can contain SQL or database values.
Clear the buffers after inspection. The 512-cell sizes are illustrative, not a maximum
diagnostic length: a failed copy leaves the corresponding buffer empty here.

Call this helper immediately after a native returns a local failure, before closing a
statement or calling any other PostgreSQL native:

<!-- pawn-example: error.local -->

```pawn
new PgLocalDetail[512];

stock ReportLocalPgError() {
    if (!pg_last_error(PgLocalDetail)) PgLocalDetail[0] = '\0';
    print("PostgreSQL call rejected; inspect private PgLocalDetail.");
}
```

For accepted jobs, inspect the terminal failure inside their completion callback instead.
There may be no PostgreSQL result at all. The error index identifies an operation, not a
guaranteed result entry; enumerate the available results for SQLSTATE and server text:

<!-- pawn-example: error.job -->

```pawn
new PgJobDetail[512], PgSqlDetail[512];

stock InspectPgFailure(job) {
    new status = pg_job_status(job), category = pg_job_error(job);
    new item = pg_job_error_index(job), sqlstate[6];
    printf("PostgreSQL job %d: status=%d error=%d item=%d", job, status, category, item);
    if (!pg_job_message(job, PgJobDetail)) PgJobDetail[0] = '\0';
    PgSqlDetail[0] = '\0';
    for (new result = 0; result < pg_result_count(job); result++) {
        if (pg_result_sqlstate(job, result, sqlstate) && sqlstate[0]) {
            printf("SQLSTATE: %s", sqlstate);
            if (!pg_result_message(job, result, PgSqlDetail)) PgSqlDetail[0] = '\0';
        }
    }
}
```

`InspectPgFailure` borrows the terminal job; the callback still calls `pg_job_close`
after inspecting it, as in the [insert/delete example](api.md#insert-and-delete-one-record).
It never retries a write. `PG_UNCERTAIN` requires application reconciliation, not an
assumption that nothing changed. A parser/file/transport error can lack SQLSTATE; the
job category/message still explains the failure. Logging configuration and sink failures
do not replace or change these SQL diagnostics.

## Queues, reconfiguration and failures

One Engine owns the existing 256-record numeric ring. Producers use `try_lock`, never wait
for logger locks, allocate, format or access the filesystem. OFF filters before taking the
ring lock. The tick removes at most 63 records plus one drop/failure summary; console output
runs outside locks using a fixed 512-byte formatting buffer. File routing copies records to
a second 256-record numeric ring. One dedicated writer formats/writes them outside locks;
file-only ticks do not format ordinary events. Saturation/contention discards records with
counters. The writer has one bounded pending configuration slot, and coalesces rapid changes.

Changing configuration discards queued old records with accounting. A record already removed
by the writer may finish on its **original** file; an old batch never moves to the new file.
A reentrant console configuration stops the remainder of that old drain. Global configuration
is independent of VM generations; numeric event owner IDs retain their original generations.

Open/write/rotation failures disable file output until another valid configuration requests
it again; no automatic reopen/retry loop. The next enabled main-thread drain reports numeric
`sink_failures` and `log_dropped` counters. File-only output emits only that emergency summary
to console, at most once per 60 seconds when new failures need reporting. OFF suppresses it.
The report excludes OS error strings and paths. Idle failed sinks do not generate artificial
drop reports. A writer-thread exception is contained and requires component restart. SQL job
status, messages, SQLSTATE, callbacks and transactions remain independent of sink failures.

Workers stop/join before logger destruction. Destruction discards pending file records and
joins the writer before DLL unload; no background thread retains component code afterward.
There is no final flush or disk-persistence guarantee. Queue work is finite, but an OS call
on an arbitrarily stalled device can delay that join; there is no absolute shutdown time
bound. No detached writer is used. Console sink exceptions are swallowed/counted; recursive
drains do nothing. Slow console APIs can still delay the main thread. File I/O never runs
on the normal tick, native path or libpq workers. Use OFF/WARN for low routine overhead.
