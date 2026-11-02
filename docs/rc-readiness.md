# Release Candidate readiness

The [finite pre-soak checklist](pre-soak-checklist.md) records the November 2 follow-up
to successful RC.3 [CI 41372458054](https://github.com/itskoleban/omp-postgresql/actions/runs/41372458054).
It closes practical bounded gaps within their stated scopes and defines the exact
final-source/host prerequisites. The final 24-hour campaign remains unexecuted and
requires separate owner authorization; stable readiness remains blocked.

This source prepares **v0.1.0-rc.3** for testing and evaluation. The
[Releases page](https://github.com/itskoleban/omp-postgresql/releases) records public
availability. Preparation does not create a tag, publish assets or certify production
suitability. Stable v0.1.0 remains blocked.

## RC.3 integrated baseline and scope

The verified pre-preparation `master` is
**`27e09b7cffa34968b78cb0025c121fce0a713e65`**, after PRs
[#43](https://github.com/itskoleban/omp-postgresql/pull/43),
[#44](https://github.com/itskoleban/omp-postgresql/pull/44),
[#45](https://github.com/itskoleban/omp-postgresql/pull/45) and
[#46](https://github.com/itskoleban/omp-postgresql/pull/46).
All seven jobs passed in
[master CI 41312759129](https://github.com/itskoleban/omp-postgresql/actions/runs/41312759129).
This integrated-source result is distinct from PR checks. Its successful qualification
is retained as baseline evidence; it does not qualify subsequent RC.3 metadata or fixes.

RC.2 was published from `3c3b3185c24362be12eb3e281383ad05d5dcd9b6`; its original
assets retain their names and hashes. RC.3 combines the renamed `PostgreSQL.dll` /
`PostgreSQL.so` and `omp_postgresql.inc` with the worker-admission correction and
network/campaign diagnostic improvements. Follow the
[migration instructions](installation.md#migrating-from-previous-release-candidates).
The public Pawn signatures, constants, callbacks and component UID are unchanged.
The fixture evidence-storage correction is delivered separately in
[PR #47](https://github.com/itskoleban/omp-postgresql/pull/47) and included in this
candidate source. It completes shutdown after journal mkdir/copy failures and
preserves the workload's original failure.

The worker correction rejects submissions after admission closes during unexpected
worker termination instead of leaving them pending in a stopped queue. Already accepted
jobs still require terminal publication. The deterministic Engine/Lane regression does
not claim that libpq throws C++ exceptions or explain historical issue #31.

## RC.3 exact-source gates

The dedicated preparation PR needs successful exact-head CI. After separate owner
approval and integration, all seven jobs must pass on the **exact final `master`
commit**. The prior baseline and PR preview cannot substitute for this integrated run.
Changes after qualification require the affected checks and a final matching source.

Require both Windows x86 and Linux x86 distributions from that commit, installed-package
official open.mp 1.5.8.3079 checks and real-session evidence, version `0.1.0-rc.3` /
channel `rc`, tracked source archive, dependency/payload manifests, release inventory,
English notes and verified SHA-256 ledger/sidecars. Preserve frozen historical evidence.
The [manual release procedure](releasing.md) requires the separately authorized tag to
resolve to that exact integrated commit and preserves Pre-release classification.
No tag, merge or public release is authorized by preparation or CI.

## Residual-risk disposition

[Issue #31](https://github.com/itskoleban/omp-postgresql/issues/31) remains open with
**ROOT CAUSE UNKNOWN** for the historical ASan network cleanup timeout. Diagnostic
collection now requires successful observer results, but successful later captures do
not establish historical causation. Preserve `UNCERTAIN` SQL outcomes and never replay
uncertain writes automatically. A recurrence or newly demonstrated defect blocks the
candidate until it has a specific disposition and correction where justified.

The [resource investigation](https://github.com/itskoleban/omp-postgresql/blob/master/audit/windows-resource-handles-2026-11-01.md)
reproduces the exact residual +64 with direct libpq TLS on a joined thread, without
an Engine; repeated cycles are stable and complete process exit clears the handles.
The vector and shell-only control identify dependency/Windows initialization as a
sufficient path. Individual unnamed allocator stacks remain unavailable. No plugin
cleanup defect is established by these observations, so the residual count does not
block an evaluation RC.3. This disposition does not claim leak freedom or stable readiness.

RC.3 is an evaluation vehicle for integrated corrections, subject to final CI.
Resource samples alone cannot prove leak freedom. Historical Windows stalls,
the Linux logging anomaly and local MinGW/libpq failures retain their original outcomes
and documented limits. Stable release requires a defensible disposition of these risks,
prolonged qualification and the final owner gate; a later passing run is not a correction.
See [validation](validation.md) and [release notes](release-notes.md).

## Dedicated 24-hour campaign preconditions

**Status: BLOCKED; not started.** The existing
`dedicated stability audit` workflow targets a manually verified disposable Linux x64
runner labelled `pg-audit-isolated`; a label in YAML does not establish that such a
runner exists or is isolated. Historical owner confirmation recorded no available
isolated runner. Read-only local checks on November 1 found only the stopped
`docker-desktop` WSL distribution, an unavailable Docker daemon, and no configured
`pg-audit-isolated` SSH target. GitHub runner registration/host isolation remains
unverified. No services or infrastructure were provisioned or started by those checks.
No new usable isolated Linux runner has been verified for this candidate.
Do not dispatch the 24-hour workflow until all of these concrete gates are recorded:

| Gate | Required evidence before starting |
| --- | --- |
| Risk review | Explicit candidate-specific disposition of issue #31, resource ownership findings, historical failures and any remaining coverage gaps; no unexplained newly reproduced blocker. |
| Source and deployment freeze | Final source SHA, installed component/include bytes, complete dependency and actual loader/runtime hashes, compiler/fixture/provider revisions, TLS configuration and positive uint32 seed; read-only staged deployment. |
| Runner isolation | Existing local/self-hosted Linux host, non-root UID, no production credentials/data, disposable loopback PostgreSQL cluster, writable evidence storage and sufficient free capacity. Verify process-tree cleanup and absence of leftover fixture backends. Do not purchase/provision paid services. |
| Bounded target preflight | Build/sanitizer provenance plus complete applicable core, TLS/network, real ENOSPC/EACCES/short-write, semantic-fuzz and installed official open.mp/real-session checks on the selected bytes and host. Preserve failed attempts and restarts. |
| Measurement contract | Freeze 86400 measured load seconds, seed, warm-up and sampling schedule; collect SQL operation counts, retained results, quiescent queues, terminal counters, backend activity, thread/handle/FD ownership, CPU/RSS and wave latency percentiles. Root-cause attribution remains separate from trend measurements. |
| Acceptance and stop criteria | Zero sanitizer findings, crashes, deadlocks, lost/duplicate terminal publications, semantic/transaction violations, unexpected uncertain outcomes or quiescent pending/errors. Stop on recurrence of issue #31, fixture cleanup failure or evidence-collection failure. Select numerical RSS/resource/latency growth and shutdown budgets from bounded measurements on the frozen host before launch; do not invent them afterward. |
| Preservation and review | Record all stage commands, timestamps, exact binaries, source/dependency identities, server/client/observer traces, raw CTest XML/logs, samples and failures; retain SHA-256 ledger outside the disposable cluster. Confirm storage/retention and inspect complete evidence independently after the campaign. |
| Owner authorization | Explicit authorization to start the qualified campaign. Preparation and passing preflight provide no such authorization. |

The current supervisor enforces requested duration, records failed preflight and rejects
inconsistent metrics. It does not verify isolation, freeze deployed bytes, choose
host-specific growth thresholds or establish a completed campaign. The Engine soak and
semantic fuzz alone do not run a 24-hour Pawn/server session; record that coverage limit
or add a separately justified component campaign before making such a claim.

## Historical RC.2 qualification context

RC.2 preparation began at `02323260583a5c6fbbcb58d933f0bc0c4121bcf5`, whose seven
jobs passed in [CI 40799996089](https://github.com/itskoleban/omp-postgresql/actions/runs/40799996089).
That historical baseline is separate from RC.2's published commit and from RC.3.
Published RC.1/RC.2 tags, assets and qualification records remain unchanged.

## Historical RC.1 baseline and gates

[v0.1.0-rc.1](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.1)
is a published testing Pre-release. Its original tag and assets remain unchanged.
The following table records RC.1-era qualification, not completion of RC.2 gates.

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
preview or branch run is not exact-commit `master` evidence. CI success does not authorize
a future tag or release. See [release notes](release-notes.md) and
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

## Completed audit preservation and branch retirement

[PR #22](https://github.com/itskoleban/omp-postgresql/pull/22) was owner-merged on October 21,
2026 as `da5ced7fd85b918c43c62296169463d39129cae2`. Its seven-job PR CI and subsequent
[master CI 39631310024](https://github.com/itskoleban/omp-postgresql/actions/runs/39631310024)
succeeded. Both records are reachable from `master` and retain the exact historical bytes;
source archives exclude them and public packages use repository links. The temporary
`codex/rc-audit-resumable` branch has been retired. It is not a remaining integration task.

Later formatting source `162a061b279f16f8b032b7a62f779c0b29e589c3` also passed all seven
jobs in [master CI 39634333824](https://github.com/itskoleban/omp-postgresql/actions/runs/39634333824).
These development results do not replace RC.1's tagged source or qualification. Future
changes need their own checks; future releases need separate owner authorization under
the [release procedure](releasing.md).

Publication of RC.1 is complete. Audit preservation and later documentation
maintenance do not extend its audit scope or certify production safety.
