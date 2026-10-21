# Installing omp-postgresql

## Published testing Release Candidate

For a known, versioned distribution, start with the published
[v0.1.0-rc.1 Pre-release](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.1).
Choose the Windows x86 or Linux x86 distribution ZIP and its matching `.zip.sha256` file.
Its full source identity is `db2bfb9b816b0042b8ef36e847cf9fb1c24e94fc`; later repository
documentation does not change that tag or its assets. The source ZIP contains build inputs,
not a prebuilt component. This is a testing release, not production certification.

## Development CI artifacts

Use artifacts from a successful [`build-and-test` run](https://github.com/itskoleban/omp-postgresql/actions/workflows/ci.yml)
only when deliberately testing development changes. Check the run's full source commit,
event and branch; a PR preview is distinct from integrated `master`. The RC version string
can remain the same across later development commits, so it is not enough to identify a
build. Each platform artifact contains a ZIP and checksum sidecar. The build workflow has
read-only repository permissions and creates neither tags nor Releases.

## Verify and install a distribution

Verify SHA-256 before extraction (`sha256sum --check NAME.zip.sha256` on Linux, or
`Get-FileHash NAME.zip -Algorithm SHA256` on Windows). Extract into a temporary directory.
Check `manifest.json`: version, full source commit, Release configuration, platform,
dependency imports and per-file hashes. Copy the contents of its `omp-postgresql/`
directory into a disposable server installation for your first test.

- `components/omp-postgresql.dll` or `.so` belongs in the server's `components/` directory.
- `qawno/include/omp-postgresql.inc` belongs in the compiler's include directory.
- Windows runtime DLLs at the package root belong beside `omp-server.exe`.
- Linux `lib/libpq.so.5` belongs in an operator-controlled directory. Export its absolute
  path in `LD_LIBRARY_PATH` before starting the server, for example
  `export LD_LIBRARY_PATH="/srv/openmp/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"`.
- Retain `LICENSE`, `NOTICE.md`, `licenses/`, `share/omp-postgresql/licenses/` and
  `share/omp-postgresql/patches/` with the dependency correction and its rationale.

Install the remaining system dependencies described in [DEPENDENCIES.md](../DEPENDENCIES.md).
The supported test server is **open.mp 1.5.8.3079 x86**, using the dynssl distribution on
Linux. Engine compilation in x64 does not prove x64 server compatibility.
Keep the component and public include from the same distribution. Hashes detect altered
bytes; the unsigned inventory is not an authenticated build attestation. Avoid an empty
`LD_LIBRARY_PATH` entry, which makes the [loader](https://man7.org/linux/man-pages/man8/ld.so.8.html)
search the current directory.

Configure `PGSERVICEFILE`, `PGPASSFILE` and trusted certificates outside the package.
The supplied [profile](../examples/pg_service.conf) is a credential-free example; replace
its connection details. Use `sslmode=verify-full` for remote services and Unix permissions
0600 for `.pgpass`. Keep passwords out of Pawn and version control.

Compile [smoke.pwn](../examples/smoke.pwn) with the **extracted** include, official open.mp
stdlib and Pawn 3.10.11 (32-bit cells, warnings as errors):

```sh
pawncc examples/smoke.pwn -E -iqawno/include -i/path/to/omp-stdlib -obuild/smoke.amx
```

Run on an isolated database/server and wait for `PG SMOKE PASS`; failures are not installation
success. CI repeats this check from the ZIP in a fresh official server, then exercises
reloads, filterscript unload, callbacks, typed batches, savepoints, streaming, bootstrap,
SQL sections and logging. All thirteen public examples compile; eleven run unattended.
Interactive account/custom gameplay behavior needs separate player/provider validation.
No existing PostgreSQL cluster or production server is used by those tests.

Read [API guarantees](api.md), [security](security.md), [validation limits](validation.md)
and the [packaging procedure](packaging.md) before deploying. The independent Astra audit
and confirmed corrections are recorded in [the audit report](independent-audit.md).
RC.1 publication is complete; future releases require separate qualification and owner
authorization. See [current readiness](rc-readiness.md) for evidence and remaining limits, and
[getting started](getting-started.md) for service configuration and your first CRUD example.

## Source builds

Use the [Windows](build-windows.md) or [Linux](build-linux.md) guide when building from a
checkout or source archive. Match the server architecture, C++ ABI and libpq dependencies;
installing a compiler does not qualify an untested platform. Git source archives omit
internal `audit/` records while retaining build scripts and tests. Repository copies of
these records remain linked from [validation](validation.md).

## Optional diagnostic file output

Default remains WARN to the open.mp console; configure startup `OMP_PG_LOG_LEVEL` before
launch. A later valid `pg_log_config` can select console/default file/custom file/both/OFF.
The server working directory must allow the writer to create `logs` (or pre-create it with
restricted ACLs). Use only `[./]logs/<basename>.log`; never share these files with another
writer or deploy private data as `.log.1`. The archive intentionally excludes runtime logs.
See [operator details](debugging.md) and the [compile-checked example](../examples/logging.pwn).
