# PR #27 — final technical follow-up

Date: October 24, 2026 (America/Bogota). Scope is exclusively
`codex/real-player-qualification` → `master`, PR #27. Initial remote/local head was
`101f540520657273f5bc987b0ae834a5bc45000f`, with no later commits or local edits.
[CI 40098277568](https://github.com/itskoleban/omp-postgresql/actions/runs/40098277568)
finished seven jobs successfully at that exact head. Those results remain historical;
the follow-up requires its own full CI, including both x86 real-session jobs.

## Privacy and evidence integrity

All 142 files added/modified by the initial PR were inspected. Unnecessary paths
occurred in 57 evidence files; the two JUnit files also contained machine/container
hostnames, bringing the number of transformed files to 58. The remaining 69
registered files remain byte-identical. No actual credentials, password inputs,
encoded bcrypt values, private keys or private certificate payloads were found in
these checked-in captures. Fictional usernames, SQLSTATEs, job/session generation
tokens and binary/source SHA-256 values remain visible because they are required
to assess the tests.

Original bytes were verified before transformation. The exact original ledger is
preserved separately; the current ledger gives both original and sanitized hashes
and sizes, labels each representation, and records its rules and observation hash.
See [the evidence contract](real-session-evidence/README.md). A sanitized result is
not presented as original raw bytes. Earlier history is not purged or rewritten.

The automated verifier checks original-ledger identity, current hashes, inventory,
observation fingerprints, JSON/QA/JUnit outcome preservation, personal-path absence
and private-key/credential/bcrypt patterns. Unit tests cover escaped Windows and
CMD-caret paths, valid XML, stable locators, original/hash mismatches and unchanged
historical states. This is a targeted privacy check, not proof that every arbitrary
future secret format can be recognized.

## Windows startup observations

The three historical failures remain FAIL in the original matrix and retain their
original byte identities. Their sanitized views show:

| Capture                                                    | Observed boundary                                                                                                                        | Comparison with successful final runs                                                                                                |
| ---------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `development-failures/final-windows-custom-load.txt`       | Components/provider loaded; last database event is state 1, readiness 0. No player or application SQL events follow.                     | Final `windows-complete-063191e/custom-load` reaches readiness 1, observes A/B on slot 0, rejects the stale load and closes its job. |
| `development-failures/final-windows-custom-save.txt`       | Startup did not produce the required readiness event before the harness deadline.                                                        | Final custom-save observes both old requests (2 and 3) after B connects; B keeps no account/score and both jobs close.               |
| `development-failures/qualified-windows-duplicate-two.txt` | The second server reaches database state 1/readiness 0 and then exits during cleanup. The concurrent registration phase was not reached. | Final duplicate run reaches both servers, submits both registrations and observes one winner, a 23505 loser and one database row.    |

The retained `windows-tls-postgres.txt` contains five Winsock 10053 messages: one
receive abort and four SSL-accept aborts. Four SSL messages occur at 08:03:04.271,
08:03:04.598, 08:03:27.929 and 08:03:28.039 local time, compatible with the two
custom startup windows. They show that PostgreSQL saw a socket abort during TLS;
they do not identify the code that caused it. The first receive abort at 08:02:18.306
must not be counted as a custom TLS handshake failure. There is no paired PostgreSQL
trace here that proves a TLS cause for the separate duplicate-server failure.

**Classification: no confirmed root cause; not reproduced in the final complete
local runs or the subsequently passing CI. Not marked fixed.** The evidence is
consistent with a transport/startup boundary problem, including a transient Windows
environment, scheduling/deadline, PostgreSQL/TLS or client-side connection failure.
There is insufficient data to choose among them or blame the component. Native TLS
error stacks, lane-level connection diagnostics and host scheduling/security traces
were not captured at the failing instant. A passing retry is not a causal diagnosis.

No C++ implementation change, new retry, increased timeout, weakened assertion or
authentication-rule change is proposed. The existing 15-second observed-event
deadline remains intact. If the issue recurs, retain the failing run's startup,
connection/lane and PostgreSQL diagnostics on an isolated Windows runner before
changing behavior. A demonstrated component defect would need a separate proposal
for review within the user's scope decision, not an unreviewed C++ patch here.

The older CI recovery-oracle failure is a different, already demonstrated harness
ordering issue: a fast PostgreSQL restart won the race with pool exhaustion. It was
corrected in `44d5de9` by keeping PostgreSQL down until the failed state is observed.
Its historical FAIL and subsequent passes remain unchanged.

## What source_dirty means

Previously the flag was only `bool(git status --porcelain)`. That combines tracked
edits with non-ignored untracked files. The workflow creates `libpq32/`, extracted
`postgresql-17.11/` and downloaded PostgreSQL/open.mp archives inside the checkout.
These roots are not covered by the existing `/build*/` ignore. Ignored build
directories themselves do not make that old flag true.

A disposable Git regression reproduces `source_dirty=true` by creating those
generated roots without modifying a tracked byte. Other regressions retain staged,
unstaged, deleted and renamed tracked paths, and leave unknown untracked files
visible/unclassified. The historical manifests did not retain the status inventory,
so the exact entries at those historical instants cannot be reconstructed from the
boolean alone; they are not retroactively declared clean.

New snapshots keep:

- exact `source_commit` and `head_tree`;
- the original aggregate `source_dirty` meaning;
- `tracked_source_modified`, tracked status/path/rename records, and a tracked-patch hash;
- each non-ignored untracked entry, its count and conservative classification;
- `source_state`: clean, generated-files-only, untracked-unclassified or tracked-modified;
- the status-byte hash, and the unchanged SHA-256 identities of executed inputs.

Known generated roots are an explicit classification tied to workflow steps, not
an ignore rule or a claim that their content is trustworthy. No entry is omitted
to force a false clean flag. A CI run with tracked source edits now fails the
session gate **after preserving the sanitized evidence**. New real x86 CI captures
will establish the actual status inventory for their own SHA; no new result is
attributed to the old head. Provenance is captured once before work begins and
cannot be rebound by a later edit.

## Coverage, gate and decision boundary

All 19 real-session scenarios, fixture controls and assertions are preserved,
including the unique identity-sequence advance after an uncertain INSERT. Server,
component, provider/client pins, x86 matrix and existing sanitizer/package gates
remain unchanged. Publication changes only which representation of a capture is
uploaded, not whether a test failure fails the job. Original raw files are retained
locally on the runner; current public copies have both hashes.

Ordinary SA-MP-compatible external clients are still distinct from NPC and from
the untested rendered/encrypted official-client path. Retain-work, real slower
bcrypt, discarded provider response and fixture-only custom RCON gate retain their
documented limits. Multi-VM provider dispatch and byte-level third-party heap proof
remain outside the established coverage.

**The 24-hour gate is BLOCKED** until remote isolated infrastructure is verified,
the candidate is formally frozen, and the owner explicitly authorizes the run.
The old NOT RUN execution row is preserved; a separate current gate row records
this restriction. No campaign, merge, tag, branch deletion or release is performed.

Merge readiness depends on the new SHA's full CI and the integrity/privacy checks.
The PR can be ready to merge while the documented intermittent Windows incident
remains a known limitation with no demonstrated component fault; that is not a
stable-release or 24-hour qualification decision.
