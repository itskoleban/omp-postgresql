# omp-postgresql

A C++20 component for open.mp with libpq and an asynchronous, parameterized Pawn API.
Version **0.1.0 experimental**. Review the
[validated platforms and known limitations](docs/validation.md) before deploying it.

## Implemented features

- Connections through libpq service profiles, a pool of FIFO lanes, and a worker that owns each session.
- Deferred session preparation, typed parameters, immutable snapshots, and bounded batches.
- Nonblocking extended query protocol, tick-driven callbacks, and ownership by VM generation.
- Explicit results per job and statement, NULL, RETURNING, SQLSTATE, duration, and affected rows.
- Transactions that reserve a lane, commit/rollback, and rollback after a required operation fails.
- Cancellation, timeouts, and progressive reconnection without replaying uncertain writes.
- Worker-based SQL file loading with path confinement and UTF-8 validation.
- Simple queries, heterogeneous batches, per-operation outcomes, and column OID metadata.
- Exact numeric/temporal text transport, BYTEA cells, and typed arrays with NULL elements.
- Explicit isolation/read-only options, savepoints, and asynchronous savepoint recovery.
- Bounded streaming blocks, slow-consumer backpressure, and session/job statistics.

There is no ORM, query builder, password hashing, application cache, or synchronous Pawn query API.
Pipeline mode is evaluated by a standalone benchmark; the initial execution engine does not use it.

## Building

Requirements: CMake 3.24+, a C++20 compiler, Git, and libpq **17 or later** with TLS and thread support.
FetchContent obtains the open.mp SDK and Pawn headers at pinned commits.
Boost and pawn-natives are not required. Build **all** dependencies for the target server's
architecture and ABI. An x64 DLL cannot load into an x86 server.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPostgreSQL_ROOT=/path/to/postgresql
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/server
```

On Linux, install the system libpq headers and library. An x86 server requires a multilib
toolchain and 32-bit libpq. On Windows, use MSVC or MinGW with compatible libpq architecture
and ABI. `PostgreSQL_ROOT` contains include/lib directories; validate the import library format
before combining MSVC and MinGW libraries. `PG_BUILD_COMPONENT=OFF` builds only the engine
and tests. `PG_SANITIZE=ON` enables ASan/UBSan on supported toolchains.

On Windows, distribute `libpq.dll` and its dependencies, including TLS and runtime libraries,
from the same provider and architecture. On Linux, install the corresponding runtime package.
Experimental x86 Actions packages include selected runtime dependencies and their licenses;
see [DEPENDENCIES.md](DEPENDENCIES.md) for the remaining system dependencies.
The SDK and Pawn retain their licenses; neither reference plugin is linked or copied.
See the [Windows x86 build and runtime guide](docs/build-windows.md) and the
[Linux x86 build, TLS, and server guide](docs/build-linux.md).
Installation includes license notices in `share/omp-postgresql`. `PG_PAWN_INCLUDE_DIR`
can override the default include destination, `qawno/include`.
The CI workflow generates Windows/Linux x86 ZIPs and SHA-256 only after testing a fresh
server with the extracted package. See [package installation](docs/installation.md) and
[reproducible archive assembly](docs/packaging.md). These are experimental artifacts;
no Release Candidate has been approved or published.

## Installation and connections

Copy `omp-postgresql.dll` or `.so` into `components/` and install the Pawn include in your
compiler's include directory. Keep the open.mp Pawn component enabled. Do not register this
component as a legacy SA-MP plugin.

Set `PGSERVICEFILE` in the server process environment; see the [example profile](examples/pg_service.conf).
Use `PGPASSFILE`/`.pgpass` with Unix permissions 0600, certificate authentication, or another
authentication mechanism managed by libpq. Keep passwords out of the gamemode.

The following fragment belongs inside `OnGameModeInit`. See
[smoke.pwn](examples/smoke.pwn) for a complete gamemode and callback implementation.

```pawn
new database = pg_connect("game", 2);
// Wait for OnPgConnection(database, PG_CONNECTED) before submitting queries.
```

`PG_CONNECTED` means every lane is ready to accept jobs. `pg_connection_ready` reports how
many physical sessions are connected. This does not guarantee that future operations will
succeed. The default lane is 0; selecting another lane is an explicit concurrency decision.

For remote connections, use `sslmode=verify-full`, a trusted CA, and a host name matching the
certificate. `require` encrypts the connection but does not provide the same identity guarantee.
Configure client certificates with `sslcert`/`sslkey` and suitable PostgreSQL authentication rules.
See [libpq SSL](https://www.postgresql.org/docs/17/libpq-ssl.html) and
[service profiles](https://www.postgresql.org/docs/17/libpq-pgservice.html).

## Runnable example

[examples/smoke.pwn](examples/smoke.pwn) creates a temporary table within a transaction,
inserts 20 rows through `pg_add`, updates them with arrays/UNNEST, and commits.
It does not modify existing tables. Compile with the official open.mp Pawn 3.10.11 compiler,
32-bit cells, and official includes:

```sh
pawncc examples/smoke.pwn -ipawn -i/path/to/omp-stdlib -obuild/smoke.amx
```

Install the AMX as the gamemode of a test instance, configure the `game` profile, start the
server, and look for `PG SMOKE PASS`. Asynchronous callbacks must return without `sleep`;
callback continuations are unsupported.

[extensions.pwn](examples/extensions.pwn) exercises heterogeneous typed batches, binary
values, savepoint recovery, and a bounded streaming consumer. [bootstrap.pwn](examples/bootstrap.pwn)
loads properties, vehicles, and clans on the session owning its temporary fixtures, validates
the results, and keeps players gated until initialization succeeds. The callback and unload
gamemodes cover VM lifecycle regressions. All five examples are compiled with warnings
treated as errors in the Linux and Windows x86 server CI jobs.

See the [complete API](docs/api.md), [architecture and reference analysis](docs/design.md),
[security and concurrency guide](docs/security.md), and [tests and limitations](docs/validation.md).
Original project code is licensed under [MIT](LICENSE); dependencies retain their notices.

## Integration tests and performance

```sh
# Put initdb, pg_ctl, psql, openssl, and ctest on PATH; use a non-root Unix account.
python tests/run_postgres.py --build build
```

The runner creates a temporary loopback-only instance, test certificate, and TLS service
profiles. It runs CTest and shuts down the instance even if a test fails. It does not open an
existing database. Its temporary directory remains available for inspection.
CTest skips integration with exit code 77 when `OMP_PG_TEST_SERVICE` is unset;
a skipped test is not a successful integration run.

The default run also exercises 4,640 seeded SQL operations across four lanes and ten
combined lifecycle stress rounds with blocked streams, saturated job limits and
transaction rollback verification. See [stress reproduction and memory limits](docs/validation.md#combined-engine-stress).

```sh
cmake -S . -B build -DPG_BUILD_BENCHMARK=ON
cmake --build build --parallel
# Run both benchmarks in a fresh disposable TLS database, after CTest.
python tests/run_postgres.py --build build --benchmark --benchmark-repetitions 7
```

The transport probe compares direct libpq prepared queries, pipeline with Sync and UNNEST.
The Engine benchmark compares individual jobs, 20 snapshots, grouped UNNEST, heterogeneous
batches, four explicit lanes, buffered reads and streaming. It verifies all returned values
and final database contents, then reports repeated job/queue/worker p50/p95/p99, CPU and
process peak memory. The existing CI matrix runs a small correctness smoke with no timing
thresholds. Neither probe measures Pawn or open.mp ticks. See [measurement definitions and
reproduction](docs/performance.md); local samples are not performance guarantees.

## Maintenance conventions

Keep code comments, diagnostics, examples, and documentation in natural technical English.
Follow the checked-in `.editorconfig` and `.clang-format`: tabs display as four columns in
C++ and Pawn, while Python, YAML, Markdown, and CMake use four-space indentation.
Keep function boundaries and control-flow blocks easy to scan. Format C++ changes with
clang-format and run the relevant unit, integration, and server checks before publishing.
Pawn formatting is maintained manually and checked with the compiler. Black and cmakelang
read the checked-in Python and CMake settings:

```sh
python -m black --check tests
python -m cmakelang.format --check CMakeLists.txt
```
