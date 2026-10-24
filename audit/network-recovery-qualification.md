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

- `startup_exit_overlap`: three rounds, six official open.mp x86 processes; verify
  both processes are live, request the first's exit while the second starts, then
  require the second's real TLS readiness and both clean exits.
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

Local source `699e84a8e6f7415425453056375f18eed42b82d6` passed all thirteen wire
scenarios (8.04 seconds), 22 configured Windows x86 CTest cases (90.98 seconds) and
the documented SQL checks. PostgreSQL/libpq 17.11, MSVC 19.51.36248 and an x64 Windows
NT 10.0.26300 host were used. The executable inputs remained unchanged while the
documentation-only a96efdf correction was committed during the complete CTest run.
The original Python run at 699e84a failed the existing packaged-document link check;
source `a96efdf475908c511296273580f0f5e1b62766e6` passed all 126 Python methods.
See the [capture manifest](network-recovery-evidence/execution.json) and its explicit
original/public hashes. Each run retains its own source and scope.

The complete Windows real-session run at a96efdf passed the original nineteen cases
and the initial three-round startup case, then **FAIL**ed the new recovery-reuse case
before introducing the database outage. The new observer incorrectly searched for
`application_name='qa_game'`; `Lane::connect` deliberately supplies `omp-postgresql`
and overrides that service-profile value. The query remained lock-blocked while
the observer found zero matches. This is a test-fixture selection error, not a
transport or component defect. The [complete negative public inventory](network-recovery-evidence/real-a96efdf-negative/sha256.json)
retains the failed result, process logs, timeline, SQL observations and original hashes.
The correction selects the disposable role plus the actual application name and
requires the exact query, PID and active lock wait. No timeout is increased. The
startup case is also strengthened to observe two live processes before requesting exit.

Final source-specific CI, both x86 packages, corrected real sessions and the verdict
are reported in [PR #29](https://github.com/itskoleban/omp-postgresql/pull/29).
No green result is inherited from PR #28, a cancelled run or an earlier source.

### Independent TSan control failure at f8fbb3f

[CI 40132439602](https://github.com/itskoleban/omp-postgresql/actions/runs/40132439602)
at `f8fbb3fae6e08b6931a671d63b89889be3b579f9` completed six of seven jobs successfully.
Both x86 platforms passed all **21/21** real-session cases, including the corrected
observer, actual simultaneous live processes, startup during a PostgreSQL outage and
explicit recreation of both failed pools. Both packages were built, installed in fresh
servers, downloaded and independently verified by the seventh job. These are finite
qualification results, not stable-release approval.

The THREAD job [120986936596](https://github.com/itskoleban/omp-postgresql/actions/runs/40132439602/job/120986936596)
remains **FAIL**: 28/29 CTest cases passed, including the new wire suite (9.52 seconds),
but `tsan_detector_control` did not satisfy its required race diagnostic/exit 66.
The old wrapper did not print the child's exit code when both streams were empty.
Its original result cannot distinguish successful execution without detection from
an early signal/runtime failure. **ROOT CAUSE UNKNOWN** at this revision; it is not
attributed to the component, network tests, ASLR or compiler optimization without proof.
The failed artifact identity is
`a5600785b7647c7ab0d0edde4f099f2738febe8a0a8dac4264fd3345f4145498`;
the ZIP was independently downloaded and its digest matched. The
[negative log and x86 result excerpts](network-recovery-evidence/ci-f8fbb3f/execution.json)
retain original/public identities. The separate
[focused Windows run](network-recovery-evidence/real-focused-f8fbb3f/results.json)
passed registration and both corrected new cases at the same source.

The diagnostic extension preserves the genuine race source and its compiler flags.
It records executable hashes, every child return code, output lengths and complete
streams, and the Linux mmap entropy setting. Twenty independent race executions and
twenty independent instrumented startup-only executions must all satisfy their own
oracle. This is a bounded diagnostic/control series, not retry-until-success: any
failure fails the existing CTest gate. The 30-second CTest supervisor and 15-second
child timeout are unchanged. Two synthetic oracle regressions reject crashes,
missing markers, wrong exits and false race confirmation; they do not qualify TSan.
No suppression, uninstrumented component path, ASLR change or unsupported non-PIE
workaround is introduced. See the [upstream TSan limitations](https://clang.llvm.org/docs/ThreadSanitizer.html#limitations).

For the next phase, retain the current sanitizer/descriptor limitations, inject bounded
allocations and storage failures independently, and carry uncertain/no-replay and
session-generation oracles into those tests. Defer exhaustive fuzzing, prolonged
20/10/10-minute campaigns and the 24-hour campaign until separately authorized or
justified by a new result. A network pass alone does not qualify those boundaries.
