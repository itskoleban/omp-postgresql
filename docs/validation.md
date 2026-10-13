# Validation results and known limitations

The component remains **0.1 experimental**. Passing the current suite is evidence for the
listed scenarios, not complete production qualification. Engine tests and real open.mp
loading are recorded separately.

## Automated coverage

Checks remain active in Release builds. `tests/run_postgres.py` creates an isolated,
loopback-only PostgreSQL cluster with TLS and always stops it. Integration tests return
77 when no service profile is supplied; a skipped test is not a successful integration run.

| Suite | Coverage |
| ----- | -------- |
| `unit` | Parameter validation, immutable snapshots, limits, SQL/file confinement and UTF-8. |
| `integration` | Pools, FIFO, 256 ordered executions, NULL, UNNEST, partial failure, prepared-plan invalidation, transaction finishing capacity/expiry, cancellation, backend loss, reconnection and shutdown. |
| `types_unit` | Sixteen scalar/array OID pairs, BIGINT boundaries, nullable array escaping, all 256 bytes, empty BYTEA and invalid encodings. |
| `types_integration` | Exact numeric/temporal values, native boolean output versus text casts, scalar/array OIDs, nullable/empty arrays in prepared, direct and heterogeneous execution, BYTEA and server format errors. |
| `extensions_integration` | Heterogeneous snapshots/rejection/partial outcomes, lane metrics, BEGIN options/defaults, savepoint recovery, 10000-row streaming, zero-column tuples, provisional rows, slow consumers, delivery-only timeout, cancellation and owner unload. |
| Real open.mp | Native loading, three GMX reloads, unloading a filterscript with pending SQL, callback state restoration, typed batches, savepoint recovery, streaming and gated world initialization. |

The TLS runner checks verify-full, incorrect host identity/password, and client certificates.
A real immediate PostgreSQL restart is exercised. A proxy drops COMMIT CommandComplete:
the client reports UNCERTAIN and a subsequent connection verifies exactly one committed row.

## Results recorded on October 13, 2026

`master` was verified at `732d251`; work continues on `codex/stabilize-types-validation`,
[PR #1](https://github.com/itskoleban/omp-postgresql/pull/1).

| Platform | Current evidence |
| -------- | ---------------- |
| Windows x64 | Local GCC/MinGW 16.1, libpq 18.4, EDB PostgreSQL 17.11: all five CTest suites passed. Counts: 26 core unit, 146 core integration, 97 type unit, 532 type integration, 606 extension checks. |
| Windows x86 | Local MSVC 19.51, TLS-enabled libpq 17.6, EDB PostgreSQL 17.11: all five suites passed with the same counts. Real open.mp 1.5.8.3079 loaded the component and passed smoke/GMX, unload, callback, extension and bootstrap scenarios. |
| Linux x64 | [CI run 38476331764](https://github.com/itskoleban/omp-postgresql/actions/runs/38476331764), commit `c4adc11`: both normal and ASan/UBSan jobs succeeded with all five suites. Later Pawn/example and harness fixes require the current branch's CI results. |
| Linux x86 | The same CI run passed all five core suites, then failed the server unload harness. Corrected command ordering is awaiting current-branch CI confirmation. |

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

## Remaining release qualification

- x64 builds qualify the engine and compilation. The tested distributed open.mp server is
  x86; component, client libraries and server architectures/ABIs must match.
- Confirm the current branch's complete CI matrix, including the Linux unload correction
  and warning-free compilation, before claiming all declared platforms are qualified.
- Prolonged network partitions, interruption at all BEGIN/ROLLBACK stages, combined
  cancellation/transaction expiry/shutdown, exhausted handles and extreme backpressure
  need more adversarial coverage.
- AMX input fuzzing, sustained stress, ThreadSanitizer and measured peak memory remain
  pending. Bounded retained objects do not establish a strict RSS limit; libpq can allocate
  a large row before the component checks it. DNS/authentication/file providers can delay
  shutdown independently of socket deadlines.
- COPY transfer and receiving LISTEN/NOTIFY are excluded from the 0.1 candidate scope.
  Their dedicated protocol states, bounded queues and lifecycle tests are not implemented.
  Ordinary queries reject COPY/LISTEN/UNLISTEN. Parameterized `SELECT pg_notify($1,$2)` can
  send notifications; notifications are not persistent and cannot be recovered after a
  receiving session disconnects. See the [protocol scope decision](extension-audit.md).
- Pipeline, automatic lane selection and named-plan caches remain optional optimizations.
  The sequential FIFO engine does not depend on them. Existing benchmarks measure direct
  libpq transport, not comparative engine/Pawn batch latency or a statistical distribution.
- The complete lifecycle is demonstrated by the smoke, extension and bootstrap examples
  together; a single combined example and a reproducible release-candidate package still
  require qualification. No release was published or merged automatically.

## Reproducing validation

Configure `PG_SANITIZE=ON` on a supported Linux toolchain and run as an unprivileged user:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
python3 tests/run_postgres.py --build build
```

See [Windows server testing](build-windows.md) and [Linux server testing](build-linux.md)
for the full five-example server commands. `--only-openmp` uses a fresh database to rerun
bridge tests without claiming a new core-suite run. For the Windows x64 core, use
`--postgres-bin` to select independent server tools without changing client DLL resolution.

Deploy current libpq/OpenSSL maintenance releases. A recorded client version is test evidence,
not a recommendation to retain an older patch. Dependencies are not automatically distributed.
