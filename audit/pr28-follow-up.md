# PR #28 evidence and allocation follow-up

Base reviewed before this follow-up: `510f17d8e79293393d68a6607882caf231f69646`.
The remote work branch matched that SHA and master matched
`2f366003b866461b80432fdfb075e072dc026551`; no later work was overwritten.
All changes remain on `fix/v0.1.0-pending-qualification`, within PR #28.

## Timeline publication

`scripts/real_session_evidence.py` now selects `timeline.jsonl`. Its ordered JSONL
records are part of the observation fingerprint, not merely a byte inventory.
The original and published byte hashes/sizes are both recorded. Valid timelines
remain byte-identical, including line endings, event order, UTC and monotonic
clocks, process identifiers, states and outcomes.

The timeline is deliberately payload-free. The publisher validates known scalar
fields, requires UTC/event/monotonic identity, permits only numeric generation/job
tokens, and rejects unknown fields, paths in labels, duplicate keys and private
credential markers. It does not rewrite event values to conceal a failure. The
producer also checks its actual generated passwords before writing, records suite
startup before initial SQL setup, and records a numeric PostgreSQL failure exit
without exposing SQL or error payloads. Malformed or unsafe records fail publication;
their source bytes are never edited. No historical timeline was manufactured or
added to an old inventory.

Five focused publication regressions cover success, scenario failure, readiness
failure, TLS failure and unexpected process exit. These are synthetic unit fixtures,
not network qualification passes. They exercise the actual scenario/wait/close
methods, check ordered records and preserved FAIL outcomes, and confirm that the
CI runner's finally block publishes available evidence while rethrowing its original
nonzero exit. Other cases reject modified clocks/outcomes/order, rehashed observation
tampering, credentials and duplicate fields. Pawn VM errors at exit zero remain rejected.

## Independent TSan scenario requirements

`tests/worker_allocation.cpp` takes counter snapshots around each run and requires
the run's own delta to contain at least one `interrupted_failure`, two
`terminal_message` and three total exceptions. Each run prints its own counters.
Earlier injections and excess injections at another site cannot satisfy that delta.
The active job retains uncertain status/item metadata, zero completed SQL and the
structured internal error. Existing single-delivery, queued no-replay, explicit
recovery and worker-shutdown assertions remain intact.

Three additional CTest regressions check counter isolation and deliberately omit
the recovery or shutdown injections. Each omission must produce its own missing-
checkpoint assertion with exit 1 and zero scenario deltas. An arbitrary crash,
missing service, timeout or another failure is not accepted as the negative control.
Global new/delete replacement remains disabled under TSan; checkpoint exceptions
are not described as global memory exhaustion. The intentional-race detector
control and all production code/interfaces remain unchanged.

The [focused execution](pending-qualification-evidence/tsan-68c4da8/execution.json)
used exact source **`68c4da874ef51f548e761b29dcefdac6d23f3afb`** in a disposable Ubuntu
container with its external network disconnected and no published ports. Clang
19.1.1 TSan and loopback PostgreSQL/libpq 16 were used locally; complete CI uses
the repository's PostgreSQL 17 environment. All five focused controls passed in
1.91 seconds. The [full log](pending-qualification-evidence/tsan-68c4da8/LastTest.log)
records recovery and shutdown independently at **1 / 2 / 3** injections, both
correctly rejected zero-injection variants, and the real TSan race diagnostic/exit 66.
Commands, source hashes, JUnit and separate original/public identities are retained.

## Verifiable GCC/Clang EMFILE evidence

The [baseline publication](pending-qualification-evidence/emfile-baseline-2f36600/manifest.json)
retains its original source **`2f366003b866461b80432fdfb075e072dc026551`**. All four raw
log hashes and both recorded C++ source hashes were checked against the preserved
originals and Git blobs before publication. The old manifest had no run ID,
execution UTC or factory argv; those fields are not retroactively invented.

The [fresh run](pending-qualification-evidence/emfile-68c4da8/manifest.json) belongs
only to **`68c4da874ef51f548e761b29dcefdac6d23f3afb`**, not to later evidence/documentation
commits. Its original run ID and UTC bounds, source/provenance inventory, compiler
version output, factory/driver/run commands, full flags, sanitizer options, binary/
object identities and complete merged-output diagnostics are retained. The checkout
record transparently includes the then-untracked baseline publication; tracked code
was unchanged. Relevant input hashes were captured before and checked after execution.

| Compiler/runtime | Uninstrumented | ASan/UBSan | Actual EMFILE |
| --- | --- | --- | --- |
| GCC 13.3.0 | PASS, exit 0 | FAIL, exit 1 | Observed in both |
| Clang 19.1.1 | PASS, exit 0 | FAIL, exit 1 | Observed in both |

The controls contain a valid external polymorphic object and no component code or
database. **ENVIRONMENT LIMITATION CONFIRMED** remains the classification for the
tested runtimes. No vptr check or interceptor was disabled and no suppression was
introduced. The component plus ASan/UBSan plus total FD exhaustion remains **NOT RUN**.
These controls do not certify that component combination or reinterpret historical FAILs.

`scripts/probe_sanitizer_descriptors.py` now records complete provenance, saves
negative diagnostics with nonzero outcomes, verifies integrity separately from
execution success, and publishes explicitly sanitized copies. A zero exit without
observed EMFILE/completion, or with sanitizer diagnostics, is not accepted as PASS.
The manual descriptor workflow uses this same path and uploads the public manifest,
logs and both hash inventories even after its execution step fails. Its diagnostic
failure remains a failed job; no extended campaign was dispatched for this follow-up.

Reproduction and verification:

```sh
python scripts/probe_sanitizer_descriptors.py --compiler g++ --compiler clang++-19 --output build-descriptors
# The known instrumented failures make the command above return 1.
python scripts/probe_sanitizer_descriptors.py --verify build-descriptors
python scripts/probe_sanitizer_descriptors.py --publish build-descriptors --destination build-descriptors-public
python scripts/probe_sanitizer_descriptors.py --verify audit/pending-qualification-evidence/emfile-baseline-2f36600
python scripts/probe_sanitizer_descriptors.py --verify audit/pending-qualification-evidence/emfile-68c4da8
python scripts/real_session_evidence.py verify audit/pending-qualification-evidence/tsan-68c4da8
```

An integrity-command exit zero means the recorded identities/outcomes verify; it
does not convert the instrumented execution FAIL into a sanitizer PASS. The ledgers
retain original digests separately from public-copy digests. Cluster directories,
TLS private keys, service profiles, passwords and executables are excluded.

## Combined validation and remaining gates

Focused Python, publication, descriptor-integrity, Pawn VM-error and English-policy
regressions pass. The English rules and all 173 frozen exceptions are unchanged;
the existing 127 session identities and 26 other historical blobs still verify.
The final PR description and owner report record the final HEAD's complete seven-job
CI, the final Python count, both 19-scenario network suites and artifact identities.
Those final results must not be inferred from earlier CI or these local controls.

Historical Windows/TLS incidents remain **NOT REPRODUCED / ROOT CAUSE UNKNOWN**;
passing current runs do not resolve Winsock 10053. TSan site faults remain a
**WORKAROUND VALIDATED** with narrower scope than global OOM. The VM buffer fix,
VM-error guard, real bcrypt/playerid/callback/registration/recovery/uncertainty tests
and Linux/Windows package verification are preserved. No public API, ABI, Pawn
interface, production implementation, deadline or retry policy changed in this follow-up.

**24-hour campaign: BLOCKED** pending verified isolated infrastructure, formal
candidate freeze and explicit owner authorization. No merge, master commit,
additional PR, branch deletion, tag, release or RC.1 asset modification is performed.
