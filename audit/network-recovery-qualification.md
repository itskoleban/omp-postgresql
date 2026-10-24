# Phase 2: network, TLS and recovery qualification

Initial remote inspection on October 24, 2026: master was
`95340017919b85d4c35ebe02ef25a63b6382922d`, the only remote branch. PRs #1 through
#28 were closed and merged. Work is confined to
`test/v0.1.0-network-recovery-qualification` and its new draft PR. No stable-release
qualification is implied by a successful PR. The 24-hour campaign remains **BLOCKED**
until the other phases, verified isolated remote infrastructure, a formal candidate
freeze and explicit owner authorization are complete.

## Investigation boundaries

The PR #28 follow-up, real-session qualification/review, allocation report, stability
runbook, validation documentation, English policy, connection stalls and CI were
reviewed before extending coverage. Existing fixes and preserved captures are not
rewritten. No production source, Pawn API, ABI, public state, cancellation policy,
timeout, retry count or sanitizer setting is changed.

The historical custom-load, custom-save and second concurrent-registration server
failures remain **NOT REPRODUCED / ROOT CAUSE UNKNOWN**. Their original FAILs and
hash ledgers remain intact. The five Winsock 10053 observations include one receive
abort and four SSL-accept aborts; the separate registration startup lacks a paired
PostgreSQL TLS trace. None identifies the component as the cause. Current passes
cannot establish a retrospective root cause.

The observable path is open.mp process launch, Engine pool/lane creation,
`PQconnectStartParams`, TCP establishment, PostgreSQL SSLRequest, TLS handshake,
startup/authentication, `PQconnectPoll` completion, lane readiness and application
readiness. Existing test-only lane sampling, payload-free UTC/monotonic timelines,
PostgreSQL connection/disconnection logs and process identities cover these boundaries.
The new wire log adds backend PID and fault boundaries without startup, SQL, bind,
password, key or TLS payloads. Native Windows scheduling/security traces were absent
from the historical captures and are not reconstructed.

## New reproducible scenarios

`network_recovery_integration` uses the actual Engine and libpq against a newly
initialized loopback-only TLS PostgreSQL cluster. The thirteen named scenarios below
have a 10-second observation deadline, 1.5-second query deadline and a 90-second
CTest supervisor. Every proxy is one-shot except the fragmentation relay. Recovery
may create a physical session; old application transactions require explicit finish
and a fresh BEGIN. No original uncertain job is submitted again.

| ID | Introduced fault and independent oracle | Required assertions |
| --- | --- | --- |
| begin | Drop BEGIN CommandComplete | UNCERTAIN, connection cause, item/context identity, old backend closed, explicit new transaction |
| savepoint | Drop SAVEPOINT confirmation after an INSERT | UNCERTAIN; old transaction rejected before dispatch; zero rows and one identity attempt |
| rollback_to | Drop ROLLBACK TO SAVEPOINT confirmation | Same transaction/session separation and persisted rollback oracle |
| release | Drop RELEASE SAVEPOINT confirmation | Same transaction/session separation and persisted rollback oracle |
| commit | Drop executed COMMIT confirmation | UNCERTAIN despite exactly one persistent row and one identity attempt |
| rollback | Drop executed ROLLBACK confirmation | UNCERTAIN despite zero persistent rows and one identity attempt |
| insert | Drop autocommit INSERT confirmation | One persistent row; identity sequence detects attempted duplicate execution |
| hold | Keep socket open and withhold INSERT response | Timeout cause, UNCERTAIN, one committed row, subsequent explicit SQL succeeds |
| partial_parse | Forward all but the final byte of a marked Parse frame and close | Conservative UNCERTAIN; zero rows; sequence is_called=false proves no INSERT attempt |
| batch | Drop second item's INSERT confirmation | Confirmed prefix, uncertain current item, NOT_EXECUTED suffix; one row and one identity attempt |
| fragment | Send frontend/backend frames in seven-byte application writes | Exact large result, successful BEGIN/savepoint/rollback-to/release/COMMIT |
| tls_hold | Withhold opaque server TLS bytes after observing exact PID/SQL/PgSleep in PostgreSQL | TLS verified independently; timeout/UNCERTAIN; queued write fails NOT_EXECUTED; sequence unchanged; explicit new work |
| tls_shutdown | Same opaque TLS hold, then destroy Engine with a queued write | Bounded destruction; dispatched uncertain/cancelled cause versus queued CANCELLED; original backend gone; no queued write |

Terminal delivery counts must be exactly one for each notifying job while its Engine
is polled. Job/result ownership is checked using invalidated handles and weak-pointer
expiration. Destruction settles accepted jobs without delivering callbacks to a dead
Engine. The existing full cluster restart test now observes the exact query and PID
in `pg_stat_activity`, replacing its weaker RUNNING-plus-sleep precondition.

Two additional real-session cases retain all nineteen original scenarios:

- `startup_exit_overlap`: three rounds, six official open.mp x86 processes; launch
  the second after requesting the first's exit, require real TLS readiness and exit 0.
- `postgres_recovery_playerid_reuse`: observe A's database lock wait, connect B on
  the same playerid before stopping PostgreSQL, retain the old completion, verify
  its disposal without transferring A's identity/score, exhaust recovery, start a
  second official server during the outage and explicitly recreate both failed pools.

These clients use the pinned external ordinary compatible protocol and real bcrypt
provider. They are not the official graphical client, NPCs or synthetic callbacks.

## Coverage before and after

| Boundary | Baseline | Added qualification |
| --- | --- | --- |
| TCP/SSLRequest/TLS/startup stalls | Three protocol stall phases, timeout/retry/disconnect/unload/destruction | Preserved; repeated real server overlap and startup during outage |
| BEGIN and savepoint response ambiguity | Normal transactions/savepoint and cancellation coverage | Four explicit lost transaction-command responses |
| COMMIT/ROLLBACK response ambiguity | Existing real-server loss/deferred COMMIT termination | One-shot loss with identity-attempt oracle and explicit logical recovery |
| Partial transport / withheld replies | Fragmented fixture controls and stalls | Real Engine partial Parse, seven-byte framed relay, committed response hold and opaque TLS hold |
| Batch interruption | Existing cancellation/resource/lifecycle tests | Confirmed/uncertain/unsent item vector and persistent/sequence oracle |
| Database restart | Real restart and failed-pool recreation | Exact PID/SQL precondition and recovery with real playerid reuse |
| Sessions | Nineteen cases per x86 platform | All nineteen retained plus two new cases |
| Physical partition / OS short writes | NOT RUN | NOT RUN; proxy writes do not prove OS syscall short-write coverage |
| Official graphical/encrypted client | NOT RUN | NOT RUN |
| ASan/UBSan plus total descriptors | NOT RUN; external-runtime limitation confirmed | Unchanged; not reopened |
| 24-hour campaign | BLOCKED | BLOCKED; never dispatched |

## Commands and evidence contract

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPG_STRESS_SECONDS=0
cmake --build build --parallel 4
python tests/run_postgres.py --build build --network-only
python tests/run_postgres.py --build build --doc-sql
python -m unittest discover -s tests -p 'test_*.py' -v
python scripts/check_english.py
```

Use the existing CI x86, ASan/UBSan and TSan configurations and pinned dependencies.
On Windows, use the patched x86 libpq, matching runtime DLLs and
`--postgres-bin <disposable-server-tools>`. Full real-session commands remain in
`ci.yml`/`scripts/run_real_session_ci.py`; they now include both added cases.
`--network-only` is explicitly focused and never counts as a full CTest pass.
Skipped, failed or missing cases remain rejected for either inventory.

Every executed result must identify its exact source SHA (and any dirty patch),
OS/architecture, PostgreSQL/libpq/open.mp versions, command, fault, assertions, SQL
observations, diagnostics and cleanup. `LastTest.log`, JUnit and `wire-*.log` are
retained by the existing seven-job CI artifact paths even on failure. Real-session
publication retains separate original/public hashes and ordered timeline identities.
Do not publish cluster data, service profiles, passwords, private keys or personal paths.

Development negatives include incorrect new test expectations for a stale transaction's
NOT_EXECUTED item and a queued job's connection error. They are fixture-oracle errors,
not demonstrated component defects. Their failed logs must remain available. Early
uncommitted iterations have a baseline SHA but incomplete immutable input provenance;
they must not be presented as exact-commit component regressions.

## Qualification status

The draft is in progress. Final source-specific local, CI, artifact identities,
negative evidence, residual risk and verdict are recorded after execution. No green
result is inherited from PR #28 or from a previous source version.

For the next phase, retain the current sanitizer/descriptor limitations, inject bounded
allocations and storage failures independently, and carry uncertain/no-replay and
session-generation oracles into those tests. Defer exhaustive fuzzing, prolonged
20/10/10-minute campaigns and the 24-hour campaign until separately authorized or
justified by a new result. A network pass alone does not qualify those boundaries.
