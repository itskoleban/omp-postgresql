# omp-postgresql

[Introduction](#introduction) · [Installation](#installation) · [Quick Start](#quick-start) ·
[Documentation](#documentation) · [Building and Testing](#building-and-testing) · [License](#license)

## Introduction

omp-postgresql is a C++20 component that brings PostgreSQL to open.mp through libpq and
an asynchronous, parameterized Pawn API. Database work runs on background workers;
callbacks deliver connection changes and results on server ticks.

- Service-profile connections, FIFO lanes, timeouts, cancellation and progressive reconnection.
- Typed parameters, immutable snapshots, bounded batches and SQL files with named sections.
- Transactions, savepoints, explicit per-operation results and bounded streaming with backpressure.
- NULL, RETURNING, SQLSTATE, column metadata, exact numeric/temporal text, BYTEA and typed arrays.
- VM-generation ownership, custom completion callbacks and configurable bounded diagnostics.

There is no ORM, query builder, password hashing, application cache or synchronous Pawn query
API. Uncertain writes are never replayed automatically. Pipeline mode is benchmarked separately;
the execution engine does not use it. See the [execution guide](docs/api.md) for API guarantees.

**Development status:** stable **v0.1.0 remains blocked**. Source preparation and version metadata
do not establish stable readiness or authorize publication. Use development artifacts only for
testing and evaluation; review the [release gates](#release-status-and-publication) and
[validated platforms and known limitations](docs/validation.md).

## Installation

1. Choose a matching package from the [releases page](https://github.com/itskoleban/omp-postgresql/releases)
   or a development CI artifact, and verify its exact source identity and SHA-256.
2. Copy `PostgreSQL.dll` or `PostgreSQL.so` into `components/`, and `omp_postgresql.inc` into
   your Pawn compiler's include directory. Keep the include and binary from the same distribution.
3. Install the [runtime dependencies](DEPENDENCIES.md) and configure the service profile below.
   Keep the open.mp Pawn component enabled; do not register this as a legacy SA-MP plugin.

Follow the [installation guide](docs/installation.md) for package verification, runtime placement
and a first server test. Published RC packages retain their original filenames; the names above
apply to the current source. Validated server distributions target **open.mp 1.5.8.3079 x86**
on Windows and Linux (dynssl). Match every dependency's architecture and ABI: an x64 DLL cannot
load into an x86 server, and x64 engine tests do not establish x64 server compatibility.

When migrating from RC.1/RC.2, replace `#include <omp-postgresql>` with
`#include <omp_postgresql>` and recompile. Stop the server before replacing the old binary;
keep only one copy of the component. Public Pawn signatures, UID and the internal
`omp-postgresql` identity remain unchanged. See the
[migration instructions](docs/installation.md#migrating-from-previous-release-candidates).

Keep passwords out of Pawn and version control. Use `PGPASSFILE`/`.pgpass` with Unix permissions
0600 (restricted service-account ACLs on Windows), certificate authentication or another
libpq-managed mechanism. For remote services, use `sslmode=verify-full`, a trusted CA and a
hostname matching the certificate; `require` encrypts without the same identity guarantee.
Client certificates use `sslcert`/`sslkey` and suitable PostgreSQL authentication rules.
See [secure configuration](docs/security.md), [libpq SSL](https://www.postgresql.org/docs/17/libpq-ssl.html)
and [service profiles](https://www.postgresql.org/docs/17/libpq-pgservice.html).

## Quick Start

`pg_connect("game")` uses the PostgreSQL service profile named `game`. Configure its connection
details and `PGSERVICEFILE` **before starting the server**, following
[Install and configure](docs/getting-started.md#install-and-configure) and the
[example profile](examples/pg_service.conf). The request is asynchronous: a valid handle means
local acceptance; wait for `OnPgConnection` to report `PG_CONNECTED` before using the connection.

<!-- pawn-example: intro.connection -->

```pawn
#include <open.mp>
#include <omp_postgresql>

new g_Database = PG_INVALID;

public OnGameModeInit() {
	g_Database = pg_connect("game");

	if (g_Database == PG_INVALID) {
		new error[256];

		pg_last_error(error);
		printf("[PostgreSQL] Connection request rejected: %s", error);
	}

	return 1;
}

public OnPgConnection(connection, connection_state) {
	if (connection != g_Database) return 1;

	switch (connection_state) {
		case PG_CONNECTED: {
			print("[PostgreSQL] Connected successfully.");
		}

		case PG_CONNECTION_FAILED: {
			new error[256];

			if (pg_connection_message(connection, 0, error) && error[0]) {
				printf("[PostgreSQL] Connection failed: %s", error);
			} else {
				print("[PostgreSQL] Connection failed.");
			}

			pg_connection_close(connection);
			g_Database = PG_INVALID;
		}

		case PG_DISCONNECTED: {
			g_Database = PG_INVALID;
		}
	}

	return 1;
}

public OnGameModeExit() {
	if (g_Database != PG_INVALID) {
		pg_connection_close(g_Database);
		g_Database = PG_INVALID;
	}

	return 1;
}
```

The default is one lane (lane 0). `PG_CONNECTED` means every configured lane is ready, not that
future operations are guaranteed to succeed; `pg_connection_ready` reports connected physical
sessions. Additional lanes are an explicit concurrency decision described in the
[execution guide](docs/api.md). Callbacks must return promptly without `sleep`; callback
continuations are unsupported. Connection diagnostics can contain sensitive information:
keep test output private and redact or restrict production logs as explained in
[security](docs/security.md) and [error inspection](docs/debugging.md#inspect-local-and-job-errors).

## Documentation

| Topic | Guides |
| --- | --- |
| Start here | [Installation](docs/installation.md), [Getting started](docs/getting-started.md), [Examples and learning path](docs/examples.md) |
| Choose and use the API | [Quick reference](docs/quick-reference.md), [Execution guide](docs/api.md), [Native reference](docs/native-reference.md), [Callbacks](docs/callbacks.md), [Types](docs/types.md) |
| Build gameplay features | [Accounts and player sessions](docs/accounts.md), [Saving](docs/saving.md), [Transactions](docs/transactions.md), [SQL file sections](docs/sql-sections.md) |
| Operate safely | [Troubleshooting](docs/troubleshooting.md), [Debugging and logging](docs/debugging.md), [Security and concurrency](docs/security.md) |
| Understand and contribute | [Architecture](docs/design.md), [Windows builds](docs/build-windows.md), [Linux builds](docs/build-linux.md), [Language policy](docs/language-policy.md) |
| Review evidence and limits | [Validation](docs/validation.md), [Performance](docs/performance.md), [Pawn logging qualification](docs/logging-qualification.md), [Independent audit](docs/independent-audit.md) |
| Package and release | [Packaging](docs/packaging.md), [Readiness](docs/rc-readiness.md), [Pre-soak checklist](docs/pre-soak-checklist.md), [Release procedure](docs/releasing.md), [Release notes](docs/release-notes.md) |

Prefer [custom completion callbacks](docs/callbacks.md) for new application code;
`OnPgComplete(job, context)` remains supported. Pre-RC.1 code using the removed
`pg_prepare_section` must migrate to `pg_prepare_file(..., section)` and recompile;
see [cleanup and migration](docs/prestable-cleanup.md) and the
[bounded API audit](docs/api-modernization.md). Include and component versions must match.

## Building and Testing

Requirements: **CMake 3.24+**, a **C++20** compiler, Git and **libpq 17+** with TLS and thread
support. FetchContent obtains the SDK and Pawn headers at pinned commits; Boost and pawn-natives
are not required. Build all dependencies for the server's architecture and ABI. Linux x86 needs
a multilib toolchain and 32-bit libpq; Windows MSVC/MinGW builds need compatible import libraries.
Follow the [Windows](docs/build-windows.md) or [Linux](docs/build-linux.md) guide for details.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPostgreSQL_ROOT=/path/to/postgresql
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/server
```

`PostgreSQL_ROOT` contains the include/lib directories. `PG_BUILD_COMPONENT=OFF` builds only
the engine and tests. `PG_PAWN_INCLUDE_DIR` overrides the install destination, `qawno/include`.
Use `PG_SANITIZE=ON` for ASan/UBSan on supported toolchains, or a separate Linux x64 build
with `PG_THREAD_SANITIZE=ON`. See [qualification controls](docs/validation.md#sanitizers-and-server-integration)
for stress reproduction and limits.

Compile Pawn with the official open.mp **3.10.11** compiler, **32-bit cells**, official includes
and warnings treated as errors (`-E`). CI compiles all thirteen public examples and the
classified Markdown examples, including from extracted x86 packages. Start with
[smoke.pwn](examples/smoke.pwn) and expect `PG SMOKE PASS` in a disposable server; the
[example catalogue](docs/examples.md) describes the broader learning and validation paths.
Eleven public examples run unattended. The interactive account example requires an optional
external hashing provider and denies authentication by default. The
[bounded real-client qualification](docs/validation.md#real-player-sessions-october-24-2026)
excludes rendered clients, the official-client encrypted connection path, untested
providers/environments and the 24-hour campaign.

For a temporary, loopback-only TLS database and CTest (put `initdb`, `pg_ctl`, `psql`, `openssl`
and `ctest` on PATH; use a non-root Unix account):

```sh
python tests/run_postgres.py --build build
```

The runner shuts down its temporary instance even on failure and leaves the directory for
inspection; it does not use an existing database. CTest skips integration with exit code 77
when `OMP_PG_TEST_SERVICE` is unset; a skip is not a successful integration run. See
[validation](docs/validation.md) for lifecycle, stalled-connection, native-boundary and stress
coverage. Use the existing [benchmark procedure](docs/performance.md) and
[Pawn workload evidence](docs/logging-qualification.md) with their recorded source identities:
local samples are not performance guarantees, do not isolate component tick cost and do not
establish superiority over other components. Direct libpq/engine probes do not measure Pawn ticks.

Keep comments, diagnostics, examples and documentation in natural technical English. Follow
`.editorconfig` and `.clang-format`; C++/Pawn use tabs displayed as four columns. Pawn formatting
is manual and compiler-checked. Run checks appropriate to the change; documentation checks are:

```sh
python scripts/check_docs.py
python scripts/check_english.py
python scripts/compile_examples.py --compiler /path/to/pawncc --include pawn --include /path/to/omp-stdlib --include /path/to/pawn/include --output build --docs
```

For code changes, format C++ with clang-format, check Python/CMake with
`python -m black --check tests` and `python -m cmakelang.format --check CMakeLists.txt`, and run
the relevant unit, integration and server checks before publishing.

### Release status and publication

Stable v0.1.0 remains blocked. Required corrections need owner-approved integration and
successful CI on the **exact final `master` commit**, followed by a freeze and isolated-host preflight
including **100,000 semantic fuzz cases** before the final **24-hour campaign**. The campaign
and tag/publication each require separate owner authorization. Later source or deployment
changes require impact review, affected checks and a new freeze/final campaign; see
[readiness](docs/rc-readiness.md) and the [release procedure](docs/releasing.md).

CI produces Windows/Linux x86 ZIPs and SHA-256 after fresh-server tests of extracted packages.
These are qualification artifacts; building them does not publish a release. The existing
[v0.1.0-rc.1](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.1) and
[v0.1.0-rc.2](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.2)
retain their original tags, assets, checksums, release notes and historical evidence.

## License

Original project code is licensed under [MIT](LICENSE). Dependencies, the SDK and Pawn retain
their licenses and notices; no reference plugin is linked or copied. Retain the installed
notices in `share/omp-postgresql` and the packaged dependency licenses. See
[NOTICE.md](NOTICE.md) and [DEPENDENCIES.md](DEPENDENCIES.md) for attribution and runtime requirements.
