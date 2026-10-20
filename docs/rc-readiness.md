# Release Candidate readiness

The intended candidate is **v0.1.0-rc.1**. Documentation and release preparation are
ready for owner review within the limits below. Publication remains unauthorized;
an RC is a testing release, not stable-release or production certification.

## Verified baseline and current gates

Last verified `master`: **`71cdf6f19f00534fd9748286673554e68cebe195`**, with all seven
jobs successful in [CI 39281081599](https://github.com/itskoleban/omp-postgresql/actions/runs/39281081599).
The [source-specific evidence](validation.md#verified-master-baseline-october-20-2026-utc)
records original artifacts and 104 executed CTest cases without failures/errors/skips.
PRs #19/#20 are integrated: configurable logging, parser source locations, actual Linux
filesystem-fault checks and real Pawn/server measurements are completed within that scope.
The final documentation merge still needs its own exact-commit CI and package review.

| Gate                   | Verified state                                                                                                                                                                       | Remaining work                                                                                                                                                                |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Runtime and API        | 69 natives, three callbacks; pools, exclusive workers/FIFO, snapshots, typed arrays, batches, transactions/savepoints, bounded streaming, SQL sections and configurable diagnostics. | Preserve documented limits; report new confirmed defects separately.                                                                                                          |
| Documentation/examples | Exact include/reference signatures and 13 public Pawn examples; 11 unattended examples plus two interactive tutorials.                                                               | Review this documentation PR; accounts/custom gameplay and external providers need operator validation.                                                                       |
| x86 packaging          | Both Windows/Linux source and fresh extracted-package official-server gates passed at the baseline; manifests, hashes and source ZIP verified.                                       | Qualify final merged source/packages. x64 Engine tests do not qualify x64 server components.                                                                                  |
| Windows libpq          | Integrated mutex correction, deterministic regression, fresh-process repetitions and patch provenance.                                                                               | Other providers need equivalent verification; version alone is insufficient.                                                                                                  |
| Stress and concurrency | ASan/UBSan and Clang 19 TSan; historical ten-minute/2,040,672-operation load retains its recorded source.                                                                            | Hours-long partitions/logging soaks, fully instrumented dependencies and AMX loader fuzzing remain unqualified. Finite RSS samples are not memory bounds.                     |
| Logging faults         | Actual Linux EACCES, one-byte short write and tmpfs ENOSPC; ENOSPC preserves same-Engine SQL errors/results and recovery.                                                            | Physical devices, Windows kernel ENOSPC, privileged reparse races and network filesystems remain unqualified. Stalled OS calls can delay writer join without a hard deadline. |
| Performance            | Engine/transport measurements and actual Pawn callback/timer scheduling plus process CPU/RSS are recorded with source identities.                                                    | Isolated component tick execution cost, causal per-destination overhead and comparison with other components are not established.                                             |
| Independent audit      | Bounded Astra audit at `962ee8a`, corrections and later source-specific reviews completed; no confirmed critical/High finding remains open in that audit.                            | Preserve its historical scope; it does not certify later code or all possible defects.                                                                                        |
| Candidate identity     | CMake/package identity `0.1.0-rc.1`, intended tag `v0.1.0-rc.1`, exact-source notes, manifests and SHA-256 ledger.                                                                   | Review the final source/distribution inventory. Hashes are not signed attestations.                                                                                           |
| Publication            | Separate manual [release workflow](releasing.md), default draft and exact-master CI/existing-tag/confirmation checks.                                                                | Owner reviews and merges; final master CI must pass; owner separately authorizes tag creation and publication.                                                                |

Implemented, verified, integrated and published are separate states. A successful PR
preview or branch run is not exact-commit `master` evidence. No merge, tag or release is
authorized by this documentation review. See [release notes](release-notes.md) and
[installation prerequisites](installation.md).

## Historical qualification context

The [pre-stable cleanup](prestable-cleanup.md) qualified `6b4e732` with **68 natives and
three forwards** before `pg_log_config` was added. That count and its results are retained
as historical evidence, not the current public inventory.

Recovered October 18, 2026: owner-integrated master
`c1f4d48380ca13a1c497d1a1eb23f81a49692f30` after [PR #13](https://github.com/itskoleban/omp-postgresql/pull/13)
had all seven jobs pass in [CI 39113244875](https://github.com/itskoleban/omp-postgresql/actions/runs/39113244875).
Its documentation gate covered six examples. The later [documentation audit](documentation-audit.md)
at baseline `77527fd` introduced the eleven-example learning path. Later callback/logging
work brought the public inventory to thirteen. None of these historical counts or
measurements is rewritten as evidence for another source.

The preserved `codex/rc-audit-resumable` branch contains the completed internal audit at
`255f568`; it is separate from qualified master and is not a branch to merge for publication.
Its original records are linked from [validation](validation.md#verified-master-baseline-october-20-2026-utc).

## Owner actions before publication

1. Review and integrate the focused documentation PR into `master`.
2. Require all seven existing `build-and-test` jobs to pass on that exact final master
   commit; review no-skips CTest results, both fresh x86 package gates, source identity,
   manifests, checksums and the release inventory. Earlier runs cannot satisfy this gate.
3. Accept the stated limits and separately authorize/create **v0.1.0-rc.1** on that tested
   commit. Do not tag the audit branch or a PR preview merge.
4. Follow [the manual release procedure](releasing.md) with the exact CI run and tag.
   Review a draft prerelease and authorize public publication separately; do not mark it stable.

**GO for documentation and release preparation for owner review. NO-GO for publication
until the owner completes the integration, exact-master qualification and authorization gates.**
