# Consolidated independent audit handoff

Status: **COMPLETED**. The exact candidate completed the seven-job gate in run
`40255510980` before independent `gpt-6-astra` review was actually invoked.
Following a usage-limit interruption, the same review resumed and completed on
October 26. [The final report](astra-first-review.md), including its dependency
addendum, finds no new confirmed blocker and permits campaign preparation within
the stated limits. It does not authorize a campaign, merge or stable publication.

## Candidate identity and dependencies

Runtime and regression candidate:
`c612e9f1ed258d081d91fd929c5333bf77b78d28` on PR #36,
`qualify/v0.1.0-resource-lifecycle`, base
`e371ccb3a0dd01cca5dc92b197250de7f55226f2`.
The checkpoint's `manifest.json` hashes all changed runtime/build/test sources.
Evidence-only descendants must not be silently substituted for the audited SHA.
No merge, tag, release or stable qualification is requested by this handoff.

The existing workflow pins open.mp SDK
`473b732b2de159773b2e2908fd11aa99f07db284`, Pawn
`29f1a8c7fd2e01929807cd8c50353fbc93bbd651`, and omp-stdlib
`46a5d1d7ac8985e4104f745c462a247a0c2eb6a0`.
Inspect workflow/build manifests for each platform's exact libpq/server/provider
identity; do not generalize one dependency build to another.

## Finite qualification scope

| Phase                   | Existing implementation and regressions                                                                                                                                                 | Candidate evidence                                                                                   |
| ----------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| Memory/resources        | PR #30 allocation, terminal queue, callbacks and worker handling; PR #32 handle/token limits; PR #35 batch admission; PR #36 allocation-free owner unload and finish-marker reclamation | PASS in normal, ASan/UBSan, TSan and Windows configurations; TSan retains its allocator interceptors |
| Files/storage           | PR #32 bounded SQL reading including SIZE_MAX and no partial dispatch; PR #33 interrupted stream, permission and descriptor tests; logging short writes and ENOSPC                      | Configured exact-source cases PASS; local and historical negatives preserved                         |
| Connections/concurrency | TLS/auth, cancellation, timeout, recovery, transactions, shutdown and 13 wire-fault scenarios; uncertain SQL never replays                                                              | New retirement and configured integrations PASS; bounded-schedule limits remain                      |
| open.mp                 | Pawn callbacks, AMX reload, authentication/registration, stale playerid responses, bcrypt and reconnect; 23 current real-session scenarios per x86 platform                             | PASS 23/23 per platform; installed-package smoke tests qualify their separate binaries               |
| Automated/fuzz          | Parameter/type conversions, SQL sections, lifetime tests, PR #34 numeric/byte fuzz, PR #35 randomized admission recovery; 143 Python tests                                              | Configured hosted suites PASS, with no configured skips                                              |
| Multiplatform/package   | Six build/test configurations plus downloaded-distribution verification                                                                                                                 | PASS 7/7 jobs, 194/194 CTest executions, directly verified artifact identities                       |

Earlier reports can contain superseded PENDING rows. Use the continuous
checkpoints, actual workflow inventories and final exact-SHA artifacts together;
do not edit old manifests to make them appear current.

## Current fixes and proof

Read `README.md`, the before/after regression captures and the source diff.
Owner unload formerly allocated before any teardown, so bad_alloc escaped and
could leave a script registry entry intact. It now marks each existing job with
main-thread-only retirement state. Test normal/ASan modes deny global main-thread
allocation during unload; TSan modes preserve runtime interceptors and explicitly
make a narrower lifecycle claim.

Finish-marker cleanup formerly depended on a surviving connection and registered
job. COMMIT/ROLLBACK entries now retire independently, including status-visible
before terminal-queue publication. Six controlled actual-poll snapshots fail
before and pass after; they do not force the actual worker interleaving.

## Negative evidence and limits requiring explicit review

- Issue #31: original run `40164306733`, job `121082357048`, source
  `7ca0d1df1e970e622fad09240bd099b939a563c8`, remains FAIL / ROOT CAUSE UNKNOWN.
  Passing later network tests neither explain nor correct the historical
  ASan cleanup timeout. Development integration acceptance is not release
  acceptance. Preserve UNCERTAIN semantics and no automatic replay.
- The Linux x86 logging failure retained under
  `2026-10-25-logging-observation` remains causally unresolved.
- This checkpoint's local Windows GCC16/MinGW/PostgreSQL18.4 full suites remain
  FAIL, 16/19 baseline and 20/22 corrected. A libpq-only control reproduces the
  local backend exception on an extended-protocol stacked query. This isolates
  that installation without proving an upstream cause. The exact type query
  affected during server-wide recovery was not captured. All raw negatives remain.
- TSan instruments repository code, not every external dependency. Existing
  platform/runtime and descriptor-exhaustion limitations still apply; inspect
  original records rather than inferring coverage from one green job.
- Bounded real clients, TLS fixtures, compile tests and semantic fuzzing do not
  establish 24-hour stability or behavior of every external graphical client.
- Post-confirmation result-vector allocation failure is a useful additional
  future regression. Current contracts permit missing result payloads after local
  failure while preserving known item outcomes; no new defect is asserted for it.

## Independent review request

Review the full repository runtime paths, not only PR #36. Examine data integrity,
transaction state and reservation release, worker/main ownership, cancellation,
shutdown, allocation failure, statement/batch/result lifetimes, stream backpressure,
callbacks, script generations and player identity reuse. Check test validity and
negative evidence. Distinguish confirmed defects from hypotheses and external
limitations. Provide precise source evidence and minimal regression ideas for
each actionable finding; do not infer causality from a passing rerun.

Confirmed blocking defects require code correction, regression tests and
requalification. The audit must explicitly assess residual risks and state whether
the candidate is ready for preparation of an authorized stability campaign.

## Later gates

After this first independent audit, freeze the qualified runtime candidate and
dependency identities, verify isolated infrastructure and resource/result limits,
and request express owner authorization for the 24-hour campaign. It is not
authorized by the continuous task. Preserve the entire campaign, then request a
second independent Astra review. Stable v0.1.0 publication requires final owner
approval; issue #31 remains open until a technically justified pre-release
disposition is explicitly recorded.
