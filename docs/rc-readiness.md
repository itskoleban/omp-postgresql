# Release Candidate readiness

[v0.1.0-rc.1](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.1)
is published as a testing Pre-release. It is not stable-release or production
certification; later repository changes do not alter that tag or its assets.

## Verified baseline and current gates

Published source: **`db2bfb9b816b0042b8ef36e847cf9fb1c24e94fc`**, with all seven
jobs successful in [CI 39515353213](https://github.com/itskoleban/omp-postgresql/actions/runs/39515353213)
and [release workflow 39584133358](https://github.com/itskoleban/omp-postgresql/actions/runs/39584133358).
The [publication record](validation.md#published-release-candidate-october-21-2026-utc)
separately identifies its checked assets. The earlier `71cdf6f` qualification
[evidence](validation.md#verified-master-baseline-october-20-2026-utc)
records original artifacts and 104 executed CTest cases without failures/errors/skips.
PRs #19/#20 are integrated: configurable logging, parser source locations, actual Linux
filesystem-fault checks and real Pawn/server measurements are completed within that scope.
The final documentation merge passed its own exact-commit CI and package review before
publication. Subsequent documentation changes need their own CI and packaging checks.

| Gate                   | Verified state                                                                                                                                                                       | Remaining work                                                                                                                                                                |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Runtime and API        | 69 natives, three callbacks; pools, exclusive workers/FIFO, snapshots, typed arrays, batches, transactions/savepoints, bounded streaming, SQL sections and configurable diagnostics. | Preserve documented limits; report new confirmed defects separately.                                                                                                          |
| Documentation/examples | Exact include/reference signatures and 13 public Pawn examples; 11 unattended examples plus two interactive tutorials.                                                               | Accounts/custom gameplay and external providers need operator validation; later documentation changes need their own checks.                                                  |
| x86 packaging          | Both Windows/Linux source and fresh extracted-package official-server gates passed at the baseline; manifests, hashes and source ZIP verified.                                       | Qualify subsequent source/package changes separately. x64 Engine tests do not qualify x64 server components.                                                                  |
| Windows libpq          | Integrated mutex correction, deterministic regression, fresh-process repetitions and patch provenance.                                                                               | Other providers need equivalent verification; version alone is insufficient.                                                                                                  |
| Stress and concurrency | ASan/UBSan and Clang 19 TSan; historical ten-minute/2,040,672-operation load retains its recorded source.                                                                            | Hours-long partitions/logging soaks, fully instrumented dependencies and AMX loader fuzzing remain unqualified. Finite RSS samples are not memory bounds.                     |
| Logging faults         | Actual Linux EACCES, one-byte short write and tmpfs ENOSPC; ENOSPC preserves same-Engine SQL errors/results and recovery.                                                            | Physical devices, Windows kernel ENOSPC, privileged reparse races and network filesystems remain unqualified. Stalled OS calls can delay writer join without a hard deadline. |
| Performance            | Engine/transport measurements and actual Pawn callback/timer scheduling plus process CPU/RSS are recorded with source identities.                                                    | Isolated component tick execution cost, causal per-destination overhead and comparison with other components are not established.                                             |
| Independent audit      | Bounded Astra audit at `962ee8a`, corrections and later source-specific reviews completed; no confirmed critical/High finding remains open in that audit.                            | Preserve its historical scope; it does not certify later code or all possible defects.                                                                                        |
| Candidate identity     | Published `v0.1.0-rc.1`, exact-source notes, manifests and SHA-256 ledger.                                                                                                           | Preserve the published tag/assets. Hashes are not signed attestations.                                                                                                        |
| Publication            | Owner-authorized testing Pre-release published after exact-master CI and draft/public asset verification.                                                                            | Future releases require separate exact-source qualification and owner authorization.                                                                                          |

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

The completed internal audit checkpoint is
**`255f56847dcc76353cb6388f1990b616569bc038`**, originally on `codex/rc-audit-resumable`.
Its [continuity ledger](https://github.com/itskoleban/omp-postgresql/blob/master/audit/rc-continuity.md)
and [qualification JSON](https://github.com/itskoleban/omp-postgresql/blob/master/audit/qualification-71cdf6f.json)
are preserved byte for byte under `audit/`. Original dates, source identities and historical
publication decisions remain intact. These are repository-only records: source archives
exclude `/audit/`, and binary staging is unchanged. See
[validation](validation.md#verified-master-baseline-october-20-2026-utc) for provenance.

## Owner actions for audit-branch retirement

1. Review the focused audit-preservation/documentation PR and its own CI. Integrate only
   with owner approval; do not merge the original audit branch wholesale.
2. Verify both records are reachable from `master` and still match checkpoint `255f568`.
   Check their GitHub links, source-archive exclusion, package gates and final master CI.
3. Only after those checks, delete remote `codex/rc-audit-resumable`. Until then retain it
   as an additional reference to the original evidence.
4. Preserve the published RC tag and assets. Future releases need separate version/source
   qualification and owner authorization under the [release procedure](releasing.md).

Publication of the testing RC is complete. Audit preservation and later documentation
maintenance do not extend its audit scope or certify production safety.
