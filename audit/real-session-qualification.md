<a id="integración-real-de-openmp--omp-postgresql-v010"></a>

# Real open.mp integration - omp-postgresql v0.1.0

English translation of the maintained narrative at `c8528bf89068dfd33fd96bf7350ee59e599d278f`. Historical statements retain their original dates and scope; this translation does not rerun tests or change evidence. Numeric spelling and legacy heading IDs are preserved.

Date: October 24, 2026 (America/Bogota). Branch: `codex/real-player-qualification`.
PR: https://github.com/itskoleban/omp-postgresql/pull/27.

Later review in the same PR: [privacy, integrity, Windows and provenance](real-session-review.md).
Files cited below are now explicitly identified sanitized copies where necessary;
original results and hashes are retained in [the evidence contract](real-session-evidence/README.md).
New CI is evaluated against its own SHA and does not inherit historical results.

<a id="identidad-y-alcance"></a>

## Identity and scope

The inspected and rechecked `master` is
`79cf0e659e426af559ecc305f4009104d77eb64b`. Only four audit files changed between
`177c95f` and that master; the 227 historical executions remain bound to their
original commit and are not added to these results. Complete runs use
`28933d11ef3edbfd5cc24f929109c4961ddeb5b2` (Linux) and
`063191e23f06e5df3bcc7349f77bade9805abb11` (Windows). The latter adds the deterministic
exhaustion wait, initial provenance and sequence-based no-replay assertion; the last
was also executed separately on Linux. Manifests record a clean checkout and input/
harness hashes. The component was rebuilt from master; `src/`, `include/` and `pawn/`
are identical on the test branch. Implementation, Pawn API and ABI are unchanged.

`docs/accounts.md`, `examples/accounts.pwn`, `examples/custom.pwn`, `docs/validation.md`,
the two requested historical qualifications and workflows were reviewed. Default
authentication still fails closed without a provider. The new suite enables and
actually executes `samp-bcrypt`; it neither substitutes a fictional hash nor
synthetically calls `OnPlayerConnect` or `OnPlayerDisconnect`.

<a id="viabilidad-comprobada"></a>

## Verified feasibility

| Capability        | Evidence and conclusion                                                                                                                                                                                                                                                                                           |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Official server   | open.mp **1.5.8.3079 x86** started and loaded the component and Pawn on Windows and Linux. This is the project-pinned version; the conclusion does not extend to other servers.                                                                                                                                   |
| Automated client  | RakSAMP-cli `f75af4cb97fed341cb45b49aff489f9f20665e99`, built from source. Uses normal player RakNet handshake/RPC, answers dialogs over the network and runs in an external process. The server observed `IsPlayerNPC=0`, connections, disconnections and new sessions.                                          |
| Client adaptation | The retained patch supports piped stdin, avoids interactive dialog UI and avoids interpreting `-n/-p` fragments in Windows paths as legacy options. It does not change the protocol. LuaJIT was built from `a4c064539ea931274d407e1249b4bb575424a0f4`; the prebuilt requiring unavailable MSVCR120 was discarded. |
| Real reuse        | A and B were distinct client processes; the same `playerid=0`, different tokens and A's callback after B connected were observed. Verified for lookup, hash, verify, INSERT, custom load and save.                                                                                                                |
| Official NPC      | `NPC_Create`/`NPC_Destroy` executed; `npc=1`, slot 7 and deferred removal were observed. These exercise server-entity callbacks and cleanup and are not counted as player connections.                                                                                                                            |
| Provider          | **samp-bcrypt 0.4.1** x86 binaries, revision `c60e68d483a95d109855fdd7517425c0363fd10c`. Real hash/verification callbacks were observed, with tokens passed through `args`.                                                                                                                                       |
| GitHub Actions    | Linux/Windows x86 jobs prepare pinned clients/provider, compile both fixtures and run the bounded suite. This compatibility protocol needs neither GTA, a desktop nor a private runner.                                                                                                                           |
| Dedicated runner  | An official graphical client and the open.mp client's encryption path require an authorized game installation and a suitable Windows runner. That path was not evaluated and cannot be inferred from RakSAMP.                                                                                                     |

Primary sources:
[official release](https://github.com/openmultiplayer/open.mp/releases/tag/v1.5.8.3079),
[NPC_Create](https://open.mp/docs/scripting/functions/NPC_Create),
[IsPlayerNPC](https://open.mp/docs/scripting/functions/IsPlayerNPC),
[pinned client](https://github.com/pavetr1337/RakSAMP-cli/tree/f75af4cb97fed341cb45b49aff489f9f20665e99),
[provider and dispatcher](https://github.com/Sreyas-Sreelal/samp-bcrypt/tree/c60e68d483a95d109855fdd7517425c0363fd10c).

<a id="entorno-aislado-y-dependencias"></a>

## Isolated environment and dependencies

Server and PostgreSQL bound exclusively to `127.0.0.1`. There was no announcement,
public query, remote RCON or published Docker port. Each execution created a new
cluster and stopped it afterward. Only fictional accounts were used. `qa_runtime`
received SELECT/INSERT and USAGE on the tutorial sequence, plus SELECT/UPDATE needed
by custom; it was neither owner nor superuser.

Normal connections used TLS `verify-full`. The sole exception was the confirmation-
loss peer, on loopback without TLS to identify and discard exactly
`CommandComplete INSERT 0 1`. This exception is confined to the tested fault. The
harness did not retry write SQL after the uncertain result.

| Dependency                    | Local Windows                                        | Local Linux                                           |
| ----------------------------- | ---------------------------------------------------- | ----------------------------------------------------- |
| Server/component architecture | PE32 x86 / MSVC 19.51.36248                          | ELF32 x86 / GCC 13.3.0                                |
| open.mp server                | Official 1.5.8.3079                                  | Official 1.5.8.3079 dynssl                            |
| libpq                         | 17.11, repository mutex patch                        | 17.11, TLS and `-m32`                                 |
| PostgreSQL backend            | 17.11, retained EDB x64 tools                        | Ubuntu 16.15, isolated process; CI uses PostgreSQL 17 |
| SDK                           | `473b732b2de159773b2e2908fd11aa99f07db284`           | same                                                  |
| Pawn                          | 3.10.11 / `29f1a8c7fd2e01929807cd8c50353fbc93bbd651` | same                                                  |
| stdlib                        | `46a5d1d7ac8985e4104f745c462a247a0c2eb6a0`           | same                                                  |
| bcrypt                        | 0.4.1 x86, normal cost 12                            | same                                                  |

The container was Ubuntu 24.04, image
`sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac`.
An unprivileged user ran initdb, PostgreSQL and Linux tests. No user remote services
were used.

<a id="método-y-controles-de-las-pruebas"></a>

## Test method and controls

Fixtures include the original examples with renamed callbacks and wrap them to
record sessions and observe closure of every job. open.mp produces connection,
disconnection, command and dialog events from client traffic. The component produces
SQL callbacks; the external provider produces password callbacks.

Real PostgreSQL locks retain SQL. The `retain` variant does not forward `pg_cancel`
on disconnection: it represents accepted work finishing despite A leaving and permits
observation of a confirmed callback after B arrives. Default cancellation and closure
are tested separately. This variant is not evidence that default cancellation delivers
late: its callback can arrive before B in the observed run.

Hash/verify races use real cost-16 bcrypt to allow time for reuse. GMX uses cost 18 so
the provider finishes after the reload interval; the old callback is observed in the
new VM with another token. Timeout retains the example's actual 30 seconds: a fault
adapter discards the bcrypt callback after computation. This tests an absent response,
not a spontaneous internal provider hang.

Concurrent registration uses two servers and the same name: open.mp does not allow
two identically named players in one instance. Both complete empty lookups before
INSERT locks are released: one authenticates, the other receives `23505`, and exactly
one row remains. Concurrent verification requires both starts before the first
completion callback.

Custom uses its original session/request ownership logic and two lanes. The fixture
permits its local RCON gate and retains jobs on disconnection; both controls are
confined to the test gamemode. This does not qualify production authorization policy.
Save callbacks for requests 2 and 3, B's new token, no inherited account/score and
both closed jobs are observed.

PostgreSQL recovery stops and starts the real process. Readiness loss invalidates
authentication. When the pool reaches `PG_FAILED`, harness control explicitly closes
and recreates it as documented; recovered transport only starts a lookup and requests
another password proof.

Passwords are generated in memory and sent to client stdin. They are neither written
to files nor passed as process arguments. Server SQL/chat logs, SQL parameters in
errors and PostgreSQL statements are disabled. The harness checks server output for
generated passwords and bcrypt hashes. The evidence package is inspected again by
hash; it excludes the cluster, TLS keys and service profiles.

<a id="resultados"></a>

## Results

The CSV matrix contains one row per scenario/platform, with commit, method, result
and log path. JSON retains assertions, control SQL and nonsensitive results; logs
retain ordered events, tokens, handles and codes.

| Local execution                        | Test commit | Result                                                   |
| -------------------------------------- | ----------- | -------------------------------------------------------- |
| Linux x86, complete suite              | `28933d1`   | **19/19 PASS**, clean checkout                           |
| Windows x86, final complete suite      | `063191e`   | **19/19 PASS**, clean checkout                           |
| Linux, uncertain INSERT and sequence   | `063191e`   | **2/2 PASS**, fixture registration and confirmation loss |
| Windows, uncertain INSERT and sequence | `063191e`   | **2/2 PASS**, independent repetition                     |

These are **19 distinct scenarios per platform**, not 42 distinct scenarios.
Repetition alone does not expand coverage. The identity sequence advanced only once
after uncertain INSERT and did not advance during reconciliation: neither a second
row nor an additional INSERT attempt occurred. The matrix also retains preparation
FAIL and scope NOT RUN results.

Code CI at `44d5de9` confirmed 19 Linux x86 scenarios with PostgreSQL 17 backend and
package gates. The latest complete CI status is in the PR; a later commit is not
credited with an earlier SHA's result.

Local baseline regressions are also included: **21/21 CTest Windows x86**,
**22/22 CTest Linux x86** and **89 Python methods**. These suites are independent of
the 19 network scenarios per platform. The CTest count differs because of the Linux
descriptor-exhaustion test; local builds did not enable the optional benchmark or
ENOSPC tmpfs. CI retains its additional gates.

<a id="fallos-de-preparación-y-límites-conservados"></a>

## Preparation failures and retained limits

Earlier attempts were retained as development, not final qualification: missing
MSVCR120 in prebuilt LuaJIT, missing libwinpthread, legacy client parsing that changed
the nickname upon finding `-n` in a path, an incorrect dialog listitem, SQLSTATE parsed
as an integer, incorrect expectation that a `PG_FAILED` pool reconnects, provider
completion during the VM-free GMX interval and checking `NPC_Destroy` too early.
Clients, harness controls and expectations were corrected. These are not reproduced
component defects and did not motivate API or ABI changes.

Windows had three startup observations without readiness: two custom cases and
startup of the second concurrent-registration server. PostgreSQL logs for custom
startups showed TLS socket aborts (Winsock 10053). Their cause was not established
and is not attributed to the component. Failed logs were retained. Both custom cases
passed in a control cluster; concurrent registration, recovery and custom passed in
another follow-up, and the final complete run passed 19/19. This demonstrates observed
functioning without explaining or erasing earlier aborts. Before candidate freeze,
compare Windows CI and repeat on an isolated runner if the failure recurs.

Closing handles does not establish byte-level cleanup of external bcrypt/libpq heaps.
Completed-job invalidation and clean process exit are checked; historical sanitizers
and current CI have their own scope and do not instrument all official third-party binaries.

<a id="pendientes-y-recomendación"></a>

## Pending work and recommendation

The requested critical functional paths were observed with real network clients and
a real provider on both platforms. Evidence permits PR review and candidate selection;
**it is not stable-release approval** and does not authorize 24 hours. Freeze must
select a reviewed SHA with green CI and the Windows startup incident assessed and
accepted or resolved.

Outside this qualification: official graphical client and open.mp encryption;
spontaneous bcrypt internal hang (a discarded response was tested); third-party heap
instrumentation; bcrypt dispatcher interaction across multiple VMs with identical
callback names; production application permissions/exclusivity; uninstrumented
network/storage-fault combinations. Default cancellation receives no PASS for a
callback after B if it arrived before B: the confirmed race after B is the explicit
retain variant.

This phase contains no reproduced component implementation defect justifying API or
ABI changes. Changes are tests, preparation and documentation.

The 24-hour gate remains **BLOCKED** until remote isolated infrastructure is verified,
the candidate is formally frozen and the owner explicitly authorizes execution.
That campaign was neither started, scheduled nor dispatched. No tag was created,
release published, PR merged or direct master modification performed.

<a id="reproducción"></a>

## Reproduction

Start from the stated commit in a clean checkout. Build the x86 component and Pawn
following the x86 jobs in `.github/workflows/ci.yml`; use official server 1.5.8.3079
and verify the archive before extraction into a disposable directory. The script
downloads/verifies the provider, compiles fixtures and creates a new cluster per
execution. The output directory must be new.

Linux, with libpq32 and PostgreSQL tools on PATH:

```sh
python3 scripts/run_real_session_ci.py --output build-real-sessions \
  --component build-linux32/omp-postgresql.so --server /tmp/openmp/Server \
  --compiler build-pawn32/pawncc --stdlib build-stdlib \
  --pawn-include build-linux32/_deps/pawn-src/include \
  --runtime "$PWD/libpq32/lib/libpq.so.5"
```

Windows PowerShell, with explicit MSYS2 MINGW32 and PostgreSQL tools:

```powershell
python scripts/run_real_session_ci.py --output build-real-sessions `
  --component build-msvc32/Release/omp-postgresql.dll --server build-openmp/Server `
  --compiler build-pawn32/Release/pawncc.exe --stdlib build-stdlib `
  --pawn-include build-msvc32/_deps/pawn-src/include --postgres-bin build-server/pgsql/bin `
  --mingw-bin C:/msys64/mingw32/bin --bash C:/msys64/usr/bin/bash.exe `
  --runtime libpq32/bin/libpq.dll --runtime libpq32/bin/libssl-3.dll --runtime libpq32/bin/libcrypto-3.dll
```

External timeout is 600 seconds; event waits are bounded. Costs 16/18 are used only
for these races, not a load campaign. `--client` can reuse a client previously built
by `scripts/prepare_real_clients.py` with its manifest and hashes retained.

Downloaded-input SHA-256 values:

| Input                   | SHA-256                                                            |
| ----------------------- | ------------------------------------------------------------------ |
| open.mp Windows         | `d1b5ea1d09e7de0f144bf0055183e3ceaad5ff8a0a8e791021141e8973f84e03` |
| open.mp Linux dynssl    | `2be12550e3aaff0e60510ae8823e18b9759990eca3f02c101a49bcb3c985d222` |
| samp-bcrypt Windows     | `673e7511699a8e92452a323d4e35567e2c990c0779e1ef2d20b298940cd031c3` |
| samp-bcrypt Linux       | `ebf76b12d799046a605e3063bb7b64b08e1fad9bdb3c299b357ec215a0793b5d` |
| PostgreSQL 17.11 source | `5367f6fb2ec97efe1eb2e0c7926bb33438e51b0bd3a9733b88498056a7dc9a7e` |

Hashes of the actually executed component, AMX, client, provider, server and runtimes
are recorded in each `results.json`; hashes of earlier releases do not replace them.
