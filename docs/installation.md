# Installing an Actions package

Download the artifact for your operating system from a successful
[`build-and-test` run](https://github.com/itskoleban/omp-postgresql/actions/workflows/ci.yml).
It contains a platform-specific ZIP and its `.sha256` file. These are build/qualification
artifacts; an RC version label alone does not establish release approval. The workflow has read-only repository
permissions and does not publish releases or create tags.

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
  `export LD_LIBRARY_PATH=/srv/openmp/lib:${LD_LIBRARY_PATH:-}`.
- Retain `LICENSE`, `NOTICE.md`, `licenses/`, `share/omp-postgresql/licenses/` and
  `share/omp-postgresql/patches/` with the dependency correction and its rationale.

Install the remaining system dependencies described in [DEPENDENCIES.md](../DEPENDENCIES.md).
The supported test server is **open.mp 1.5.8.3079 x86**, using the dynssl distribution on
Linux. Engine compilation in x64 does not prove x64 server compatibility.

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
reloads, filterscript unload, callbacks, typed batches, savepoints, streaming and bootstrap.
No existing PostgreSQL cluster or production server is used by those tests.

Read [API guarantees](api.md), [security](security.md), [validation limits](validation.md)
and the [packaging procedure](packaging.md) before deploying. The independent Astra audit
and confirmed corrections are recorded in [the audit report](independent-audit.md).
Publication still requires exact-candidate qualification and owner approval. See
[current readiness](rc-readiness.md) for the evidence and remaining gates, and
[getting started](getting-started.md) for service configuration and your first CRUD example.
