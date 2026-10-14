# Validation results and known limitations

The component remains **0.1 experimental**. Passing the current suite is evidence for the
listed scenarios, not complete production qualification. Engine tests and real open.mp
loading are recorded separately.

## Automated coverage

Checks remain active in Release builds. `tests/run_postgres.py` creates an isolated,
loopback-only PostgreSQL cluster with TLS and always stops it. Integration tests return
77 when no service profile is supplied; a skipped test is not a successful integration run.

| Suite                    | Coverage                                                                                                                                                                                                                                       |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `unit`                   | Parameter validation, immutable snapshots, limits, SQL/file confinement and UTF-8.                                                                                                                                                             |
| `integration`            | Pools, FIFO, 256 ordered executions, NULL, UNNEST, partial failure, prepared-plan invalidation, transaction finishing capacity/expiry, cancellation, backend loss, reconnection and shutdown.                                                  |
| `types_unit`             | Sixteen scalar/array OID pairs, BIGINT boundaries, nullable array escaping, all 256 bytes, empty BYTEA and invalid encodings.                                                                                                                  |
| `types_integration`      | Exact numeric/temporal values, native boolean output versus text casts, scalar/array OIDs, nullable/empty arrays in prepared, direct and heterogeneous execution, BYTEA and server format errors.                                              |
| `extensions_integration` | Heterogeneous snapshots/rejection/partial outcomes, lane metrics, BEGIN options/defaults, savepoint recovery, 10000-row streaming, zero-column tuples, provisional rows, slow consumers, delivery-only timeout, cancellation and owner unload. |
| Real open.mp             | Native loading, three GMX reloads, unloading a filterscript with pending SQL, callback state restoration, typed batches, savepoint recovery, streaming and gated world initialization.                                                         |

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

## Remaining release qualification

- x64 builds qualify the engine and compilation. The tested distributed open.mp server is
  x86; component, client libraries and server architectures/ABIs must match.
- Validation applies to these tested compiler/client/server combinations. Requalify changes
  to execution code, the bridge, dependencies or the workflow; other distributions are not
  covered by this successful matrix.
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
  together; the value of a single combined example still needs review. Experimental x86
  distribution is now validated; final RC artifacts must be rebuilt and qualified at the
  approved release commit after stress testing and an actual independent Astra audit.
  No release was published or merged automatically.

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
not a recommendation to retain an older patch. Experimental packages redistribute only
the selected runtimes described in [DEPENDENCIES.md](../DEPENDENCIES.md).
