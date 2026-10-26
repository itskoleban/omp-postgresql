# Confirmed-result allocation and installed-package qualification

Base master: `7104e76b3a278b5a45c1f4df6088c2a2cbf4aba8`, merged PR #36.
No qualification PR was open. Its tree exactly matched PR #36 head
`e8f7b5cd89b471fd8b8e3548174553e84a5363de`, whose hosted matrix passed.
The new implementation, regressions and workflow are commit
`ea44502e8e3539b572e114d713831bb4e6098f49`, continued in PR #37 on
`qualify/v0.1.0-confirmed-result-retention`.

## Confirmed defect

A failed transaction's requested COMMIT is replaced by a physical ROLLBACK.
The worker reset its confirmation flags only after retaining that ROLLBACK's
result. If the result-vector insertion threw, the catch-all path incorrectly
marked the never-dispatched COMMIT item confirmed and incremented `completed`.
The overall job still reported FAILED / INTERNAL; this is an incorrect per-item
confirmation, not an observed successful COMMIT or replay.

The controlled original negative printed `completed=1 results=0 item0=1` and
failed the requested-command confirmation assertion. Reset confirmation before
retaining a successful fallback ROLLBACK result. The corrected case preserves
`completed=0 results=0 item0=2`, a failed requested COMMIT, and independent
database inspection confirms rollback. No uncertain SQL is replayed.

`restore-before.patch` restores only this defect while retaining the new test
seams. Apply it with `git apply --unidiff-zero` in a disposable checkout, rebuild
`pg_confirmed_allocation`, and execute `--only rejected_commit` against a disposable
`OMP_PG_TEST_SERVICE`. It is a negative control, never a release patch.

## Regression and packaging scope

Seven scenarios inject exactly one named exception in the actual Lane body:
ordinary write, streamed write, confirmed trailing-stream delivery failure,
second item of a three-item batch, confirmed COMMIT, confirmed ROLLBACK and
rejected COMMIT. Assertions cover one terminal delivery, exact fault counters,
confirmed item/prefix and unexecuted suffix, absent current payload, retained
earlier payload/stream block, independent database effects, physical command
counts, one connection recovery, fresh SQL and transaction reservation reuse.
These model failures at all three result-vector insertions; they do not claim
global allocation exhaustion. Sanitizer allocator interceptors remain intact.

The first independent Astra review of `c612e9f` identified post-confirmation
allocation coverage and installed Linux binary identity as evidence gaps. The
new full real-session CI stages qualify both extracted x86 distributions using
their installed component, complete packaged runtime inventory and Pawn include.
The preflight rejects stale source/platform identities, modified payloads,
additional files and symlinks. Results bind the manifest hash, component hash
and runtime hashes. Separate sanitized evidence preserves original/source
sessions and installed-package sessions. Five Python regressions exercise
those gates; they are not network executions.

## Local observations

Windows x64, GCC 16.1.0 / MinGW UCRT64, Debug, libpq/PostgreSQL 18.4:

- Core CMake build: PASS.
- Seven new disposable verified-TLS PostgreSQL scenarios: PASS.
- Selected existing unit/fuzz CTest cases: PASS 8/8, zero skips.
- Python baseline: PASS 143/143; updated suite: PASS 148/148, zero skips.
- English policy, documentation inventory and diff whitespace checks: PASS.
- Original isolated rejected-COMMIT regression: FAIL, retained unchanged.
- First test-harness attempt: FAIL after the query case because the new fixture
  bound parameter index 0 instead of 1; correct the harness, retain that attempt,
  and do not attribute its failure to the component.

`local-before-after.zip` retains exact raw console/build/test/server captures,
source snapshots and the local disposable-fixture driver. `local-manifest.json`
records member and archive SHA-256 identities. Localized Windows tool output
remains inside that historical raw archive. No database data, private TLS keys,
player credentials or production material is included. All three fixtures have
stopped servers and absent `postmaster.pid` files.

The previously recorded local full-suite PostgreSQL failures remain FAIL.
This targeted pass does not resolve or replace those observations, and no new
local full-component, Pawn, sanitizer or full real-session run is claimed.

## Exact-head qualification and continuation

The implementation push and initial PR workflow runs are `40330541563` and
`40330581541`. Their final statuses must be read from GitHub; a superseded or
cancelled run is not passing evidence. This evidence checkpoint is a descendant
of the implementation commit. Its own exact-head matrix, downloaded JUnit and
installed-package results are recorded in PR #37's qualification comments,
including run/attempt, head SHA, job/artifact identities and unresolved failures.
Never substitute an earlier green source SHA for that head.

After its required checks pass, perform an independent Astra delta review against
the exact qualified head, considering the earlier consolidated report and the
new confirmed defect. Persist the review in PR #37 with its exact SHA and scope;
until that happens this head is pending independent review.

Phases 1 and 3 receive a runtime correction, phase 5 gains allocation and package
identity regressions, and phases 4 and 6 gain full installed-binary sessions.
Issue #31 remains OPEN / ROOT CAUSE UNKNOWN. Preserve original network FAIL
`40164306733` and the unresolved historical Linux logging observation. No new
cause is inferred from this fix or later green runs.

The earlier preparation packet remains historical, including its then-unmerged
PR #36 status and `c612e9f` package identities. PR #36 is now merged. Select new
candidate/dependency bytes from the exact qualified PR #37 head for any future
campaign; do not silently reuse the older hashes. The isolated
`pg-audit-isolated` runner is still unverified: the connected GitHub tool rejects
the runner-inventory endpoint. No runner is provisioned by this checkpoint.

Next campaign gates: owner integration disposition, verified disposable non-root
infrastructure, exact deployed binary/dependency and fixture freeze, bounded
preflight, measured resource/queue/result/latency thresholds and stop/capture
criteria, then express owner authorization. The 24-hour campaign has not started.
Its complete results need a second independent Astra review; stable v0.1.0 still
requires final owner approval and a justified issue #31 risk disposition.
