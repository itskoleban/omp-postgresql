# Validation results and known limitations

The component remains **0.1 experimental**. Passing the current suite is evidence for the
listed scenarios, not complete production qualification. Engine tests and real open.mp
loading are recorded separately.

## Automated coverage

Windows builds that supply `PG_LIBPQ_SOURCE_DIR` also run `libpq_windows_mutex_unit`
against the actual dependency C source with a forced stale-initializer schedule, and
`libpq_process_startup_integration` repeats the existing eight-lane lifecycle test in
20 fresh processes. Repeating rounds in one process does not repeatedly initialize
libpq's process-global mutex. CI enables both tests for the Windows x86 source build;
the qualification inventory automatically includes them.

Checks remain active in Release builds. `tests/run_postgres.py` creates an isolated,
loopback-only PostgreSQL cluster with TLS and always stops it. Integration tests return
77 when no service profile is supplied; a skipped test is not a successful integration run.

The disposable runner enforces this distinction automatically: it inventories the configured
CTest cases, removes any stale qualification report, runs all cases with `--no-tests=error`
and `--output-junit`, then requires an exact inventory match with every case executed
successfully. A zero CTest exit code alone is insufficient: disabled tests, return-code
skips and output-based skips fail qualification. Missing, duplicate or unexpected cases,
missing reports and malformed XML also fail. Optional component/benchmark cases are taken
from the configured build, so no fixed test count substitutes for the actual inventory.
This verifies execution of configured tests; it cannot detect an intended test omitted
from CMake altogether or prove that a test's assertions are sufficient.

CI retains `Testing/Temporary/qualification.xml` with the existing test logs, including
failed runs when CTest produced a report. `--only-openmp` remains a separate server check
and does not claim a new CTest run. Offline `test_ctest_qualification.py` regressions invoke
real CTest, including skipped/disabled cases where CTest itself returns success.

| Suite                              | Coverage                                                                                                                                                                                                                                       |
| ---------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `unit`                             | Parameter validation, immutable snapshots, limits, SQL/file confinement and UTF-8.                                                                                                                                                             |
| `integration`                      | Pools, FIFO, 256 ordered executions, NULL, UNNEST, partial failure, prepared-plan invalidation, transaction finishing capacity/expiry, cancellation, backend loss, reconnection and shutdown.                                                  |
| `types_unit`                       | Sixteen scalar/array OID pairs, BIGINT boundaries, nullable array escaping, all 256 bytes, empty BYTEA and invalid encodings.                                                                                                                  |
| `types_integration`                | Exact numeric/temporal values, native boolean output versus text casts, scalar/array OIDs, nullable/empty arrays in prepared, direct and heterogeneous execution, BYTEA and server format errors.                                              |
| `extensions_integration`           | Heterogeneous snapshots/rejection/partial outcomes, lane metrics, BEGIN options/defaults, savepoint recovery, 10000-row streaming, zero-column tuples, provisional rows, slow consumers, delivery-only timeout, cancellation and owner unload. |
| `stress_integration`               | Seeded four-lane shared-row contention, individual/homogeneous/heterogeneous executions, FIFO, retained-job saturation, reserved COMMIT capacity and repeated cancellation/expiry/disconnect/unload/destruction with blocked streams.          |
| `engine_benchmark_smoke`           | Optional benchmark build: thirteen Engine workloads (including file/section variants), partial 20-item groups, repeated exact returned-data/final-table validation; no timing thresholds.                                                      |
| `natives_unit`                     | Component builds: public Pawn/native name and arity parity, malformed argument counts, seeded invalid addresses, packed/unpacked strings, array limits/NULL masks, allocation failures and output preflight.                                   |
| `natives_integration`              | Actual native wrappers with disposable PostgreSQL: snapshot ownership after memory reuse, nullable/empty arrays and BYTEA, rejected rebind preservation, output overlap/capacity/address checks and VM ownership.                              |
| `connection_lifecycle_integration` | Repeated eight-worker startup, ready-pool destruction, startup disconnect and refused-connection retry interruption.                                                                                                                           |
| `connection_stalls_integration`    | Four-worker stalls at SSL negotiation, TLS handshake and startup response; deadline/retry exhaustion, disconnect, owner unload and destruction while an independent owner still executes.                                                      |
| Real open.mp                       | Native loading, three GMX reloads, unloading a filterscript with pending SQL, callback state restoration, typed batches, savepoint recovery, streaming and gated world initialization.                                                         |

The TLS runner checks verify-full, incorrect host identity/password, and client certificates.
A real immediate PostgreSQL restart is exercised. A proxy drops COMMIT CommandComplete:
the client reports UNCERTAIN and a subsequent connection verifies exactly one committed row.

## Results recorded on October 13, 2026

The following is historical evidence from
[PR #1](https://github.com/itskoleban/omp-postgresql/pull/1), integrated as `48b12af`.
Before packaging work, `master` was verified at `859bdf5`, whose
[CI run 38505660890](https://github.com/itskoleban/omp-postgresql/actions/runs/38505660890)
also succeeded. Later package qualification is recorded separately below.

| Platform    | Current evidence                                                                                                                                                                                                                     |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Windows x64 | Local GCC/MinGW 16.1, libpq 18.4, EDB PostgreSQL 17.11: all five CTest suites passed. Counts: 26 core unit, 146 core integration, 97 type unit, 532 type integration, 606 extension checks.                                          |
| Windows x86 | Local MSVC 19.51, TLS-enabled libpq 17.6, EDB PostgreSQL 17.11: all five suites passed with the same counts. Real open.mp 1.5.8.3079 loaded the component and passed smoke/GMX, unload, callback, extension and bootstrap scenarios. |
| Linux x64   | Ubuntu 24.04 CI, GCC 13.3/libpq 18.6: all five suites and all five Pawn examples passed in both normal and ASan/UBSan jobs, with leak detection enabled and no sanitizer diagnostics.                                                |
| Linux x86   | Ubuntu 24.04 CI, TLS-enabled libpq 17.11: all five suites and all five Pawn examples passed; the real open.mp dynssl server passed smoke/GMX, pending unload, callback, extension and bootstrap scenarios.                           |

[CI run 38477565124](https://github.com/itskoleban/omp-postgresql/actions/runs/38477565124)
completed successfully on code/documentation commit `a7a0303`, with **all five jobs green**:
Windows x64, Windows x86 MSVC/open.mp, Linux x86/open.mp, and Linux x64 with sanitizers
off/on. The Windows CI uses an independent PostgreSQL 17.11 server and libpq 18.6 on x64,
17.11 on x86. This record was added in a subsequent documentation-only commit; execution
code, examples, tests and workflow are identical to the validated commit.

All five examples compiled locally with Pawn 3.10.11, 32-bit cells and official includes,
with `-E` treating warnings as errors. Callback sleep/divide-by-zero are deliberate recovery
tests, not ignored failures. All locally tested server processes exited with code 0.
The corrected extension test produced `PG EXTENSION PASS`; bootstrap produced
`PG BOOTSTRAP PASS: properties=2 vehicles=1 clans=2`.

The original array regression was an incorrect BOOLEAN text-cast expectation: element OID
16, array OID 1000, expected `t`, actual `true`. Native output remains `t`; the regression
now checks both representations separately, NULL cells and OIDs. No binder change or test
disablement was required. Real server testing also found indexed Pawn string slots built
without explicit capacity; supplying their 24-cell width fixed the empty BIGINT input.

The Linux server harness sent unload and GMX without waiting for acknowledgement. The
open.mp console at commit c6759bd8d265171ae3d86598895a23d5a8d92a3b stores one pending command,
so an immediately following command can replace it before a tick processes it. The harness
now waits for `PG UNLOAD EXIT` before GMX; missing acknowledgement remains a test failure.

A first local run with the MSYS2 PostgreSQL 18.4 **server** crashed a backend on the
multiple-command rejection test (exception `0xC0000028`), producing secondary connection
failures. Validation therefore uses the independent EDB 17.11 server; client libpq 18.4
continues to pass. The MSYS2 server crash is unresolved external-toolchain evidence, not a
passing platform run or a reason to remove the multiple-command test.

The [current CI history](https://github.com/itskoleban/omp-postgresql/actions/workflows/ci.yml)
is authoritative for later commits. CI retains test logs, compiles all examples on Linux,
and runs real x86 servers on both operating systems. Sanitizer jobs retain their own results;
matrix fail-fast is disabled. Superseded commits may be cancelled by concurrency control.

Earlier October 12 local results covered the core baseline on Windows x86/x64 and Linux
x86/x64, including Linux leak detection. They do not prove extension coverage for a later
commit. The tested Linux server used native Linux storage because running its distribution
directly from an NTFS bind mount exposed a server filesystem metadata error.

## Experimental package qualification on October 14, 2026

[CI run 38648946591](https://github.com/itskoleban/omp-postgresql/actions/runs/38648946591)
completed successfully with **all six jobs green** on branch commit `24cc29d`.
The pull-request checkout and both package manifests identify the exact preview merge
`53ee0d42daf98e1a1a6215afe72f29eed5d66c26`; this is distinct from the branch head and does
not represent an owner-approved merge into `master`.

- Windows x86 MSVC/libpq 17.11 and Linux x86/libpq 17.11 passed all five core suites and
  real open.mp scenarios, then assembled two byte-identical ZIPs per platform. SHA-256,
  architecture, import dependencies, symbol versions on Linux, legal notices, payload
  inventory and relative documentation links were verified.
- All five examples compiled again with warnings as errors using the **extracted** include.
  Fresh official open.mp 1.5.8.3079 distributions loaded the extracted component/runtime,
  with development runtime search paths removed, and passed four smoke runs/three GMX,
  pending filterscript unload without a late callback, callbacks, extensions and bootstrap.
- Both validated ZIPs and their checksum files were uploaded. The sixth job downloaded
  those artifacts and repeated checksum, manifest, architecture and extraction checks.
  Packages are available as [Windows x86](https://github.com/itskoleban/omp-postgresql/actions/runs/38648946591/artifacts/11934226810)
  and [Linux x86](https://github.com/itskoleban/omp-postgresql/actions/runs/38648946591/artifacts/11934006322)
  Actions artifacts, subject to their retention period.
- Linux x64 normal/ASan/UBSan and Windows x64 engine jobs also passed all five suites.
  The 16 package-verifier regressions passed. No integration suite was skipped in these runs.

Local Windows qualification separately used MSVC 19.51, libpq 17.6 and isolated EDB
PostgreSQL 17.11. All five suites passed (26/146/97/532/606 checks), all extracted examples
compiled with Pawn 3.10.11 `-E`, and a fresh server passed the same scenarios with development
DLL directories removed from PATH. The locally tested ZIP belongs to `4e76245`; it is
separate from the final CI artifacts. Both local assemblies had identical SHA-256.

Earlier CI attempts exposed two packaging defects: downloaded build inputs were incorrectly
classified as a dirty source checkout, and a fixed local MSYS2 path did not exist on the
hosted Windows runner. The final procedure rejects tracked changes and untracked install
assets while allowing non-installed build inputs, and obtains the MSYS2 path from `cygpath`.
The temporary-Git regression verifies the source-cleanliness policy. Failed/superseded runs
are not package qualification.

These artifacts remain **0.1.0 experimental**. No RC tag, release, automatic merge or new
performance measurement was made. See [installation](installation.md) and [packaging](packaging.md).

[PR #2](https://github.com/itskoleban/omp-postgresql/pull/2) was subsequently merged
as `a02a0d6`. Its [master CI run 38654671286](https://github.com/itskoleban/omp-postgresql/actions/runs/38654671286)
and the documentation-only branch run `38650068506` both completed successfully.
The master run had all six jobs green, with no skipped steps; the preview-merge evidence
above remains a historical record of the initial package qualification.

## Local stress validation on October 15, 2026

On [PR #3](https://github.com/itskoleban/omp-postgresql/pull/3), Windows x86 MSVC 19.51,
libpq 17.6 and the disposable EDB PostgreSQL 17.11 TLS fixture passed all six CTest suites,
without skips. The default seeded run completed 4,640 SQL operations and ten lifecycle
rounds in 13.20 seconds; process peak working set was 16,596 KiB. After tightening the
unload assertions to avoid requiring delivery to a dead VM, seed `42` with 25 rounds
passed 11,600 SQL operations and every lifecycle action five times, with a 16,748 KiB peak.
Check counts vary because the unload loop asserts on each polling tick.

The 16 package regressions, clang-format, cmakelang, actionlint and whitespace checks also
passed locally. These are engine/resource stress results, not new local open.mp loading,
cross-platform results, a throughput benchmark or proof of a maximum memory bound.
Current PR CI remains authoritative for remote sanitizer and package qualification.

## Engine benchmark validation on October 15, 2026

Code commit `4ca1674` adds the optional Engine benchmark and enables its correctness
smoke in the existing CI matrix. Locally, Windows x86 MSVC 19.51/libpq 17.6 with disposable
TLS EDB PostgreSQL 17.11 passed **all seven CTest suites, without skips**, then both
standalone benchmarks. The Engine run verified 1,000 rows across seven modes with one
warmup and seven measured repetitions each; its 49 CSV records and interpretation are
in [performance](performance.md#recorded-engine-sample-october-15-2026). No component,
native, Pawn include or example implementation changed.

The benchmark target compiled without diagnostics. Invalid zero/negative/out-of-range
and trailing-character row arguments were rejected; a missing service returned 77.
Configuration from an exported source tree without Git succeeded and explicitly marked
identity `unknown`/`unavailable`. The 16 package regressions, actionlint, C++/CMake/Python
formatting and whitespace checks passed. Sandbox attempts initially failed on temporary
file permissions; the recorded database/package runs used the authorized local environment.
The first general MSBuild invocation stalled and was stopped; targeted builds succeeded.
No passing build is inferred from that interrupted invocation.

Recovery found `master` at `b00fe2f`, PRs #1-#3 merged and no open PRs. The existing
[experimental release](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-experimental.b00fe2f23787)
was already public at recovery, published October 15 at 14:48:59 UTC. Its
[CI run 38754771577](https://github.com/itskoleban/omp-postgresql/actions/runs/38754771577)
passed all six jobs with no skipped steps, including downloaded x86 package verification.
That qualifies the earlier package commit; it does not qualify the later benchmark change
or constitute RC/Astra evidence. This development run did not publish a release or tag.

The subsequent [master CI run 38834343351](https://github.com/itskoleban/omp-postgresql/actions/runs/38834343351)
on `c0ac4dc578f9051e54d3136a1bcff4a02945420e` passed all six jobs with no skipped steps.
All seven CTest suites, including the Engine benchmark smoke, passed across the existing
engine matrix and Linux ASan/UBSan. Both x86 packages passed fresh-server tests and
downloaded-artifact verification. This qualifies the benchmark addition; it predates the
native-boundary suites described below.

## Native boundary validation on October 15, 2026

Windows x86 MSVC 19.51/libpq 17.6 with disposable TLS EDB PostgreSQL 17.11 passed the
initial nine-suite CTest run in 46.05 seconds. The final native suites passed 13,852
offline checks and 82 database-backed checks. GCC/MinGW Windows x64 also compiled the
same wrappers and passed the 13,852 offline checks. The 16 package regressions and
C++/CMake formatting checks passed. No runtime implementation or public API changed.

A full repetition passed both final native suites but timed out in the existing
`extensions_integration` (90 seconds) and `stress_integration` (180 seconds); the other
seven suites passed. Those failures are retained as unresolved intermittent evidence,
not replaced by the earlier successful run. Direct extension execution against the same
disposable cluster subsequently passed all 606 checks in 9.38 seconds. Further repeated
execution and CI qualification must distinguish a worker/shutdown defect from a local
toolchain or runner issue. These timeouts prevent treating this work as RC qualification.

## Connection lifecycle investigation on October 16, 2026

Recovery verified `master` at `c649966` with PRs #1--#4 merged and no open PRs.
[CI run 38841611296](https://github.com/itskoleban/omp-postgresql/actions/runs/38841611296)
passed all six jobs without skipped steps, including downloaded x86 distribution verification.
That is baseline evidence; it predates the new connection lifecycle suite.

Five direct extension/stress pairs passed against the existing disposable TLS fixture,
without reproducing the prior timeouts. With the new suite, Windows x86 MSVC 19.51,
libpq 17.6 and disposable EDB PostgreSQL 17.11 passed all ten CTest suites without skips
in 47.36 seconds; the final diagnostic build repeated all ten successfully in 41.47 seconds.
A separate 100-round connection run passed 3,400 checks in 43.80 seconds,
covering 3,200 worker starts and 800 successful backend-identity queries. The configured
debugger-on-stall helper did not trigger, so no blocking-thread stack was obtained.
Missing-service and invalid-round argument guards and all 16 package regressions also passed.

These are correctness/stress results, not comparative performance measurements. No runtime
fix is inferred from passing repetitions. The original extension/stress timeouts remain
unresolved and block RC qualification. The new flushed phase/error diagnostics and eight-lane
regression improve the evidence available on a future failure without changing the API,
runtime, test assertions or existing test deadlines.

## Remaining release qualification

- Named SQL sections extend the API to 69 natives. Their current implementation, tests,
  file/capture limits and examples are described in [named SQL sections](sql-sections.md).
  They still require qualification of the exact candidate commit before RC inclusion;
  historical evidence below does not qualify newly added behavior.
- x64 builds qualify the engine and compilation. The tested distributed open.mp server is
  x86; component, client libraries and server architectures/ABIs must match.
- Validation applies to these tested compiler/client/server combinations. Requalify changes
  to execution code, the bridge, dependencies or the workflow; other distributions are not
  covered by this successful matrix.
- Prolonged network partitions, interruption at all BEGIN/ROLLBACK stages, exhausted
  handles and extreme backpressure need more adversarial coverage. The combined lifecycle
  stress described below covers a bounded, reproducible subset of cancellation/expiry/shutdown.
- One startup/destruction deadlock has a native-stack diagnosis and deterministic
  dependency regression; see the October 17 Windows libpq investigation below. Its fix
  requires the corrected DLL. Every historical extension/stress timeout has not been
  independently mapped to that defect, and other client providers require verification.
- Malformed AMX file/bytecode fuzzing, prolonged soak testing and ThreadSanitizer remain pending.
  Native argument/address tests cover a bounded SDK memory model, not the AMX loader or interpreter. Stress tests
  record process peak memory and verify object release, but do not establish a strict RSS limit;
  libpq can allocate a large row before the component checks it. DNS/authentication/file providers can delay
  shutdown independently of socket deadlines.
- COPY transfer and receiving LISTEN/NOTIFY are excluded from the 0.1 candidate scope.
  Their dedicated protocol states, bounded queues and lifecycle tests are not implemented.
  Ordinary queries reject COPY/LISTEN/UNLISTEN. Parameterized `SELECT pg_notify($1,$2)` can
  send notifications; notifications are not persistent and cannot be recovered after a
  receiving session disconnects. See the [protocol scope decision](extension-audit.md).
- Pipeline, automatic lane selection and named-plan caches remain optional optimizations.
  The sequential FIFO engine does not depend on them. Benchmarks now measure direct
  libpq transport and repeated Engine job/queue/worker latency, CPU and process peak memory.
  Comparative Pawn/tick latency remains pending; see [measurement definitions](performance.md).
- The complete lifecycle is demonstrated by the smoke, extension and bootstrap examples
  together; the value of a single combined example still needs review. Experimental x86
  distribution is now validated; final RC artifacts must be rebuilt and qualified at the
  approved release commit after stress testing and an actual independent Astra audit.
  No release was published or merged automatically.

## Reproducing validation

### Stalled connection protocol tests

`connection_stalls_integration` uses the actual Engine/libpq with three loopback-only
fault peers created by `tests/run_postgres.py`. They withhold the SSLRequest response,
accept SSL but withhold the server handshake after ClientHello, or receive a plaintext
StartupMessage without sending authentication or ReadyForQuery. These are deliberate
[startup protocol boundaries](https://www.postgresql.org/docs/17/protocol-flow.html);
the peers never connect upstream, request credentials or execute SQL. The independent
healthy database connection continues to use the disposable PostgreSQL verify-full profile.

For each phase, four lanes exercise connection deadline expiry with exactly one retry,
explicit disconnect, owner unload/reaping, and Engine destruction. Fixture markers prove
receipt at the intended boundary before teardown begins; no startup sleep substitutes for
that acknowledgement. The test rejects SQL on incomplete/failed connections and checks zero
queued/dispatched commands. A second owner's real `SELECT 42` must complete while peers
are stalled. No application callback is fabricated by the peer; VM callback filtering
remains covered by the separate native/open.mp suites.

Timeout cases use the Engine's 1-second attempt deadline despite a 60-second libpq
`connect_timeout`. The other cases use a 60-second Engine deadline and require shutdown
within a generous 10-second assertion bound; this is a liveness guard, not a latency
benchmark. The enclosing CTest deadline is 90 seconds. Each full run observes 20
handshakes/closures per phase (60 total), and the runner fails on fixture errors or missing
coverage. Missing service/fixture environment returns 77 and is never counted as passing.
The peers retain bounded buffers, at most 256 accepted clients per phase, and explicitly
join their threads during cleanup. Their `stall-*.log` summaries are copied into the
build's `Testing/Temporary` directory and uploaded with existing CI test artifacts.

Five Python regressions independently cover fragmented input, boundary acknowledgement,
withheld responses, partial peer shutdown, malformed startup and fixture thread cleanup:

```sh
python3 -m unittest discover -s tests -p test_stall_server.py -v
python3 tests/run_postgres.py --build build --config Release
```

This adds bounded network-stall coverage. It does not establish a cause for the historical
local extension/stress timeouts, qualify DNS/authentication provider blocking, or replace
long network-partition/soak testing.

Local validation on October 16, 2026 (Windows x86, MSVC 19.51, libpq 17.6 and disposable
TLS EDB PostgreSQL 17.11): all eleven CTest suites passed without skips in 55.80 seconds.
The new stall suite passed in 8.79 seconds; each peer recorded 20 accepted, acknowledged
and closed connections. All 21 Python package/fixture regressions passed. An additional
reproduction using the historical disposable cluster passed two extension/stress repetitions
with each of Kitware and MinGW32 CTest (eight executions total); no hang or stack was
captured. These results do not resolve the historical timeouts or measure comparative
performance. Restricted local tooling also exposed sandbox process/temporary-file failures;
the successful validation used the authorized runner outside that sandbox.

### Concurrent connection lifecycle

`connection_lifecycle_integration` exercises the actual Engine with eight physical session
workers per pool. Ten rounds each cover immediate destruction while connections may still
be starting, destruction after all lanes execute a query, explicit disconnect during startup,
and destruction after a refused connection has entered retry handling. The ready-pool case
checks eight distinct PostgreSQL backend PIDs. Disconnect must reject new SQL, dispatch no
commands and eventually acknowledge stop on every worker. This is a bounded scheduling
stress test; it does not force a particular instruction-level interleaving.

The suite requires both `OMP_PG_TEST_SERVICE` and `OMP_PG_TEST_BAD_CONNECT`; the disposable
TLS runner supplies them and CTest runs it in the existing platform/sanitizer matrix.
Missing profiles return 77 (skipped, never a passing integration run). The default timeout
is 90 seconds. `OMP_PG_CONNECTION_ROUNDS=1..1000` changes the number of rounds for direct
diagnostic execution; large runs need an independently managed deadline and disposable
fixture rather than an increased CI timeout.

Each phase is flushed before creating or destroying an Engine. Extension and load stress
tests also emit phase markers and print assertion/deadline failures before unwinding, since
a stalled destructor can otherwise hide the original failure. The last marker narrows the
investigation but does not identify a root cause without further evidence such as thread
stacks. Successful repetitions do not resolve the previously recorded local timeouts.

### Pawn native boundary tests

With `PG_BUILD_COMPONENT=ON` and `BUILD_TESTING=ON`, CTest builds `pg_native_tests` from the
actual `src/natives.cpp` and `src/component.cpp`, linked against the pinned official SDK
and the same engine. It does not add a replacement native implementation or a database
dependency to the offline `natives_unit` test. Existing CI jobs run both new suites;
`PG_SANITIZE=ON` also instruments the native and component code in this executable.

`tests/pawn_memory.hpp` implements only native registration and checked address lookup.
Its allocated heap and stack regions have an inaccessible gap; unsupported SDK methods
fail the test. The unit suite independently reads names and arities from the public Pawn
include and checks all 69 registered natives, wrong counts, null argument blocks and
documented error sentinels. Seed `0x414d5801` generates 4,096 additional address cases.
Explicit cases cover negative/unaligned/out-of-range addresses, a successful lookup with
a null physical pointer, injected allocation failure, unterminated packed/unpacked strings,
invalid UTF-8, array dimensions, NULL masks, unloaded/unknown VMs and complete output
preflight without partial writes. The seed fixes input generation, not thread scheduling.

`natives_integration` uses the runner's disposable TLS service. It executes packed and
unpacked string snapshots after overwriting their Pawn buffers, checks preserved staging
after a rejected rebind, and round-trips flat/nullable/empty arrays and binary NULs. Real
results exercise byte-buffer/length overlap, insufficient capacity, invalid destination
addresses, exact-capacity copies, SQL NULL, BIGINT narrowing rejection and cross-VM handles.
No persistent application table is created. As with other integration suites, missing
`OMP_PG_TEST_SERVICE` returns 77 and must not be reported as a successful integration run.

```sh
ctest --test-dir build -C Release -R '^natives_unit$' --output-on-failure
# Run both native suites with the rest of CTest in a fresh TLS database:
python3 tests/run_postgres.py --build build --config Release
```

These tests call the registered C++ wrappers through an SDK test double. They do not
execute malformed AMX files, emulate all interpreter behavior, exercise asynchronous
Pawn callbacks or replace the separate real open.mp loading/lifecycle regressions.

### Combined engine stress

`stress_integration` is part of the existing CTest run on every CI engine platform,
including Linux ASan/UBSan with leak detection. It uses the same disposable TLS cluster;
the runner never opens a pre-existing cluster. Direct execution against a service is
appropriate only for a database that can be discarded: this suite creates uniquely named
ordinary tables so different physical sessions can verify the same writes. Tables are
dropped on success; the disposable cluster retains failed fixtures for investigation.

The default seed is `1592594996`, with ten rounds. The load phase executes **4,640 SQL
operations** across four workers: 20 captured parameter sets per homogeneous job, ordered
heterogeneous updates/SELECTs, and individual updates. Every update contends on a shared
row before updating its lane's row, using the same lock order. Assertions check every
returned counter, FIFO within each lane, affected rows, heterogeneous result order,
quiescent worker statistics and the final shared counter. Closing statements and batches
immediately after acceptance verifies snapshot ownership throughout the load.

Ten lifecycle rounds rotate cancellation, transaction expiry, logical disconnect, owner
unload and engine destruction. Each round retains current and pending eight-row blocks
on two streams, holds a provisional transaction write, fills the 32-job limit, randomly
cancels queued writes and submits COMMIT using its reserved slot. The test checks that
interrupted dispatched streams remain UNCERTAIN, pending writes/COMMIT never execute or
replay, reservations are released, reconnecting lanes are usable, unloaded owners receive
no job/row callbacks, and weak references to jobs and blocks expire after resource release.
A separate connection verifies the provisional write rolled back and no queued write ran.
Destruction is exercised without waiting for completion dispatch.

Every run prints its seed, round/action, libpq version, pointer width, operation count
and process peak resident memory in KiB. Windows uses peak working set; Linux uses
`getrusage(RUSAGE_SELF).ru_maxrss`. This process-wide high-water value includes startup,
TLS, allocation caches and all earlier phases; it is neither a leak measurement nor a
memory ceiling. ASan/LSan and explicit ownership checks provide separate evidence.
CTest logs are retained by the existing CI artifact steps. No latency or throughput
comparison is inferred from stress-test duration.

For a second seed or a longer bounded run, set the variables before invoking the runner:

```sh
OMP_PG_STRESS_SEED=42 OMP_PG_STRESS_ROUNDS=25 \
python3 tests/run_postgres.py --build build
```

In PowerShell use `$env:OMP_PG_STRESS_SEED='42'` and
`$env:OMP_PG_STRESS_ROUNDS='25'`. Seeds accept decimal values from 1 to 4294967295;
rounds accept 1 to 100. At least five lifecycle rounds are needed to visit every action.
The SQL mix and selected cancellation bits are seeded; thread/network scheduling is not
claimed to be deterministic. Deadlines fail the test instead of accepting a hang.
Manual `build-and-test` workflow runs expose `stress_seed` and `stress_rounds` inputs
for the same CTest suite across the existing matrix, without a second build matrix.
The CTest stress timeout is 180 seconds, including customized runs; very long soaks
should be executed directly under a separately managed disposable fixture.

### Named SQL sections

During local development on 2026-10-17, both new section suites and the expanded benchmark
smoke passed in two complete Windows x86 MSVC/libpq 17.6/EDB PostgreSQL 17.11 TLS runs.
Both **complete runs failed**: one timed out after 90 seconds in the existing connection
lifecycle suite (round 0, ready create); the diagnostic run timed out after 180 seconds in
the existing stress suite (load connecting). Each ran 12 of 13 suites successfully with
no skips; neither is a qualified full pass. This reproduces the historical startup/teardown
problem and remains an RC blocker. LLDB's missing Python 3.11 runtime was repaired locally;
the resulting x64 debugger capture showed only WOW64 frames, insufficient for a root cause.
Preserve these failures even if later targeted tests or CI pass; obtain an x86-aware native
stack before calling the issue resolved. The task-local logs are retained under build/logs.

After the final file-diagnostic/capture changes, the five affected CTest cases passed in
10.29 seconds without skips in a targeted run. All six Pawn examples compiled with warnings
as errors; a separate fresh PostgreSQL/open.mp 1.5.8.3079 run passed smoke/GMX, filterscript
unload, callbacks, extensions, bootstrap and the new account-section scenario. These targeted
passes do not qualify the failed complete suite. All 31 Python package/fixture/gate
regressions and the workflow/format/whitespace checks passed. Cross-platform qualification
must use the exact PR/candidate checkout, including its extracted packages.

`sql_sections_unit` exercises the real lexer with first/middle/last selection, strict names
and their 64-byte boundary, full-file duplicate/empty/malformed rejection, Unicode lookalikes,
BOM/UTF-8/NUL, LF/CRLF, quoted/escaped/dollar strings, continuation escapes, quoted identifiers,
nested comments, fake markers, parameters/casts/UNNEST/CTEs/JOIN/RETURNING, the 1,024-section
boundary, path confinement and file sizes. Every prefix truncation and 4,096 fixed-seed byte
mutations (`0x53454354`) exercise bounded parsing; syntactically invalid SQL may remain for
PostgreSQL to reject. This finite adversarial corpus is not comprehensive fuzzing.

`sql_sections_integration` uses the disposable TLS database and actual Engine/libpq code.
It checks concurrent sections on four lanes with 20 snapshots each, statement close/reuse,
worker-time reads, edits during a captured homogeneous/heterogeneous job, full-file rejection
without dispatch, SQL syntax/permission SQLSTATE, array/UNNEST/RETURNING in a transaction,
missing names/files, nonstandard strings, queued and dispatched cancellation, streaming,
owner unload/resource release and capture budgets. Existing suites retain direct/file API,
transaction/savepoint, memory and lifecycle coverage. The native inventory now covers 69
wrappers; section-specific cases check Pawn pointer/string validation and copied selectors.

The sixth real-server example, `sections.pwn`, uses `examples/sql/accounts.sql`. CI compiles
all six examples, copies the trusted SQL fixture into the disposable server, then repeats
the check using **both the AMX/include and SQL from the extracted package**. The SQL file is
mandatory in package validation. This adds no build matrix or release permission.

### Windows libpq startup deadlock investigation, October 17, 2026

At integrated `master` `ea82bb7`, [CI 38990960268](https://github.com/itskoleban/omp-postgresql/actions/runs/38990960268)
passed all six jobs without skipped steps. All five Engine jobs ran 13/13 tests, including
Linux ASan/UBSan; both extracted x86 packages passed all six server scenarios. This
preserves cross-platform evidence for the integrated SQL sections but did not explain
the historical local startup/destruction timeouts.

A focused Windows x86/MSVC/libpq 17.6 test launched the lifecycle executable in fresh
processes. Process 57 reproduced the ready-pool deadline and subsequent destruction hang.
An x86 DbgEng capture with project symbols showed all eight workers inside libpq waiting
on the same critical section. DLL relocation, its COFF symbols and disassembly mapped
that section to `ssl_config_mutex` and the waiting call to `pthread_mutex_lock`.
The export-only debugger label `PQsetSSLKeyPassHook_OpenSSL+0x99` was a nearest-symbol
label, not evidence that the component called the key-pass hook API.

The original Windows mutex initializer can overwrite initialized state 1 with 2 when a
late initializer resumes. An owner's concurrent unlock then returns EINVAL without
releasing the critical section. This matches the independently reported
[PostgreSQL defect](https://www.postgresql.org/message-id/CAAH-eSXFXSEtXpb4kUAGOKKdm3o3y-yWfjsJ7R7zUL6g-swuzw@mail.gmail.com);
the [upstream correction](https://commitfest.postgresql.org/patch/7255/) remained pending
review when checked. The real dependency C source was compiled with Windows scheduling
hooks: the unpatched version deterministically failed the holder's unlock with exit 1;
the checked-in correction passed that same schedule and 80,000 contended locks.

After rebuilding the same local libpq 17.6 client with the correction, 100 fresh lifecycle
processes passed (3,400 checks; 67.89 seconds). A full Windows x86 Release run against a
disposable TLS PostgreSQL 17.11 server passed all 15 configured tests without skips in
62.59 seconds, including the deterministic regression and 20 additional fresh processes.
All 33 Python package/fixture/qualification regressions passed. Forward application and
reverse verification also passed against the checksum-verified official PostgreSQL 17.11
source used by CI. These are local correctness checks, not throughput benchmarks.

The full local run also passed all six real open.mp 1.5.8.3079 scenarios. A Windows
package from branch commit `f43fd53` was assembled twice with identical SHA-256
`2d6c617197278a9c927fa32838427983f7ab5d738bbbf494110184b738a6bd5b`.
The extracted package passed inventory/architecture/dependency/license/hash/provenance
verification. All six packaged examples compiled again with warnings as errors; a freshly
extracted official server passed smoke/GMX, pending filterscript unload, callbacks,
extensions, bootstrap and sections using those AMX/include/SQL files and the packaged
DLLs, with development PATH entries removed. Installed DLL hashes matched the payload.
This local libpq 17.6 package is separate from CI's libpq 17.11 artifacts and is not an RC.

The Windows source helper applies a narrow downstream compare-exchange correction,
preserving the mutex struct and libpq ABI. No component scheduling or connection ownership
is changed. Packages retain the patch/rationale and bind the DLL and patch hashes to the
source version. This directly diagnoses one reproduced deadlock; it does not establish
that every historical timeout shares its cause. Final cross-platform CI/candidate package
qualification is required independently of these local checks. Other Windows
libpq providers require equivalent verification. The Astra audit and remaining RC gates
remain outstanding.

### Sanitizers and server integration

`PG_THREAD_SANITIZE=ON` enables a separate Linux 64-bit GNU/Clang build. It cannot
be combined with `PG_SANITIZE`; unsupported platforms fail at configuration time.
The existing Linux CI matrix adds a Clang 19 ThreadSanitizer configuration, including
the real native wrappers and every configured CTest case. Reports fail the job through
`TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`, without suppressions. TSan
build metadata is marked `THREAD` and package verification rejects such binaries.
This is race detection for instrumented project code; system libpq/OpenSSL are not
rebuilt with TSan and this does not qualify a 64-bit open.mp distribution. See the
[Clang limitations](https://clang.llvm.org/docs/ThreadSanitizer.html#limitations).

```sh
CC=clang-19 CXX=clang++-19 cmake -S . -B build-tsan -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DPG_THREAD_SANITIZE=ON -DPG_BUILD_BENCHMARK=ON
cmake --build build-tsan --parallel 4
TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 \
    python3 tests/run_postgres.py --build build-tsan
```

For sustained load, configure `-DPG_STRESS_SECONDS=600` (strict decimal, 0..1800;
default 0). `stress_integration` runs at least that many seconds **and** at least
`OMP_PG_STRESS_ROUNDS` load waves, on the same Engine and four sessions, before the
existing bounded lifecycle fault rounds. Every 48-job wave checks FIFO/results,
exact database counters and expiration of all weak job references after release.
Limits of 48 retained jobs and four statement/batch handles expose leaked capacity
on subsequent waves. Each wave logs elapsed time and process peak RSS/working set.
This high-water metric includes allocator/dependency caches and is not a leak bound.
The seed reproduces the submitted workload; a duration run's wave count also depends
on host scheduling and speed. SQL operation count excludes setup/integrity queries.

CMake sets the test's duration environment and timeout to `180 + PG_STRESS_SECONDS`
seconds; per-operation deadlines are unchanged. Manual Actions input `stress_seconds`
applies the same setting to all engine configurations, including both x86 builds.
`stress_rounds` still controls the separate lifecycle phase (1..100); duration extends
the load phase only. Logs/JUnit are retained on failure and skipped tests fail the
qualification gate. A finite soak is evidence for its recorded duration and seed,
not an hours-long qualification or a throughput comparison. Runtime/server errors
must be investigated rather than counted as passing repetitions.

Configure `PG_SANITIZE=ON` on a supported Linux toolchain and run as an unprivileged user:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
python3 tests/run_postgres.py --build build
```

See [Windows server testing](build-windows.md) and [Linux server testing](build-linux.md)
for the original five-example server commands; add `--section-gamemode sections.amx`
and `--section-sql examples/sql/accounts.sql` for the sixth example. `--only-openmp` uses a fresh database to rerun
bridge tests without claiming a new core-suite run. For the Windows x64 core, use
`--postgres-bin` to select independent server tools without changing client DLL resolution.

Deploy current libpq/OpenSSL maintenance releases. A recorded client version is test evidence,
not a recommendation to retain an older patch. Experimental packages redistribute only
the selected runtimes described in [DEPENDENCIES.md](../DEPENDENCIES.md).
