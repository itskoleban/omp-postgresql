# Asynchronous registration and login

[accounts.pwn](../examples/accounts.pwn) is an interactive integration tutorial, not a
production authentication package. It uses the player's current name as an exact,
case-sensitive account key; a real project must define name normalization, renaming,
session exclusivity, abuse limits, password reset and account recovery policies.

## Prerequisites and password provider

Apply [tutorial-auth.sql](../examples/sql/tutorial-auth.sql) manually to a **disposable**
database using a schema-owner role. Give the `game` runtime role SELECT/INSERT privileges
on `tutorial_accounts` and USAGE on its identity sequence. Do not grant schema ownership
merely to run the tutorial. Multiple lanes can access this persistent table, unlike the
temporary tables in the unattended examples. Delete the fixture manually after testing.

The default build compiles with only open.mp and omp-postgresql. Its password adapters
return false: it can look up an account but **cannot register or authenticate anyone**.
This deliberately fails closed rather than supplying a fake hash or plaintext password.

The optional `TUTORIAL_BCRYPT` build uses the external
[samp-bcrypt provider API](https://github.com/Sreyas-Sreelal/samp-bcrypt/blob/c60e68d483a95d109855fdd7517425c0363fd10c/include/samp_bcrypt.inc).
Install that provider/include separately, verify its server/architecture compatibility, and
compile with the symbol `TUTORIAL_BCRYPT=1` and its include directory. The provider is not
bundled or implemented by omp-postgresql. Its asynchronous hash/verify callbacks carry an
extra integer token through the provider's `args` mechanism. No password is sent to
PostgreSQL; only the encoded hash is bound for INSERT. Never log password inputs or hashes.
At the pinned provider revision, the hash dispatcher supplies `(playerid, token)`;
verification supplies `(playerid, bool:success, token)`. Its
[dispatcher implementation](https://github.com/Sreyas-Sreelal/samp-bcrypt/blob/c60e68d483a95d109855fdd7517425c0363fd10c/src/plugin.rs)
is authoritative for that adapter; the include's illustrative `hashid` comment differs.
The example accepts 12..64 input bytes and cost 12. Choose and measure the provider's policy
for your server; authentication input must reach it through a suitable protected channel.

Provider-enabled compilation does not establish runtime compatibility or cryptographic
quality. The tutorial is not deployable until the provider, interactive client flow,
timeouts, global rate limits and recovery paths have been exercised in your environment.
When adapting another provider, preserve the callback token check and fail-closed behavior.

## Request flow

On connection, the player is gated from spawning. Once the database is ready, a prepared
lookup binds the account name. No row prompts registration; one row copies the account ID,
hash and score into that session's staging data. A password dialog starts hashing or
verification; only successful verification or a confirmed registration enables spawning.
Account data is applied with `SetPlayerScore` only after authentication. SQL confirmation
of a lookup is not authentication.

Registration is one parameterized INSERT with RETURNING. The database UNIQUE constraint
arbitrates duplicate names; a prior missing-row lookup cannot reserve a name. SQLSTATE
`23505` prompts a fresh login. A failed or uncertain INSERT is never automatically replayed.
Reconnect and look up the name to reconcile whether registration occurred, then prove
password ownership. Do not treat a duplicate account as belonging to the current player.

The example allows one database request per player and one password attempt per dialog.
It does not implement a general request queue. Local rejection prints a local diagnostic;
asynchronous errors are read from the job and shown to players as generic messages.
Additional retries must be explicitly rate-limited. A 30-second timer invalidates a hash/
verify attempt whose provider never calls back; it cannot cancel the provider's own work.
SQL messages can contain private data; filter logs.

## Player IDs are reusable slots

`context=playerid` alone is unsafe. Player 4 can disconnect while a query runs, and a new
person can receive slot 4 before its callback. This tutorial tracks both the exact job
handle and a monotonic generation token. It checks that the player is still connected and
that the token belongs to the current lookup/authentication attempt. Tokens are invalidated
on disconnect and database readiness loss. The counter never wraps into an old live token;
when exhausted, the tutorial rejects new sessions/attempts instead. The counter is kept
in the operator-controlled server variable `pg_tutorial_auth_token`, surviving gamemode
reloads while provider work may still be pending. Do not delete/reset it or run multiple
copies with the same callback names. The external provider's callback dispatch is distinct
from the component's VM ownership and requires its own lifecycle qualification.

Late database callbacks always close their completed jobs even when no player owns them.
Late hashing callbacks cannot modify a reused player slot or an invalidated attempt.
Only operator-controlled scripts/providers may invoke the authentication callbacks. The
component's VM ownership protects scripts, not one player from another inside a script;
the generation check is the gamemode's responsibility.

## Reconnects, saves and shutdown

Loss of readiness gates every player and invalidates authentication attempts. A later
`PG_CONNECTED` starts a fresh lookup after an old request has been collected. It does not
replay registration. The example requires reauthentication and does not preserve an
authenticated player across an outage. An exhausted connection needs operator/application
policy to close and recreate the logical pool; reconnecting the transport is not restoring
an application session.

Statistics saving is taught separately in [saving](saving.md). Copy persistent account IDs,
values and expected versions before disconnect clears player state. A save's completion may
update persistent-save bookkeeping after logout, but may only update a player's UI after a
matching generation check. A write already accepted belongs to its job even if its logical
statement closes. Cancellation or VM unload is not proof that the write never happened.

Before adapting this tutorial for a real server, test disconnect during lookup/hash/verify/
INSERT, immediate slot reuse, duplicate registration, wrong password, provider timeout,
readiness loss/recovery, missing schema, permission denial and lost INSERT response. These
interactive/provider tests are not covered by the unattended SQL tutorials.

## Custom SQL completion routing

The account tutorial now submits `OnAccountComplete(job, playerid, token)` directly with
format `"ii"`. It validates player bounds/connection, the exact PendingJob, and session token
before applying results; no player-array scan is needed at completion. Readiness-loss,
disconnect, password-provider and uncertain-write behavior remain as documented above.
Legacy completion examples remain available in the [catalogue](examples.md).
