# Internal diagnostics

Set `OMP_PG_LOG_LEVEL` before starting open.mp. The exact, case-sensitive values are
`OFF`, `ERROR`, `WARN`, `INFO`, `DEBUG`, `TRACE`. Unset means **WARN**. Empty, lowercase,
whitespace or unknown values reject component construction, like other strict settings.
Restart the server after changes; no Pawn recompilation or runtime control native is needed.

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

| Level | Additional events (includes all less verbose levels) |
| --- | --- |
| OFF | No additional diagnostic records, locks, formatting or output. |
| ERROR | Exhausted lane startup/recovery, worker failure, uncertain outcomes and internal job failures. |
| WARN (default) | Other failed/cancelled jobs, retry ordinals, admission rejection, custom callback missing, callback push/execution failure, missing owner and expired transactions. |
| INFO | Logical connection create/close, lane connecting/ready/stopped, VM generation unload. |
| DEBUG | Accepted, running, terminal jobs, completion polled, final callback attempt begin/end, absent optional legacy publics. |
| TRACE | Suppressed notifications, explicit job release and row-block callback attempts. |

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

`-1` means unavailable for that event. No wall-clock timestamp is generated; any server
log prefix belongs to the server. `completion_polled` does not mean Pawn received it.
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
There is no raw diagnostic mode, disk writer, global file/result cache or public debug API.

One Engine owns an immutable-level logger and a fixed 256-record ring. Producers use
`try_lock`, never wait for a logger lock, never allocate/format/flush, and count contention
or capacity drops. The main thread takes at most 63 records plus one coalesced drop report
per tick. Formatting uses a 512-byte stack buffer; the sink runs after releasing locks.
Overflow can lose lifecycle events; logs are diagnostic hints, not an audit trail.
Logging stops at engine destruction; workers join before logger destruction. Undrained
shutdown records are discarded, with no blocking final flush. Reset/unload can leave
bounded old-generation records pending; numeric generations prevent identity confusion.

Sink exceptions are swallowed and counted without changing SQL or public diagnostics.
Recursive drains do nothing. A slow sink can delay the main thread for up to 64 calls
per tick; its wall time cannot be bounded by the component, but workers continue and the
buffer remains fixed. Use WARN or OFF in production; DEBUG/TRACE are temporary diagnostic
levels, with measurable formatting/output cost. No hard real-time or lossless claim is made.
