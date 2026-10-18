# Performance and reproducible benchmarks

Execution moves the pending batch into the job and restores it if enqueueing is rejected;
it does not duplicate the entire batch on each execute call. Each session retains one
unnamed prepared statement. Reusing SQL and OIDs avoids repeated Parse operations without
accumulating plans in PostgreSQL. The default FIFO lane preserves order. Use other lanes
only when the application's concurrency rules permit it.

Twenty snapshots still mean twenty SQL statements, each with its own response and potential
failure. One UNNEST statement can modify twenty rows in a single execution. The gamemode
defines array lengths, duplicate handling, and SQL rules; UPDATE is never transformed
automatically into bulk SQL.

## Direct libpq transport

`tests/benchmark.cpp` compares three modes on a temporary table and checks that every row
received exactly three increments:

```sh
cmake -S . -B build -DPG_BUILD_BENCHMARK=ON
cmake --build build --parallel
python tests/run_postgres.py --build build --benchmark
```

One local run with 1000 rows, Windows x64, GCC 16.1, libpq 18.4, PostgreSQL 17.11, and
TLS verify-full over loopback produced:

| Mode                             | SQL statements | Total microseconds |
| -------------------------------- | -------------: | -----------------: |
| Individual prepared queries      |           1000 |             154686 |
| Pipeline with Sync per statement |           1000 |              73146 |
| One UNNEST statement             |              1 |               8010 |

This is a verification sample, not a statistical distribution or performance guarantee.
It measures libpq directly, excluding Pawn, ticks, queues, and game-server contention.
Pipeline uses windows of 32 operations and Sync per operation to preserve implicit transaction
boundaries. Omitting Sync may change atomicity and error propagation.

For production comparisons, record hardware, exact versions, TLS, RTT, row counts, indexes,
and contention. Repeat after warmup and report percentiles and peak memory. Measure callback
latency separately. Resource limits bound retained objects, but libpq can allocate a large
row or message before the component applies its result limit.

## Engine workloads

`tests/engine_benchmark.cpp` exercises the actual main-thread Engine API, workers, queues,
parameter snapshots, results and stream consumer. It does not invoke Pawn or open.mp ticks.
`PG_BUILD_BENCHMARK=ON` builds both executables. The runner's `--benchmark` runs both after
CTest in the same new, disposable TLS database and stops the server even on failure:

```sh
python tests/run_postgres.py --build build --benchmark \
    --benchmark-rows 1000 --benchmark-repetitions 7
```

For Visual Studio use `--config Release`; executable lookup supports both single- and
multiple-configuration build directories. Rows accept 1..10000, repetitions 1..100.
For direct execution, set `OMP_PG_TEST_SERVICE` to an **explicitly disposable** database
and run `pg_engine_benchmark 1000 7`. It creates one uniquely named ordinary table for
cross-session access and drops it on success. Failure may retain that table for inspection;
the runner's entire database is disposable. Never use a production service.

| Mode                                          | Work per measured repetition                                                                                                   |
| --------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `individual`                                  | N parameterized UPDATE jobs, one SQL each, one lane.                                                                           |
| `snapshots20`                                 | Up to 20 immutable `add` snapshots per job, N SQL UPDATEs, one lane.                                                           |
| `unnest20`                                    | Up to 20 IDs in one integer array per job, ceil(N/20) SQL UPDATEs, one lane.                                                   |
| `heterogeneous20`                             | Up to 20 alternating SQL forms (`value=value+1` and `value=1+value`) per batch job, N equivalent UPDATEs, one lane.            |
| `individual4`                                 | Same jobs as `individual`, explicitly round-robin across four lanes.                                                           |
| `buffered`                                    | One ordered SELECT of N rows, retaining the full result.                                                                       |
| `streaming`                                   | Same SELECT, consuming/releasing blocks of up to 128 rows.                                                                     |
| `file`                                        | Same individual UPDATE as `individual`, loaded by `pg_prepare_file` per job.                                                   |
| `section1`, `section128`, `section1024`       | Same individual UPDATE selected from files containing 1, 128 or 1,024 sections; the complete file is indexed per job.          |
| `file_snapshots20`, `section1024_snapshots20` | Same SQL execution/snapshot grouping as `snapshots20`, through a plain file or a 1,024-section file; one read/capture per job. |

The six file/section modes extend the original seven modes. Fixture generation is excluded
from timings; worker file opening/reading, indexing and capture are included in execution
and completion latency. SQL files use a unique process-local temporary directory and are
removed afterward. They are repeatedly read on a warm filesystem; this is not a cold-disk
benchmark. Different files contain extra unused SQL but submit only the equivalent UPDATE.
Compare `individual` with `file`/`section*`, and `snapshots20` with the two grouped file modes.
The same CSV measures CPU, resident high-water memory, queues and repeated job percentiles.
It does not separately instrument filesystem calls, allocations or lexer/lookup time, and
does not measure concurrent file writers or multi-worker shared-file contention. Those
remain separate profiling tasks; these measurements alone do not justify a global cache.

All update modes return both ID and value and must increment every row exactly once.
The benchmark checks result counts, affected rows, duplicate/missing IDs, every returned
value and final database contents. Read modes start with value=1, check all values and
stream order, then verify the table again. Each mode gets one excluded warmup. Measured
repetitions shuffle mode order with seed 902 (actual order is in CSV; standard-library
shuffle algorithms can differ across toolchains). The four-session pool is connected
before warmup. Each workload admits at most 32 retained jobs; statement/batch construction,
binding, submission, polling, result validation and resource release are timed. Connection,
table creation/reset, initial prepare handles and final database verification are outside
the timing. PostgreSQL Parse/Bind/Execute work still occurs within the timed execution.

CSV contains one row per mode/repetition, with total microseconds, rows/second, logical
SQL command count, job count, SQL text bytes and serialized parameter-value bytes. Bytes
sum the SQL/values for each logical execution, excluding protocol, TLS and Parse framing;
they are **not wire-traffic measurements**. Three distributions use nearest-rank p50/p95/p99:

- `job_*`: accepted job to `Engine.poll()` completion delivery, including queue and polling delay.
- `queue_*`: acceptance to the worker taking the job from FIFO.
- `execution_*`: worker duration, including streaming backpressure when applicable.

These are **job** percentiles: a snapshot or heterogeneous job can contain 20 SQL statements;
UNNEST can update 20 rows in one SQL statement. A read repetition has only one job, so its
three percentiles coincide. Keep the repeated rows; a single-job p99 is not a tail-latency
distribution. `total_us` includes consuming the final stream block, which may occur after
SQL completion delivery. Polling requests a 100-microsecond sleep only when no progress is
observed; operating-system scheduling can make it longer. These timings cannot predict
Pawn callback latency at a server's tick rate. Read throughput and write throughput describe
different workloads and must not be ranked together.

`process_cpu_us` measures user+kernel CPU consumed by this benchmark process and its worker
threads during the measured interval, excluding PostgreSQL/server CPU. Short Windows
intervals can report zero because of CPU accounting granularity. `process_peak_kib` is the
process-wide high-water RSS/Windows working set, including previous modes, warmup, TLS and
allocator caches; it is neither a per-mode allocation peak nor proof of a memory ceiling.
A per-mode memory comparison needs a separate-process profiling harness.

Metadata records libpq/PostgreSQL/TLS versions, OS family, pointer width, compiler,
configuration, sanitizer setting, window, grouping, chunk size and source commit/tree
state **at CMake configuration time** (`unknown`/`unavailable` outside a Git checkout).
Reconfigure after source edits. The runner also
prints host OS/CPU information and the disposable fixture's loopback/verify-full condition;
RTT is not measured. A direct run labels network conditions as service-defined without
printing credentials. Keep metadata, CSV, compiler configuration and hardware conditions
together. Compare several independent process runs; seven repetitions in one process
remain a local sample, not a performance guarantee.

The existing CI matrix enables these executables and runs `engine_benchmark_smoke`
(41 rows, two measured repetitions plus warmup) under CTest, including ASan/UBSan.
The non-multiple of 20 checks the partial final group. It is a correctness check with no
timing thresholds; its Debug/sanitizer timings are not release benchmarks. Logs retain
the CSV. Set `--benchmark` explicitly for larger measurements; no extra CI matrix is added.
Comparative Pawn/tick measurements remain separate qualification work. Finite ten-minute
load and ThreadSanitizer qualification are recorded in [validation](validation.md);
instrumented dependencies and hours-long soaks remain unestablished. Pipeline remains
confined to the standalone transport probe.

### Recorded Engine sample, October 15, 2026

The [raw CSV](https://github.com/itskoleban/omp-postgresql/blob/master/tests/benchmarks/windows-x86-engine-4ca1674.csv)
contains all 49 measured records (seven modes, seven repetitions), plus metadata from a
clean configuration of `4ca1674826aaf3cd090753263b4e5e183281e65c`. Windows x86 Release,
MSVC 19.51.36248.0, libpq 17.6 and disposable EDB PostgreSQL 17.11 used TLS 1.3 verify-full
over loopback. Host: Windows 11 build 26300, Intel64 family 6/model 186/stepping 2,
12 logical CPUs. RTT, server CPU and background host load were not measured or controlled.
An earlier stuck build process was stopped before this recorded run; earlier trial
timings under concurrent compilation are not the reference CSV.

| Mode              | Rows per repetition | SQL commands | Jobs | Median total microseconds across seven repetitions |
| ----------------- | ------------------: | -----------: | ---: | -------------------------------------------------: |
| `individual`      |                1000 |         1000 | 1000 |                                             894670 |
| `snapshots20`     |                1000 |         1000 |   50 |                                             619681 |
| `unnest20`        |                1000 |           50 |   50 |                                              64464 |
| `heterogeneous20` |                1000 |         1000 |   50 |                                            1108964 |
| `individual4`     |                1000 |         1000 | 1000 |                                             469937 |
| `buffered`        |                1000 |            1 |    1 |                                              16008 |
| `streaming`       |                1000 |            1 |    1 |                                              64651 |

Every repetition passed the data checks. Process peak working set reached 15,516 KiB
across all modes. Per-repetition CPU, throughput and job/queue/worker percentiles are in
the CSV; the medians above are total workload times, not pooled job percentiles. This is
one process run on a shared workstation. Variation, grouping, SQL preparation changes,
poll scheduling and read/write differences prevent treating the table as a general
ranking or production sizing evidence. No pipeline or lane-selection change follows
from this sample.

## Custom callback costs

Legacy completion dispatch still performs one owning-VM/public lookup and two scalar pushes.
A custom descriptor adds a bounded allocation and one vector allocation when arguments exist;
each non-SSO string can add an allocation. The native copies at most 4096 logical payload
bytes, validates addresses and optionally resolves the public before accepting work. Dispatch
does another public lookup and pushes the job plus 0..16 extras. Strings temporarily expand
to Pawn cells. Descriptor storage is released at dispatch rather than job closure.

No callback lookup cache is introduced. File preparation adds no extra file read or parse:
both public interfaces feed the existing worker capture. The source-kind enum removes
implicit branching states and is not a claimed speedup. Existing Engine benchmark timings
above predate this change; they are not measurements of custom callback dispatch. Callback
copy/dispatch benchmark scope and results are recorded in [the audit](api-modernization.md).

The [raw Windows x86 callback CSV](https://github.com/itskoleban/omp-postgresql/blob/74a298ca364dc7750ea0d79bc07618e9629a3d45/tests/benchmarks/windows-x86-callbacks.csv) records
seven measured repetitions after warmup, rotating mode order. It binds the source with
canonical-LF SHA-256 values. With 64 jobs per repetition on two lanes, median nanoseconds
for submission/dispatch were:

| Current implementation path | Submission, 64 jobs (ns) | Dispatch, 64 jobs (ns) |
| --- | ---: | ---: |
| Legacy global, old native arity | 156500 | 71400 |
| Custom, no extras | 143900 | 81200 |
| Custom, two integer extras | 216200 | 81300 |
| Custom, 96-byte string | 465900 | 114300 |

Submission includes validation, copying and enqueue with concurrent workers; dispatch
includes Engine polling and SDK-test-double allocation/recording. The double replaces the
Pawn interpreter. These are current-path comparisons on one shared workstation, not a
before/after benchmark, real Pawn latency or production throughput evidence. Variation
prevents interpreting the no-extra submission difference as a speed improvement. No timing
threshold is used. Installed packages omit test sources; use the repository raw-data link
in the [API audit](api-modernization.md) when reading an installed guide.


## Pre-stable diagnostics measurements

Exact compiled source: `67cdaccf5a42350523303fff5548e386c07b006e`, Windows 11 10.0.26300,
Intel Core i5-13420H (8 cores/12 threads), MSVC 19.51.36248 Release x86, patched libpq 17.6,
disposable TLS PostgreSQL 17.11. [Raw CSV](https://github.com/itskoleban/omp-postgresql/blob/8beceb53ab25b4c37092ebf184a29ebe08f2fb56/tests/benchmarks/windows-x86-logging.csv)
contains all 112 samples and canonical SHA-256 identities. Reproduce with
`logging_integration` through `tests/run_postgres.py`; baseline callback modes also remain
in `natives_integration`. No global file/public/result cache was introduced.

Each sample submits 64 jobs on two lanes. Seven measured repetitions follow one warmup;
callback modes rotate within each repetition. Levels run sequentially OFF/WARN/DEBUG/TRACE
on a shared workstation. This uses the **Pawn SDK test double**, real PostgreSQL and a
counting sink. Dispatch includes poll, test-double public/push/Exec bookkeeping and one
bounded log drain, with no console/disk I/O. It is not actual Pawn/open.mp tick performance,
nor an isolated before/after experiment. Fixed level ordering and system noise prevent
causal submission-speed claims; lower observed values are not claimed as speedups.

Median microseconds per 64 jobs, **submission / dispatch including bounded drain**:

| Callback | OFF | WARN | DEBUG | TRACE |
| --- | ---: | ---: | ---: | ---: |
| Legacy global | 354.5 / 68.0 | 152.7 / 73.1 | 95.6 / 261.9 | 121.1 / 256.2 |
| Custom, no extras | 171.9 / 71.9 | 182.3 / 75.9 | 167.6 / 364.0 | 198.5 / 260.7 |
| Custom, two integers | 226.4 / 75.5 | 216.8 / 84.8 | 154.0 / 276.7 | 206.5 / 246.2 |
| Custom, 96-byte string | 433.9 / 96.9 | 438.5 / 117.7 | 410.9 / 308.3 | 493.8 / 293.9 |

These burst samples report zero drops at OFF/WARN, 128 per sample at DEBUG and 129 at
TRACE. They intentionally retain 64 completions until one tick; more records are generated
than the ring/tick budget can retain. TRACE accounting includes release records following
the timed dispatch. This verifies bounded loss, not complete event delivery. Formatting
has a visible dispatch cost at verbose levels; use WARN/OFF for normal operation.
The CSV permits nearest-rank percentiles across seven batch samples (p95 is their maximum),
but supplies no per-job/tick percentile or allocation/RSS/CPU profile. The logger object
measured 20,584 bytes on this x86 build, with 80-byte records; disabled mode retains that
fixed storage. Concurrent-flood, sink-failure/reentry and repeated lifetime tests establish
finite behavior, not the absence of bugs or a hard bound on overall process memory.
