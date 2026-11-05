# Candidate-specific residual-risk disposition: issue #31

Recorded 2026-11-04, America/Bogota. Owner authority: the explicit instruction in
the current preparation request, accepting consideration of this known historical
risk under verifiable conditions. This record does not expand that authority to
starting the final campaign, merging changes, publishing, or production operation.

## Observation and scope

[Issue #31](https://github.com/itskoleban/omp-postgresql/issues/31) was freshly read
through the connected GitHub API. It is OPEN, with no comments or later updates
than its 2026-10-25 disposition for development integration. Its original body is
retained in `issue31-original-snapshot.json`; that historical decision is preserved.

The original [run 40164306733](https://github.com/itskoleban/omp-postgresql/actions/runs/40164306733),
job 121082357048, source `7ca0d1df1e970e622fad09240bd099b939a563c8`, remains FAIL.
The ASan/UBSan network test timed out confirming `DROP TABLE phase2_hold` after the
intended INSERT fault, recovery, independent database checks and a fresh confirmed
BEGIN/ROLLBACK. It returned uncertain/error6/completed0. **ROOT CAUSE UNKNOWN**.
No causal attribution to PostgreSQL, libpq, the component, fixture, sanitizers or
runner has been demonstrated. It is not corrected, resolved, or explained by passes.

## Provisional disposition for this candidate

Conditionally accept the residual availability/recovery uncertainty for preparation
of candidate `3a5df8439881bd514727a2c5e360f9ec0bafaf43`, tree
`21cea7c3b4620c361484be7aa76ad8f73132c545`. This allows preparation to continue;
it is not a campaign PASS or stable release acceptance. The owner must review the
final frozen candidate and this disposition again if the Engine correction changes
its source identity. Acceptance does not automatically transfer to that new SHA.

Bounded supporting evidence is the preserved PR #30 run 40200045946 and later
exact-source [CI 41579975396](https://github.com/itskoleban/omp-postgresql/actions/runs/41579975396)
(7/7 jobs), including ASan network diagnostics, plus the recorded bounded
network/transaction interruption cases. They demonstrate those executions only.
The successful 34/34 preflight and finite calibration records do not prove 24-hour
stability. Dependency internals, every possible schedule, power interruption,
physical network/storage failure and unrelated host configurations remain excluded.

No new issue comment, issue update, failed exact-source CI job or new recurrence was
found in the evidence consulted. The separately confirmed transaction-ID exhaustion
defect blocks overall candidate readiness but establishes no causal link to #31.
Unknown recurrence probability remains unquantified; no numerical reliability claim
is supportable. The previous and current physical capacity failures are host gates,
not evidence explaining the original cleanup timeout.

## Mandatory suspension and operational measures

- Any unexplained repeat of hold cleanup timeout, missing cleanup confirmation,
  unexpected UNCERTAIN terminal, lost/duplicate terminal, semantic/transaction
  violation, sanitizer diagnostic, incomplete fixture shutdown or evidence loss
  immediately suspends qualification. Mark FAIL/ABORTED as appropriate and BLOCKED
  for restart; never rerun silently to obtain a pass.
- Preserve raw client/server/proxy traces, activity/lock snapshots, command and
  backend/process identities, timestamps, resources, dependency/build identities,
  supervisor decisions, hashes and negative artifacts before scoped teardown.
- Investigate a recurrence with contemporaneous evidence; require a new technical
  disposition and affected checks before another attempt. A passing rerun alone
  cannot clear the recurrence or establish its cause.
- Maintain CONFIRMED/FAILED/UNCERTAIN/CANCELLED contracts, SQL confirmation checks,
  deadlines and prohibition of automatic replay of uncertain SQL. Reconcile uncertain
  application effects against the database before any explicit operator retry.
- Use only isolated disposable loopback fixtures and frozen inputs for qualification;
  keep operational access for stop/recovery and retain failed evidence independently.
  Do not transfer this acceptance to application SQL or production credentials/data.

The issue stays OPEN. Acceptance is a decision about a retained risk, not a bug fix.
No issue closure, release, stable tag or campaign authorization follows from this file.
