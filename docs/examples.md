# Examples and learning path

All examples use the public include. The table distinguishes unattended database/server
checks from interactive flows requiring an external provider. Each `.pwn` is independently
compiled with warnings as errors; compile success alone is not runtime qualification.
Exact evidence for this documentation revision is in [the audit record](documentation-audit.md).

| Order / purpose | Source and fixtures | Prerequisites and expected result |
| --- | --- | --- |
| 1. Connect, CRUD, context, RETURNING, result columns, affected rows, SQLSTATE | [basic.pwn](../examples/basic.pwn) | `game` service, one lane; creates session-local table; `PG BASIC PASS`. |
| 2. Prepared scalar parameters and precision | [values.pwn](../examples/values.pwn) | No table; validates quote-containing text, false, BIGINT, JSONB metadata and NULL; `PG VALUES PASS`. |
| 3. Individual saves, repeated snapshots and variable-size UNNEST | [saving.pwn](../examples/saving.pwn), [tutorial-select.sql](../examples/sql/tutorial-select.sql) | Copy SQL to `scriptfiles/sql/tutorial-select.sql`; temporary table on lane 0; `PG SAVING PASS`. |
| 4. Atomic money transfer | [transfer.pwn](../examples/transfer.pwn) | Temporary bank table, sorted row locks, checked balances and explicit COMMIT; `PG TRANSFER PASS`. |
| 5. Named sections and reusable statements | [sections.pwn](../examples/sections.pwn), [accounts.sql](../examples/sql/accounts.sql) | Copy to `scriptfiles/sql/accounts.sql`; temporary account table; `PG SECTION PASS`. |
| 6. Homogeneous snapshots, arrays and transaction lifecycle | [smoke.pwn](../examples/smoke.pwn) | Temporary fixtures; twenty SQL executions and one bulk update; `PG SMOKE PASS`. Reload regression also runs this mode. |
| 7. Heterogeneous batches, nullable arrays, BYTEA, savepoints, streaming and statistics | [extensions.pwn](../examples/extensions.pwn) | Two lanes, no persistent data; bounded block consumption; `PG EXTENSION PASS`. |
| 8. Startup properties, vehicles and clans | [bootstrap.pwn](../examples/bootstrap.pwn) | Temporary fixtures on lane 0; validates world data before enabling players; `PG BOOTSTRAP PASS`. |
| 9. Interactive registration/login and player generations | [accounts.pwn](../examples/accounts.pwn), [tutorial-auth.sql](../examples/sql/tutorial-auth.sql) | Manually install persistent fixture schema; optional external bcrypt provider. Default build denies authentication. Read [its limitations](accounts.md) before enabling. No unattended login PASS claim. |
| VM regressions | [callbacks.pwn](../examples/callbacks.pwn), [unload.pwn](../examples/unload.pwn) | Callback error/suspension restoration and filterscript unload with pending work. The runner drives console reload/unload; these are test fixtures. |

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
six server scenarios plus basic/values/saving/transfer against a disposable TLS PostgreSQL
instance. Interactive players, password-provider behavior and adversarial account races
require separate qualification; see [validation](validation.md).

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
- [Sections](../examples/sections.pwn) uses the unified file API and retains a compatibility call.
  Basic, saving, transfer and extension examples continue demonstrating `OnPgComplete`.

Use the [callback contract](callbacks.md) and [API audit](api-modernization.md) for migration.

## Pre-stable cleanup

Use `pg_prepare_file(connection, filename, parameters, prepared, section)` for named SQL.
The removed `pg_prepare_section` requires source migration and AMX recompilation; other
leading argument/default positions remain supported. See [migration](prestable-cleanup.md)
and [internal diagnostics](debugging.md).
