# Reproducible stability audit

The audit branch extends existing qualification; it does not certify a stable release.
Base source: `3cbf99849de57de09e4fa8317b9bb40f8e2d2f8f`.
Do not change RC.1 assets/tags, production services, or remote branches to run these tests.

## Fast pull request CI

Existing `ci.yml` remains in use. CTest now includes `audit_semantic_fuzz`, linked to
the same `pg_core` as other unit tests and inheriting its sanitizer flags. Its default
4,096 generated cases use seed 1096107081. The independent expectations cover exact
section slices, hidden declarations, duplicate/stacked statement rejection, Unicode
scalar encoding/invalid UTF-8, BYTEA round trips and rejected snapshot preservation.
On failure it writes `audit-fuzz-failure.bin`, the seed and iteration. Replay with
the same `OMP_PG_FUZZ_SEED` and at least iteration+1 `OMP_PG_FUZZ_CASES`.

```sh
python -m unittest discover -s tests -p 'test_*.py' -v
ctest --test-dir build -R '^(unit|types_unit|sql_sections_unit|natives_unit|audit_semantic_fuzz)$' --output-on-failure
```

The protocol proxy unit test uses actual loopback sockets and byte-fragmented frames;
it verifies that only the selected COMMIT/ROLLBACK confirmation is dropped.
These helpers/native argument checks do not fuzz or certify the AMX loader/interpreter.
Multidimensional Pawn memory must follow the existing native flat-slot contract.

## Release validation

Use existing CI for Linux x86, Windows x86 MSVC, Linux x64 OFF/ASan-UBSan/TSan and
Windows x64 Engine. Existing server/package jobs remain the architecture qualification.
The new `audit_faults_integration` requires `run_postgres.py`: it drops a ROLLBACK
confirmation and terminates our own backend while a deferred trigger blocks COMMIT on
an advisory lock. It checks actual database rows, terminal/item status and recovery.
Cancellation runs 32 rounds after an observer sees the exact command/PID active in
PostgreSQL's PgSleep wait; it requires uncertain status/item metadata and cancellation cause.
`socket_wait_unit` independently forces the critical wait-entry and select-return phases,
including transport/cancel/deadline competition, using a controlled clock without sleeps.
It compiles the actual Lane body with test-only syscall/libpq seams and checks dispatch,
terminal metadata, no replay, reconnection and a single completion. See
[A1/A2 resolution](a1-a2-resolution.md) for negative historical-runtime tests and evidence.
The existing COMMIT confirmation-loss, abrupt cluster restart, startup/TLS stalls,
authentication/TLS failures, resource limits, native boundaries and logger faults remain.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPG_BUILD_BENCHMARK=ON -DPG_STRESS_SECONDS=120
cmake --build build --parallel 4
python tests/run_postgres.py --build build --config Debug --doc-sql
```

On Windows provide `--postgres-bin <official-server-bin>`; configure the qualified,
patched x86 libpq described in `docs/build-windows.md`. Prepend only that runtime and
the matching OpenSSL/winpthread directories to PATH. x64 Engine results do not qualify
an x64 component for open.mp. Use the original official open.mp 1.5.8.3079 archive
and hash from `ci.yml`, compile examples with the pinned Pawn compiler/stdlib, and run
both source and fresh extracted-package scenarios using the existing CI commands.

## Dedicated 12–24 hour runs

No hosted resources are started automatically. `audit-long.yml` is manual-only and
requires an already provisioned, disposable Linux x64 runner labelled
`pg-audit-isolated`, with PostgreSQL >=17 development/server tools, OpenSSL, Git,
CMake >=3.24, Ninja, C++20 and Python. Run as a non-root user. The runner must not
contain production credentials, data or services. ASan/UBSan are enabled there;
run TSan separately through the existing configuration. Dependencies are not fully
instrumented. A workflow file is preparation, not evidence of execution.

```sh
cmake -S . -B build-long -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPG_SANITIZE=ON -DPG_BUILD_BENCHMARK=ON -DPG_STRESS_SECONDS=86400
cmake --build build-long --parallel 4
python scripts/audit_long.py --build build-long --config Debug --seconds 86400 --seed 1592594996 --output build-audit-evidence
```

The external runner rejects a build configured with a shorter load duration, records
SHA/dirty state, commands, exit codes and elapsed times, and rejects any failed or
skipped configured CTest case through the existing qualification gate. It creates
only new loopback TLS clusters. The fixture always attempts shutdown; the supervisor
also attempts scoped cleanup if its process tree times out. A dedicated machine must
still be destroyed/reset after cancellation or a host crash. Preserve logs before
teardown. No Work session needs to remain open. No workflow is dispatched by this audit.

The stress process retains the same primary Engine for the requested duration. Each
48-job wave checks FIFO, result order, exact shared/per-lane database counters, capacity
reuse and weak-reference expiration. Every 32 waves it also runs read/write transactions,
ordered streaming, cancellation/reconnection and additional connection creation/close.
Periodic deterministic idle intervals alternate bursts and recovery. Existing lifecycle
rounds then check backpressure, transaction expiry, queued writes, unload and shutdown,
with callback uniqueness/completeness assertions. The sustained main waves do not
measure real Pawn callback latency; existing real-server scenarios measure that separately.

`audit_metric` records cumulative CPU microseconds, current RSS KiB, cumulative load SQL
operations, quiescent pending/error counts and per-wave job-delivery p50/p95/p99 in us.
Compute load SQL throughput from the counter delta divided by elapsed wall time.
Counts exclude setup/verification SQL and most periodic helper SQL; they are not total
wire commands. A wave admits 48 jobs, but pending=0 is a verified quiescent sample,
not continuous queue sampling. Allocator byte accounting is unavailable. RSS includes
the process, dependency allocators and caches; bounded handles and observed RSS do not
prove absence of all leaks. The first sample follows a wave, not process initialization.

Physical/network filesystem stalls, hostile OS link races, systematic allocator/FD
exhaustion, real-player slot reuse and AMX loader fuzzing remain separate qualification
work. Do not claim these pass from the new semantic or Engine tests.
