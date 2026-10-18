# Release Candidate readiness

Status recovered on October 18, 2026 from local/remote `master` at
`556dca6fc3797f1646dab71692d8f5d6d098c5af`, including the owner's integration of API review
commit `962ee8a`. Corrections to independent audit findings are on
[PR #13](https://github.com/itskoleban/omp-postgresql/pull/13), awaiting owner review.
This checklist is not release approval. Recheck GitHub before changing status.

| Gate                           | Verified state                                                                                                                                                                                                                                                     | Remaining work                                                                                                                                                                                                                        |
| ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Experimental x86 packaging | Integrated. [Master CI 39045902836](https://github.com/itskoleban/omp-postgresql/actions/runs/39045902836) passed; prior API-review CI verified both extracted-package server installations and downloaded artifacts. | Requalify both distributions with the audit corrections and exact final candidate identity. |
| Runtime and API                | Integrated: service pools, exclusive workers/FIFO, immutable snapshots, typed arrays, heterogeneous batches, transactions/savepoints, bounded streaming and 69 Pawn natives including named SQL sections.                                                          | Resolve any confirmed defects found by expanded qualification or independent audit.                                                                                                                                                   |
| Windows libpq mutex correction | [PR #9](https://github.com/itskoleban/omp-postgresql/pull/9) integrated at the baseline above; deterministic dependency regression, fresh-process startup and package patch provenance.                                                                            | Other Windows libpq providers require equivalent evidence; a version number alone is insufficient.                                                                                                                                    |
| Concurrency and sustained load | PR #10 integrated. Current master CI passes all configured tests including Clang 19 TSan and ASan/UBSan. [Validation](validation.md#latest-integrated-qualification-october-17-2026) records the exact ten-minute local load and release/capacity checks.          | Instrumented dependencies, hours-long soaks and AMX loader fuzzing are not established. Native-boundary tests and finite memory samples have narrower scope.                                                                          |
| Performance                    | Repeated Engine/transport measurements and correctness checks are documented in [performance](performance.md).                                                                                                                                                     | Comparative Pawn callback/tick latency remains unmeasured. Finite stress memory samples are not memory bounds.                                                                                                                        |
| Documentation and examples | API reference and six-example lifecycle map integrated in `962ee8a`. Exact signatures/defaults, return/error distinctions, ownership and limits reviewed; six examples compiled with warnings as errors. | Keep exact-candidate behavior and documentation aligned; this does not promise a stable API or exhaustive branch coverage. |
| Independent audit | Actual GPT-6 Astra, ultra, completed on frozen `962ee8a`; [report](independent-audit.md) records two High/P1 defects, independent correction review and unproven observations. Fix commit `4c65070` passes local 15/15 CTest and 33 Python cases. | Final corrected-commit CI/package qualification and owner review; no critical finding established. Callback reset reachability, portable device-name behavior and broader coverage limits remain documented concerns. |
| Candidate and publication      | Version remains 0.1.0 experimental; no RC qualification is claimed.                                                                                                                                                                                                | Prepare exact-commit RC packages, dependency/source/provenance/checksum inventory and release notes after the gates above; prepare a separate publication workflow. Owner approval is required for main merges, tags and publication. |

For the independent review, freeze the source commit, CI run/checkout SHA, both package
manifests and checksums, dependency patch/build records, test logs/JUnit, benchmark raw
data and conditions, public include and [architecture](design.md). Supply the known
limits above and the [validation history](validation.md), including failures and toolchain
limitations. Do not substitute another model or describe a prepared prompt as an executed
audit. Fix confirmed high/critical findings with reproductions and rerun affected suites.

Candidate scope excludes COPY transfer, receiving LISTEN/NOTIFY, pipeline integration,
automatic lane selection, ORM/query builders, application caches and global SQL caches.
They do not block qualification of the documented sequential engine. Windows/Linux x64
engine builds do not establish a compatible x64 open.mp component distribution.

Implemented, tested, integrated and published are separate states. A successful branch
or preview-merge CI run does not authorize merging or publishing that change. A Release
Candidate would remain a testing release, without a guarantee of production suitability.
