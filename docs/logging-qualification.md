# Logging faults and real Pawn measurements

This follow-up qualifies PR19's outputs through the existing regression systems.
It adds test fixtures and measurement capture, without changing the 69-native API
or runtime logger. Historical results retain their original source identities.
Configured, executed, merged and published remain separate states.

## Actual Linux filesystem failures

`pg_diagnostics_tests` runs these OS faults in separate CTest processes:

- `--permission` proves `EACCES` from opening a mode-0400 file for append under
  the effective identity. It verifies sink disablement, accounted losses and
  explicit recovery preserving prior bytes. Root returns 77; qualification rejects skips.
- `--short-write` temporarily sets `RLIMIT_FSIZE` to one byte and ignores
  `SIGXFSZ`. A real longer logger write leaves exactly one byte, then disables
  the sink. It checks losses and recovery after restoring the resource limit.
  RAII restores both the limit and signal disposition in this isolated process.
- `--enospc <base>` accepts only a fresh empty caller-owned tmpfs mount point
  of at most 16 MiB. It fills only a private filler file, with a 16 MiB hard bound,
  until the kernel returns `ENOSPC`. It never fills an ordinary host filesystem.
  Zero-byte log files are opened before filling to avoid hiding exhaustion in
  previously allocated pages. Disablement, losses and recovery are checked.

The ENOSPC case also runs PostgreSQL through the same Engine whose Diagnostics
sink fails. It compares a division-by-zero job against a pre-fault baseline:
status, original message, SQLSTATE, error index, item state and retained results.
A subsequent successful query must work while the sink remains disabled. It uses
the disposable profile from `tests/run_postgres.py`; no AMX framework is duplicated.

Linux CTest registers `diagnostics_short_write` and `diagnostics_permission`.
`-DPG_LOG_ENOSPC_BASE=<mount>` additionally registers `diagnostics_enospc`.
The existing qualification runner requires every configured test to execute
successfully. CI mounts a dedicated **1 MiB** tmpfs with the runner uid/gid and
`nosuid,nodev,noexec`, then unmounts it after the job. Regular, ASan/UBSan, TSan
and x86 Linux jobs configure the faults. These are kernel/filesystem failures
in memory-backed tmpfs and a resource-limit short write, not physical drive
failure, Windows volume exhaustion or a device hang.

## Real Pawn/open.mp workload

Compile `tests/logging_performance.pwn` using the pinned Pawn compiler, `-E`,
the public `pawn` include, pinned open.mp stdlib and compiler includes.
This fixture runs inside the official x86 server's actual Pawn interpreter.
Optional arguments extend the existing runner:

```text
tests/run_postgres.py --build <build> --openmp-dir <fresh-server> ...
  --logging-performance-gamemode <logging-performance.amx>
  --logging-performance-csv <output.csv>
```

Usual component, smoke gamemode, runtime libraries and platform-specific server
tools still apply. Use a fresh disposable server: existing
`logs/pg-perf-*.log*` measurement files are rejected. Source-component and fresh
extracted-package CI runs execute the fixture. Test gamemodes are not installed
as public examples and do not change the package's thirteen-example inventory.

Twelve level/destination combinations cover OFF/WARN/DEBUG/TRACE and
console/file/both. Each runs one warmup and seven measured batches, with a fixed
cyclic permutation balancing case order. Every batch submits 64 jobs on two lanes:
56 successful constant SELECTs and eight division-by-zero errors, so WARN emits
actual diagnostic events. Every callback validates identity, status/result or
SQLSTATE and explicitly closes its job.

`GetTickCount` has integer **millisecond** resolution:

- Submission measures the individual `pg_query` call.
- Callback latency measures callback entry minus query entry, including queues,
  PostgreSQL execution and main-thread scheduling; it is not callback CPU time.
- A 10 ms one-shot timer measures lateness relative to its scheduled due time.
  This is actual server-observed scheduling delay, not component-tick execution
  duration. Probes continue for 100 ms after all callbacks finish.
- Batch elapsed includes that observation window. Printing occurs after measured
  callbacks have entered and jobs have closed; output still consumes resources
  and can influence later work.

Exclude warmup round 0 from summaries. Quantization, loopback workload and seven
repetitions do not establish microsecond costs, production throughput or latency
bounds. No timing threshold or causal-speedup claim is used.

## Raw evidence, resources and losses

Capture writes a bounded CSV plus `.resources.csv`, `.drops.csv` and
`.metadata.json`. It verifies complete unique case/repetition/job inventories,
bounded timer rows and numeric ranges. Metadata records UTC time, platform,
source commit/dirty status and hashes of AMX, component, server and measurement
sources. Dirty runs are not labeled as clean commits.

Windows child-process counters or Linux `/proc` provide process-wide CPU/RSS
samples without a dependency. They include component threads, SQL/client work,
output, startup and idle time. Context labels come from output markers and may
lag the actual phase. Discard intervals crossing labels; they neither isolate
component CPU nor prove a per-destination RSS bound. Unavailable counters are
reported explicitly, never substituted with zeros.

Drop sidecars preserve observed numeric summaries by sink. BOTH can duplicate a
summary: never sum both copies as independent loss. Rotation retention and delayed
reporting can omit history; no summary does not prove zero loss. No synchronous
writer-drain or durability guarantee is introduced. Enabled nonempty file sinks,
size bounds and observed failure counters are checked independently of timings.

## Remaining limits

These tests do not qualify privileged Windows reparse mutation races, physical
disk faults, kernel/device hangs, network filesystems, fully instrumented
libpq/OpenSSL, hours-long sustained logging soak or isolated component-tick CPU
duration. A writer blocked in an OS filesystem call can delay safe shutdown join.
Detaching code that could execute after DLL unload is unsafe; no timed detach
is introduced. PostgreSQL statement-position mapping remains deferred; original
SQLSTATE/result diagnostics and parser locations are preserved.

See [logging operations](debugging.md), [performance](performance.md) and
[validation](validation.md) for exact executed evidence and its limitations.
