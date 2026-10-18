# Your first database-backed gamemode

omp-postgresql runs PostgreSQL work on background workers and delivers results to Pawn on
server ticks. Your gamemode submits a job, returns to the game, and handles completion later.
A job is a handle to pending work, not a row, success flag or connection. Begin with
[installation](installation.md), then run the [basic example](../examples/basic.pwn).

## Install and configure

Use a verified Windows x86 or Linux x86 package matching the server architecture. Keep the
open.mp Pawn component enabled. Install the runtime libraries as described in
[DEPENDENCIES.md](../DEPENDENCIES.md); copying only the component is insufficient.

Create a database and restricted application role through an operator's PostgreSQL session.
The [administrative sketch](../examples/postgresql.sql) creates a role/database, but an
application should normally use a separate schema owner and grant its runtime role only
the table/sequence permissions it needs. Do not give a public game server superuser access.
Apply schema changes outside the gamemode. The component is not a migration manager.

Copy [pg_service.conf](../examples/pg_service.conf) to an operator-controlled location and
edit host, database, user and CA paths. Configure these variables **in the process that
launches open.mp**, before it starts:

```sh
export PGSERVICEFILE=/srv/openmp-private/pg_service.conf
export PGPASSFILE=/srv/openmp-private/pgpass
chmod 600 "$PGPASSFILE"
./omp-server
```

```powershell
$env:PGSERVICEFILE = 'C:/openmp-private/pg_service.conf'
$env:PGPASSFILE = 'C:/openmp-private/pgpass'
./omp-server.exe
```

The password file uses `hostname:port:database:username:password`, with values matching the
profile. Escape `:` and `\` as required by [libpq's password-file format](https://www.postgresql.org/docs/17/libpq-pgpass.html).
Create it privately; never paste real passwords into Pawn, logs or tracked examples. On
Windows protect it with user/service-account ACLs. Paths in the example profile are Linux
placeholders; use absolute paths suitable for your deployment. Remote TLS should use
`sslmode=verify-full` and a CA/hostname matching the server certificate. Certificate
authentication can replace a password when PostgreSQL is configured for it.

`pg_connect("game")` passes the service name to libpq. Pawn does not read or parse
`pg_service.conf`; libpq reads it on the worker. Environment edits after server startup do
not update the running process. `OMP_PG_*` component limits are read at component creation.

## Compile and run

Use Pawn 3.10.11 with 32-bit cells and the official open.mp standard library. From an
extracted package root (create `build/` first):

```sh
pawncc examples/basic.pwn -E -iqawno/include -i/path/to/omp-stdlib -obuild/basic.amx
```

In a source checkout use `-ipawn`. Install `basic.amx` under `gamemodes/` in a disposable
open.mp server and select `basic 1` in `pawn.main_scripts` in `config.json`. Supply the
service profile above to that process. The example creates a temporary table on lane 0,
performs INSERT/SELECT/UPDATE/DELETE, checks RETURNING and affected rows, and deliberately
tests division by zero. Expected console marker: `PG BASIC PASS`. A SQL error in this last
step is intentional; an earlier error is a failed run. No persistent tables are modified.

## Follow one asynchronous operation

1. `OnGameModeInit` receives a connection handle immediately.
2. `OnPgConnection` observes `PG_CONNECTED` after the entire pool is ready.
3. `pg_query` accepts trusted SQL and returns a positive job handle, or 0 with a local error.
4. `OnPgComplete(job, context)` reads terminal status, checks result shape and converts data.
5. `pg_job_close(job)` releases that job and its results. Connection shutdown is separate.

Callbacks run in the owning script on the main thread. Return promptly without `sleep`.
Use `context` to identify the step/request. Never wait in a Pawn loop for a worker to finish.
For an untrusted username, do not format it into SQL: prepare `WHERE username=$1`, bind it,
call `pg_add`, then `pg_execute`. [values.pwn](../examples/values.pwn) demonstrates quote-safe
text binding, integers, floats, booleans, BIGINT, JSONB and NULL without a table.

## Continue learning

Read the [example catalogue](examples.md) in order, then the [execution guide](api.md) and
[complete native/callback reference](native-reference.md). The
[account tutorial](accounts.md) explains stale player sessions and external password hashing;
[saving](saving.md) compares repeated snapshots with one UNNEST statement;
[transactions](transactions.md) covers transfers, rejected COMMIT and reconciliation.
[Troubleshooting](troubleshooting.md) maps errors to the right inspection tools.
