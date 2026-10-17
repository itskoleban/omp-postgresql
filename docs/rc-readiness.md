# Release Candidate readiness

Status recovered on October 17, 2026 from `master` at
`567add0cd9d50b23b78a3c18e4cb076ef64c0287`. This is a qualification checklist,
not a release approval or an independent audit. Recheck GitHub before changing status.

| Gate                           | Verified state                                                                                                                                                                                                    | Remaining work                                                                                                                                                                                                                        |
| ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Experimental x86 packaging     | Integrated. [Master CI 39009167983](https://github.com/itskoleban/omp-postgresql/actions/runs/39009167983) passed all six jobs, both extracted-package server installations and downloaded-artifact verification. | Regenerate and verify packages from the exact final candidate after fixes.                                                                                                                                                            |
| Runtime and API                | Integrated: service pools, exclusive workers/FIFO, immutable snapshots, typed arrays, heterogeneous batches, transactions/savepoints, bounded streaming and 69 Pawn natives including named SQL sections.         | Resolve any confirmed defects found by expanded qualification or independent audit.                                                                                                                                                   |
| Windows libpq mutex correction | [PR #9](https://github.com/itskoleban/omp-postgresql/pull/9) integrated at the baseline above; deterministic dependency regression, fresh-process startup and package patch provenance.                           | Other Windows libpq providers require equivalent evidence; a version number alone is insufficient.                                                                                                                                    |
| Concurrency and sustained load | Existing baseline CI passes. [PR #10](https://github.com/itskoleban/omp-postgresql/pull/10) adds TSan and timed load qualification.                                                                               | Record exact results and owner review; instrumented dependencies, longer soaks and AMX loader fuzzing are not established by native-boundary tests.                                                                                   |
| Performance                    | Repeated Engine/transport measurements and correctness checks are documented in [performance](performance.md).                                                                                                    | Comparative Pawn callback/tick latency remains unmeasured. Finite stress memory samples are not memory bounds.                                                                                                                        |
| Documentation and examples     | Six warning-free examples and real x86 server scenarios are qualified by baseline CI.                                                                                                                             | Complete a signature/return/error/ownership/limit/example review of every native; reconcile historical documentation and decide whether a combined example adds value.                                                                |
| Independent audit              | Not performed with GPT-6 Astra. Existing design/extension notes are internal implementation reviews.                                                                                                              | Review an exact qualified commit using GPT-6 Astra at its highest available effort, record findings and verify important fixes.                                                                                                       |
| Candidate and publication      | Version remains 0.1.0 experimental; no RC qualification is claimed.                                                                                                                                               | Prepare exact-commit RC packages, dependency/source/provenance/checksum inventory and release notes after the gates above; prepare a separate publication workflow. Owner approval is required for main merges, tags and publication. |

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
