# Release Candidate readiness

The [logging qualification follow-up](logging-qualification.md) adds actual Linux
filesystem-failure and real Pawn/server scheduling measurements. See its exact
run evidence before treating either as completed; the stalled-storage shutdown
limit and separate publication authorization remain unchanged.

Current pre-stable cleanup: [scope, migration and exact qualification evidence](prestable-cleanup.md).
The [integrated master verification](prestable-cleanup.md#integrated-master-verification-october-19-2026-utc)
qualifies `6b4e732` with 68 natives and three forwards, including downloaded x86 packages.
Interactive/provider, extended-soak and real Pawn/tick performance limits remain explicit;
owner authorization is still required before RC publication.
Earlier revision-specific records below remain historical.

Recovered October 18, 2026: remote/local master
`c1f4d48380ca13a1c497d1a1eb23f81a49692f30`; owner merged [PR #13](https://github.com/itskoleban/omp-postgresql/pull/13)
at 08:21 UTC. [Master CI 39113244875](https://github.com/itskoleban/omp-postgresql/actions/runs/39113244875)
completed successfully, all seven jobs. This is the integrated baseline for candidate
preparation, not evidence for a later candidate commit or release approval.

The [documentation expansion audit](documentation-audit.md) records a later source review
at `77527fd` and the new eleven-example learning path. Its validation is tracked separately
from the historical integrated gates below. This documentation task does not authorize
candidate publication or change runtime/API behavior.

| Gate                   | Verified state                                                                                                                                                                                  | Remaining work                                                                                                                                                                                            |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| x86 packaging          | Integrated Windows/Linux architecture, dependency, license, deterministic assembly and fresh extracted-package server gates.                                                                    | Requalify exact RC source/packages. x64 Engine tests do not qualify x64 server packages.                                                                                                                  |
| Runtime and API        | Integrated pools, exclusive workers/FIFO, snapshots, typed arrays, batches, transactions/savepoints, bounded streaming, SQL sections and 69 documented Pawn natives.                            | Correct any new confirmed defects; preserve documented limits.                                                                                                                                            |
| Windows libpq          | PR #9 mutex correction, deterministic regression, fresh-process startup repetitions and package patch provenance integrated.                                                                    | Other providers need equivalent verification; version alone is insufficient.                                                                                                                              |
| Stress and concurrency | Integrated TSan and ASan/UBSan; prior ten-minute/2,040,672-operation load and release/capacity checks documented in [validation](validation.md).                                                | Hours-long partitions, fully instrumented dependencies and AMX loader fuzzing are not established. Finite RSS samples are not memory bounds.                                                              |
| Documentation/examples | Complete 69-native reference and six examples integrated; exact signatures/ownership/limits reviewed and examples compiled with warnings as errors.                                             | Keep final candidate identity and evidence current.                                                                                                                                                       |
| Independent audit      | Actual GPT-6 Astra ultra [audit](independent-audit.md) completed on frozen `962ee8a`; both High/P1 fixes integrated in `4c65070`, successful corrected-source CI and package evidence retained. | Bounded new release-tooling review also completed with both findings corrected; retain unproven observations and coverage limits. No confirmed critical/High finding remains open in the completed audit. |
| Performance            | Repeated Engine/transport measurements documented in [performance](performance.md).                                                                                                             | Comparative Pawn callback/tick latency remains unmeasured; no performance superiority claim.                                                                                                              |
| Candidate identity     | `0.1.0-rc.1` preparation uses a tracked CMake candidate number, exact-commit platform names, source ZIP, complete dependency manifests, notes and SHA-256 ledger.                               | Exact final-source qualification and owner review. Unsigned inventory is not a signed attestation.                                                                                                        |
| Publication            | Separate manual [release workflow](releasing.md), default draft, existing-tag/confirmation/successful-main-CI checks and isolated write permission.                                             | Owner must authorize integration, tag creation and public publication. No candidate tag or Release created by this automation.                                                                            |

Implemented, tested, integrated and published are separate states. Prior branch/preview
CI passes do not authorize integration or publication. The version label identifies
candidate preparation; exact new changes require their own qualification. A Release
Candidate remains a testing release without a production suitability guarantee.

Scope excludes COPY, LISTEN/NOTIFY reception, Engine pipeline integration, automatic lane
selection, ORM/query builders, migration management, application caches and global SQL
caches. These do not block qualification of the documented sequential Engine.

## New logging/source-diagnostic branch

The `codex/configurable-diagnostics` feature branch changes C++ lifetime/file handling and
the public inventory to 69 natives. All older evidence above retains its original source
identity; it does not qualify this branch. No merge/tag/RC publication is authorized by this
work. Exact branch qualification and remaining limits are recorded in [validation](validation.md).
OS calls on stalled storage can delay safe writer join; no hard shutdown bound is claimed.
