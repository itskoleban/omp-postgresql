# Connection readiness qualification

[Issue #58](https://github.com/itskoleban/omp-postgresql/issues/58) tracks Winsock 10057
during direct Neon startup with RC.3. The RC.3 driver enters libpq's polling state
machine before the first write-readiness notification, and it can reenter after a
`select` timeout or interruption. These defects are reproducible in `socket_wait_unit`.
In a controlled Windows x86 run, a full TCP accept queue held connections pending
for one second. RC.3 on open.mp 1.6.7.3218 returned the same Winsock 10057 before the
gate opened. With the RC.3 libpq and OpenSSL DLLs, the corrected driver waited and
completed TLS, SCRAM channel binding and `SELECT 1` after the gate opened.

The [original reporter](https://github.com/itskoleban/omp-postgresql/issues/58#issuecomment-7084324361)
subsequently verified startup and SQL on one and two lanes against their direct Neon
service on Windows 11 with open.mp 1.6.7.3218 and confirmed installing the corrected
Windows x86 CI development package. Loaded DLL paths and hashes on the reporter's
host remain unverified. The comparison below controls for the runtime DLLs to
investigate the historical error. A same-session Neon comparison and
forced live Neon recovery remain unverified.

The connection driver waits for the direction requested by libpq and retrieves the
current socket on each wait. Winsock also reports failed nonblocking connects through
the exceptional set. This allows libpq to inspect the socket error and handle address
fallback. A 10 ms maximum wait slice bounds shutdown detection during socket waits.
`OMP_PG_CONNECT_MS` bounds the polling phase after `PQconnectStartParams` returns.
Libpq can run synchronous resolver or provider calls during startup or polling.
The worker observes the deadline and shutdown between those calls. The profile's
`connect_timeout` applies to the blocking `psql` control. See the
[libpq 17 connection contract](https://www.postgresql.org/docs/17/libpq-connect.html#LIBPQ-PQCONNECTSTARTPARAMS)
and [Winsock select](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select).

## Reproduce on an authorized Neon project

Use two disposable installations of official Windows x86 open.mp 1.6.7.3218. Use the
verified [RC.3 distribution](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.3)
in both copies. In the candidate copy replace `components/PostgreSQL.dll` with the
Windows x86 test artifact built from the corrected source. Keep the RC.3 libpq and
OpenSSL DLLs resident in both server directories and remove unrelated PostgreSQL
directories from the server's search path. While each server is running, verify that
the loaded `libpq.dll`, `libssl-3.dll` and `libcrypto-3.dll` match the intended copies
by path and SHA-256. From 32-bit PowerShell on Windows use
`Get-Process -Id <pid> -Module` to list the x86 server's loaded modules.

Use your existing private `[game]` service with the direct endpoint and a private
password file. For the incident comparison the service settings are `sslmode=require`,
`channel_binding=require`, and `connect_timeout=15`. Keep `sslmode=verify-full` with an
appropriate CA and server identity in a separate verification run. Limit the test role
to login and read-only access needed for the probe. Follow the
[private file setup](getting-started.md#install-and-configure) for permissions.

Set the following in the PowerShell session that launches the disposable servers and
run the SQL control. The component settings must be identical for both servers. A
second comparison can retain the original retry and component deadline settings.

```powershell
$env:PGSERVICEFILE = 'C:/private/pg_service.conf'
$env:PGPASSFILE = 'C:/private/pgpass'
$env:OMP_PG_CONNECT_MS = '15000'
$env:OMP_PG_RECONNECT_ATTEMPTS = '0'
psql -X -w 'service=game' -v ON_ERROR_STOP=1 -c 'SELECT 1'
```

Compile the Pawn probe at `tests/neon_connection.pwn` in the source checkout using
Pawn 3.10.11 and the official open.mp standard library. Create `build/` first.

```powershell
pawncc tests/neon_connection.pwn -E -ipawn -iC:/path/to/omp-stdlib -obuild/neon_connection.amx
```

Install the AMX as `gamemodes/neon_connection.amx` in each server. Configure
`pawn.main_scripts` to contain `neon_connection 1` and bind the test instances to
loopback. Run the RC.3 server, then the candidate with the same service. The probe
attempts a one-lane pool, then a two-lane pool even if the first attempt fails. It
checks a read-only query on each ready lane and distinct backend PIDs for two lanes.
It prints only a phase marker, ready lane count or elapsed time. If the two-lane
terminal marker does not appear after both attempts have had time to finish, stop
the disposable server and record which stage stalled. Repeat in alternating order
to reduce the effect of Neon wake-up and repeat on 1.5.8.3079 if that version is part
of qualification.

Record open.mp/component versions, runtime identities, terminal markers and timings.
Keep endpoint names, profile contents, connection strings, environment dumps,
password files and unredacted diagnostic messages private. A baseline that also
succeeds leaves the original incident unconfirmed, even if the candidate passes.
