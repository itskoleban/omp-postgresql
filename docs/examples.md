# Examples and learning path

All thirteen examples use the public include. The table distinguishes unattended
database/server checks from interactive flows requiring an external provider or player setup.
Each `.pwn` is independently compiled with warnings as errors; compile success alone is
not runtime qualification. The [documentation audit](documentation-audit.md) retains its
historical revision; see [current readiness](rc-readiness.md) for later integrated evidence.

For short fragments to read before a complete gamemode, start with
[connection and first query](getting-started.md#follow-one-asynchronous-operation), then
[parameterized insert/delete](api.md#insert-and-delete-one-record),
[individual/snapshot/UNNEST saves](saving.md), [guarded callbacks](callbacks.md),
[transaction sequencing](transactions.md), [SQL sections](sql-sections.md) and
[logging/errors](debugging.md). Their stated prerequisites matter: fragments are not
additional runnable examples, and the thirteen-file inventory below is unchanged.

| Order / purpose                                                                        | Source and fixtures                                                                              | Prerequisites and expected result                                                                                                                                                                        |
| -------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1. Connect, CRUD, context, RETURNING, result columns, affected rows, SQLSTATE          | [basic.pwn](../examples/basic.pwn)                                                               | `game` service, one lane; creates session-local table; `PG BASIC PASS`.                                                                                                                                  |
| 2. Prepared scalar parameters and precision                                            | [values.pwn](../examples/values.pwn)                                                             | No table; validates quote-containing text, false, BIGINT, JSONB metadata and NULL; `PG VALUES PASS`.                                                                                                     |
| 3. Individual saves, repeated snapshots and variable-size UNNEST                       | [saving.pwn](../examples/saving.pwn), [tutorial-select.sql](../examples/sql/tutorial-select.sql) | Copy SQL to `scriptfiles/sql/tutorial-select.sql`; temporary table on lane 0; `PG SAVING PASS`.                                                                                                          |
| 4. Atomic money transfer                                                               | [transfer.pwn](../examples/transfer.pwn)                                                         | Temporary bank table, sorted row locks, checked balances and explicit COMMIT; `PG TRANSFER PASS`.                                                                                                        |
| 5. Named sections and reusable statements                                              | [sections.pwn](../examples/sections.pwn), [accounts.sql](../examples/sql/accounts.sql)           | Copy to `scriptfiles/sql/accounts.sql`; temporary account table; `PG SECTION PASS`.                                                                                                                      |
| 6. Homogeneous snapshots, arrays and transaction lifecycle                             | [smoke.pwn](../examples/smoke.pwn)                                                               | Temporary fixtures; twenty SQL executions and one bulk update; `PG SMOKE PASS`. Reload regression also runs this mode.                                                                                   |
| 7. Heterogeneous batches, nullable arrays, BYTEA, savepoints, streaming and statistics | [extensions.pwn](../examples/extensions.pwn)                                                     | Two lanes, no persistent data; bounded block consumption; `PG EXTENSION PASS`.                                                                                                                           |
| 8. Startup properties, vehicles and clans                                              | [bootstrap.pwn](../examples/bootstrap.pwn)                                                       | Temporary fixtures on lane 0; validates world data before enabling players; `PG BOOTSTRAP PASS`.                                                                                                         |
| 9. Interactive registration/login and player generations                               | [accounts.pwn](../examples/accounts.pwn), [tutorial-auth.sql](../examples/sql/tutorial-auth.sql) | Manually install persistent fixture schema; optional external bcrypt provider. Default build denies authentication. Read [its limitations](accounts.md) before enabling. No unattended login PASS claim. |
| VM regressions                                                                         | [callbacks.pwn](../examples/callbacks.pwn), [unload.pwn](../examples/unload.pwn)                 | Callback error/suspension restoration and filterscript unload with pending work. The runner drives console reload/unload; these are test fixtures.                                                       |

[pg_service.conf](../examples/pg_service.conf) supplies credential-free connection settings.
[postgresql.sql](../examples/postgresql.sql) and [pg_hba.conf](../examples/pg_hba.conf) are
administrative starting points, not automatic migrations or production permission policies.
Do not execute every `.sql` file as a batch: some are named-section containers, others are
single queries and the authentication fixture creates a persistent table.

## Compile everything

The repository helper discovers all `.pwn` files, so newly added examples cannot silently
miss compilation. From a source checkout with compiler/includes already installed:

```sh
python scripts/compile_examples.py --compiler /path/to/pawncc --include pawn \
    --include /path/to/omp-stdlib --include /path/to/pawn/include --output build-examples
python scripts/check_docs.py
```

CI repeats compilation using the extracted package's `qawno/include`. It runs the original
six server scenarios plus basic/values/saving/transfer and logging: eleven unattended
public examples against a disposable TLS PostgreSQL instance. The separate
[real Pawn measurement fixture](logging-qualification.md) is under `tests/` and is not a
fourteenth public example. Separate [bounded real-client qualification](validation.md#real-player-sessions-october-24-2026)
exercises account/custom flows, player-slot reuse, external bcrypt callbacks and uncertain
registration with ordinary-protocol headless clients on both x86 platforms. Those finite
scenarios exclude rendered clients, the official-client encrypted connection path,
untested providers/environments and the deferred 24-hour campaign. Validate your own
interactive client, provider and application policies before deployment.

## Cleanup and adapting fixtures

Each unattended tutorial stops after its marker and closes its connection. Completed jobs
are released in callbacks. Pending jobs at gamemode exit belong to that VM and are cancelled
and collected on unload; this does not wait for writes or prove their outcome. A long-running
server should quiesce submissions, drain/reconcile outstanding saves, and close jobs before
unloading the script. Never rely on `OnGameModeExit` for a guaranteed final COMMIT.

Temporary tables are private to one physical PostgreSQL session. Keep their queries on
the creating lane; reconnect destroys them. Real accounts/world data require a persistent,
operator-managed schema accessible to all relevant lanes. The authentication example uses
such a table but deliberately omits production schema management.

## Custom callbacks and migration

- [Custom gameplay requests](../examples/custom.pwn): interactive account/clan loads and
  account/property saves, concurrent requests, copied player/session/request arguments and
  explicit failure/uncertainty handling. Operator schema and RCON-admin prerequisites:
  [callback guide](callbacks.md#complete-gameplay-example). Interactive behavior needs operator validation.
- [Accounts](../examples/accounts.pwn) now uses custom completion handlers with exact job/token
  checks. Its external password provider remains optional and authentication still fails closed.
- [Callbacks](../examples/callbacks.pwn) now tests custom scalar/string/no-argument handlers,
  failures, VM frame restoration and all eleven asynchronous natives, including stream completion.
- [Sections](../examples/sections.pwn) selects named sections through `pg_prepare_file`.
  Basic, saving, transfer and extension examples continue demonstrating `OnPgComplete`.

Use the [callback contract](callbacks.md) and [API audit](api-modernization.md) for migration.

## Pre-stable cleanup

Use `pg_prepare_file(connection, filename, parameters, prepared, section)` for named SQL.
`pg_prepare_section` was removed before RC.1. Older pre-RC.1 programs that use it require
source migration and AMX recompilation; RC.2 preserves RC.1's public Pawn signatures.
Other leading argument/default positions remain supported. See [migration](prestable-cleanup.md)
and [internal diagnostics](debugging.md).

## Logging and source diagnostics

[logging.pwn](../examples/logging.pwn) selects both outputs, prepares a named section,
uses an owned custom callback string, prints a missing-section failure and explicitly
closes the completed job. It deliberately requests `load_acount` from
[accounts.sql](../examples/sql/accounts.sql); correcting it requires binding the real section
parameters. Source and extracted-package x86 runs require `PG LOGGING PASS`.

## Maintaining documentation examples

Use the [operation selector](quick-reference.md) for a compact map of the API. Public
Markdown Pawn fences are also validation inputs, distinct from the thirteen runnable
gamemodes above. Do not turn a fragment into a complete program just to compile it.

Place `<!-- pawn-example: unique.id -->` immediately before each runnable `pawn` fence.
Add that ID once to a wrapper in `tests/doc_examples.json`; `{{unique.id}}` inserts the
fence verbatim. Wrappers supply only surrounding declarations, a function body for an
inline fragment, and compile-only calls that keep stock functions checked. Generated
`.pwn`/`.amx` files belong in an ignored build directory, never the public example inventory.

Native/forward declaration fences are recognized automatically and compared with the
actual public include. Other intentionally incomplete syntax needs
`<!-- pawn-fragment: explanation of why this is not compilable -->`; the checker reports
every such exception. New/unclassified fences, duplicate IDs, stale wrapper references
and snippets omitted from wrappers fail validation. Backtick and tilde fences are supported.
Keep tutorial fences outside list/quote containers; unsupported nested Pawn fences fail
discovery rather than silently escaping compilation.

From a source checkout, use the same pinned Pawn compiler and official includes as CI:

```sh
python scripts/check_docs.py
python scripts/compile_examples.py --compiler /path/to/pawncc --include pawn \
    --include /path/to/omp-stdlib --output build-doc-check --docs
python -m unittest discover -s tests -p 'test_doc*.py' -v
```

`--docs` adds Markdown compilation to the existing example runner; it works against extracted
package documentation/includes too, using the checkout's wrapper manifest. Failures name the
original Markdown file/line. Literal notified custom handlers are made compiler-visible to
check their names and signatures; literal format/argument counts are checked as well.
Dynamic callback routing, public lookup at runtime, VM/player lifetimes, readiness,
uncertainty and cleanup still require implementation review and runtime tests.

Selected SQL fixtures use `<!-- sql-example: unique.id -->`. The existing disposable TLS
PostgreSQL runner executes the schema, CRUD, save/UNNEST and section-query checks when passed
`--doc-sql`; no database is needed for the Python documentation unit tests. For example:

```sh
python tests/run_postgres.py --build build --doc-sql
```

This command creates and stops a fresh cluster, alongside its normal CTest qualification.
The SQL checks run in a unique schema in one rolled-back transaction; they do not load an
existing application database. Operator setup sketches and historical SQL are not all
automatically executed. SQL success does not validate Pawn binding, callback scheduling,
interactive authentication, full gameplay behavior or production suitability.
