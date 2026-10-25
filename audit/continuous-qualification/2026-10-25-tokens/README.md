# Controlled Pawn token boundary checkpoint

## Scope

Test-only continuation of PR #32 after storage-fix commit
`6aeb8774f50abfb666ebb9ecf3abda4fd5666efc`. Neither tutorial's production
logic is changed. Each fixture still includes and invokes the actual tutorial.
Only the disposable test gamemodes expose counter-seeding commands.

Two new real-network cases (`account_token_boundary` and `custom_token_boundary`)
seed each tutorial's SVar counter to 2147483646. A fresh real player must receive
2147483647 exactly once. After that player disconnects, two successive real
connections must reuse that player ID, be disconnected by the original exhaustion
guard, and retain zero token/account/score/pending work with an unchanged counter.
A separately seeded negative counter must also reject a fresh connection without
issuing a token or job. The tests require no billions of operations.

The client fixture explicitly waits for the expected kick's disconnect event.
Cleanup must then stop the client without waiting for a second disconnect event.
Ordinary clients retain their original connected=1 and disconnect assertions.
The extra snapshot fields are numeric generation/pending-work observations only.

## Results at publication

- Both real-session Pawn fixtures compile with the pinned Pawn compiler and
  warnings treated as errors (`-E`): PASS. Compiler headers at
  `29f1a8c7fd2e01929807cd8c50353fbc93bbd651`; omp-stdlib at
  `46a5d1d7ac8985e4104f745c462a247a0c2eb6a0`; provider header at
  `c60e68d483a95d109855fdd7517425c0363fd10c`.
- Python syntax compilation: PASS.
- Python suite: 143 cases, 142 PASS and one existing process-sampler SKIP,
  no failures/errors. The original capture remains `python-with-skip.txt`.
  A subsequent focused sampler check passes, but does not retroactively qualify
  the earlier skipped observation or establish its exact environmental cause.
- Additional semantic fuzzing of the unchanged storage-fix C++ sources: PASS,
  300000 cases, seed 1096107081, stdout retained. This is ordinary Debug evidence,
  not a new sanitizer pass.
- Storage-fix source `6aeb877`: hosted run 40227759035 has Linux OFF, ON
  (ASan/UBSan) and THREAD jobs completed successfully at this checkpoint.
  Other jobs are still running. This is not a complete seven-job verdict.
  A newer commit can supersede that run under the existing concurrency policy;
  a cancelled run is not passing evidence.
- New exact-head Linux/Windows real-network token cases: **PENDING** until their
  hosted jobs actually execute. Compile/parser checks do not qualify a network
  scenario. Final run/head/result links belong in the PR description.

## Continuation and unchanged gates

Inspect the exact new head's hosted CI, preserve all failures, and correct any
fixture or component defects using evidence from the failed run. Do not weaken
identity, disconnect, SQL-dispatch or uncertain-operation assertions.

Dedicated SQL permission and interrupted-read coverage remains pending; existing
logger permission/ENOSPC tests do not replace it. The local sanitizer failure and
its empty-main control remain recorded in the earlier storage checkpoint.

Issue #31 remains OPEN, ROOT CAUSE UNKNOWN; its historical FAIL is untouched.
Neither this test addition nor a passing rerun explains that historical incident.
The first independent Astra audit and consolidated audit handoff remain PENDING.
The 24-hour campaign remains BLOCKED pending express owner authorization and
approved isolated infrastructure. Stable release still requires final owner
approval. Master is not changed; no merge, branch deletion, tag or release occurs.
