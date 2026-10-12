# Validation results and known limitations

## Automated coverage

The regressions include 256 ordered executions, preserved batches after enqueue rejection,
row/byte limits, transaction expiry, transaction finishing capacity under a full queue,
client certificate authentication, and an immediate PostgreSQL restart. The latest suite also
covers prepared-plan cache invalidation after a failed Parse or a changed result shape.

`tests/tests.cpp` uses checks that remain active in Release builds rather than relying on
assert. `tests/run_postgres.py` starts an isolated TLS-enabled database, runs the tests,
and shuts it down. Windows/Linux CI is configured; configuration alone does not prove that
GitHub Actions has executed it.

| Area                  | Automated coverage                                                                                         |
| --------------------- | ---------------------------------------------------------------------------------------------------------- |
| Connections/pools     | Two sessions, cross-owner rejection, and invalid profiles.                                                 |
| TLS/authentication    | Successful verify-full, wrong host name, and wrong password in the TLS runner.                             |
| Parameters            | int32, extreme/overflow BIGINT, float, bool, UTF-8, quotes, backslashes, NULL, UUID, JSONB, and BYTEA.     |
| Arrays                | Text escaping and UNNEST updating two rows in one execution.                                               |
| Statements            | Reuse, snapshots, stable types, missing bindings, closing in flight, and prepared-cache invalidation.      |
| Batches/results       | Twenty executions, empty batches, RETURNING, zero-row results with columns, partial failure, and SQLSTATE. |
| Transactions          | Commit, rollback, lane reservation, and required failure preventing COMMIT.                                |
| Concurrency           | Two lanes and FIFO updates to the same record.                                                             |
| Cancellation/recovery | Before dispatch and during pg_sleep, backend termination, timeout, and queries after reconnection.         |
| Lost confirmation     | A proxy drops COMMIT CommandComplete; the result is UNCERTAIN and exactly one committed row is verified.   |
| SQL files             | Reading, missing files, size limits, and rejection of paths outside the root.                              |
| Shutdown              | Owner unloading and resource collection.                                                                   |
| Memory                | Local Linux ASan/UBSan and leak detection without diagnostics.                                             |

## Results recorded on October 12, 2026

The latest cache-regression suite is confirmed on Windows x64. The other platform rows record
the earlier verified baseline separately; they do not claim that the latest suite has been
rerun on those platforms.

| Platform                   | Compiler / libpq                          | Unit checks | Integration checks | Recorded suite                                   |
| -------------------------- | ----------------------------------------- | ----------: | -----------------: | ------------------------------------------------ |
| Windows x64                | GCC/MinGW 16.1 / libpq 18.4               |          26 |                146 | Latest, including cache regressions.             |
| Windows x86                | MSVC 19.51 / TLS-enabled libpq 17.6       |          26 |                135 | Earlier baseline.                                |
| Linux x64, Debian Bookworm | GCC 12.2, CMake 3.25.1 / libpq 18.6       |          26 |                135 | Earlier baseline, ASan/UBSan and leak detection. |
| Linux x86                  | GCC 12.2 / libpq 17.11 and OpenSSL 3 i386 |          26 |                135 | Earlier baseline.                                |

These runs use PostgreSQL server 17.11. Linux sanitizer and leak-detection runs produced no
diagnostics. The file-read-error detection change was rebuilt and its unit/file checks rerun.

The Windows x86 component loaded in **open.mp 1.5.8.3079**, commit
c6759bd8d265171ae3d86598895a23d5a8d92a3b. The SQL flow completed four times, including three GMX
reloads. A filterscript was unloaded with a pending query; no later callback occurred,
and the server exited with code 0. A separate gamemode verified suspended main(), callbacks
using sleep and divide-by-zero, closing other completed jobs, and a valid handle during
PG_DISCONNECTED: `PG CALLBACK PASS`.

Linux x86 also passed real loading and the same GMX, unloading, and callback tests in
open.mp 1.5.8.3079 dynssl, using libpq 17.11/OpenSSL 3 i386 and isolated PostgreSQL.
The test processes ran without elevated privileges and exited with code 0. The distribution
ran on a native Linux filesystem; starting directly from an NTFS bind mount exposed a server
filesystem metadata problem.

smoke.pwn, unload.pwn, and callbacks.pwn compile without warnings with Pawn 3.10.11, 32-bit
cells, and official includes. Sleep and divide-by-zero in callbacks.pwn are deliberate;
the test requires subsequent recovery. Server, component, C++ runtime, and libpq architectures
must match. These results do not make an x64 binary installable in an x86 server.

## Remaining release qualification

Before a production release, account for the following limits and remaining checks:

- x64 builds are for the engine and standalone tests. The distributed open.mp servers tested
  here are x86. Do not mix binaries or libraries across architectures. Validation applies
  to the listed platform/version combinations, not every operating-system distribution.
- Prolonged network partitions and interruptions at every point of BEGIN/ROLLBACK need
  further testing. Actual PostgreSQL restart and client certificate authentication are covered.
- AMX input fuzzing, prolonged stress, ThreadSanitizer, and peak-memory measurement remain pending.
- Backpressure extremes, exhausted IDs, and cancellation/transaction expiry concurrent with
  shutdown need exhaustive testing. Coverage of every possible interleaving is not claimed.
- Pipeline integration remains pending. The benchmark evaluates transport, while the engine
  uses sequential prepared execution. Integration requires correlation, Sync boundaries,
  backpressure, and failure tests before enabling it.
- COPY, savepoints, arrays with individual NULL elements, and public streaming are outside
  the initial API.
- There is no strict RSS guarantee or independent timeout guarantee for DNS/external providers.

These limitations are explicit. There are no placeholder natives for pending capabilities.
API 0.1 may evolve before stable compatibility is established.

## Reproducing validation

For Linux, configure `PG_SANITIZE=ON` and run the test harness as an unprivileged account with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
See [Windows server testing](build-windows.md) and [Linux server testing](build-linux.md).
`--only-openmp` verifies Pawn bridge changes with another temporary database.
The configured CI has not been executed on GitHub during this task.

Deploy current libpq and OpenSSL maintenance releases. The listed x86 client version records
local test evidence; it is not a recommendation to retain an old patch, and dependencies are
not automatically redistributed.
