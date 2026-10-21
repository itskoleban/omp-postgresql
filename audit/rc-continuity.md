# RC audit continuity

## Recovery identity and objective

- Repository: https://github.com/itskoleban/omp-postgresql; default branch: `master`.
- Recovered source: `71cdf6f19f00534fd9748286673554e68cebe195`, owner commit after qualified master `fbdbaf175617e53e523d52f3a31d5ee47f9baeba` (PR #20).
- Persistent working branch: `codex/rc-audit-resumable`. No equivalent branch or tracked ledger existed in remote branches, reachable history or local reflog at recovery.
- Latest audit checkpoint: the commit containing this ledger, resolved without a circular embedded hash by `git log -1 --format=%H -- audit/rc-continuity.md`. Previous remotely confirmed checkpoint: `441297c004fa4da2e3c4cf2d04d9c98c1a755107`. The automation memory and end-of-run report record the final pushed SHA. The qualified runtime source remains `71cdf6f`, not the audit-only branch.
- Run started 2026-10-20 UTC / 2026-10-19 America/Bogota. Remote snapshot: 2026-10-20T00:43:26.941942+00:00.
- Overall objective: preserve the completed RC audit and qualification roadmap, verify changes since the last qualified source, and issue a source-specific readiness recommendation without merging, tagging or publishing.
- Current phase: continuity recovered and owner's subsequent formatting commit qualified. Runtime feature work is already integrated; do not restart it.

## Prioritized roadmap and status

| Priority | Work item | Status | Evidence / completion condition |
| --- | --- | --- | --- |
| 1 | Restore tracked, remotely durable continuity | Verified and completed | Persistent branch with ledger and compact evidence; source archive exclusion directly checked; initial checkpoint remotely confirmed. Final checkpoint identity is recorded as described above. |
| 2 | Verify exact current master `71cdf6f` and its artifacts | Verified and completed | Primary verifier and independent original-ZIP review agree: nine artifact hashes, seven logs, six JUnit suites/104 executed cases, exact source archive and both package manifests. |
| 3 | Runtime/API, lifecycle, concurrency and release tooling audit | Verified and completed at historical sources | Owner-integrated PRs #1-20; see `docs/validation.md`, `docs/independent-audit.md`, `docs/prestable-cleanup.md`, `docs/api-modernization.md`. Preserve their explicit revision-specific scope. |
| 4 | Configurable diagnostics, parser locations, actual Linux filesystem failures and Pawn scheduling qualification | Verified and completed at `fbdbaf1` | PRs #19/#20 owner-merged; prior exact-master run 39276336255: seven jobs, 104 executed CTest cases, downloaded packages and real servers. Current source qualification remains separate. |
| 5 | Final exact-source GO / NO-GO recommendation | Verified and completed for `71cdf6f` | Technical GO for owner RC review within documented scope; publication NO-GO until explicit authorization. See decision below. |
| 6 | RC tag/publication | Blocked: owner authorization required | No authorization to merge into master, create tags or publish. This automation must not perform those actions. |
| 7 | Physical storage, fully instrumented dependencies, hours-long logging soak, isolated component tick cost, PostgreSQL server-position mapping | Not started / deferred scope; explicitly unqualified | Retained limits of the completed bounded audit, not silent passes. Revisit on explicit scope or a reproducible defect; do not invent new features on recurring runs. |

## Recovery observations and commands

- Read automation memory first; newest completed entry qualifies owner-merged PR #20 at `fbdbaf1`. Older open-PR and pending-task records are superseded.
- `git status --short --branch`: clean `master`, equal to `origin/master` at `71cdf6f` before branch creation.
- Initial sandbox `git fetch origin` failed (`getaddrinfo() thread failed to start`); authorized retry succeeded. A failed attempt is not a successful fetch.
- Authenticated GitHub API: default `master`; only remote branch `master`; no open PRs; push permission available. Current master CI [39281081599](https://github.com/itskoleban/omp-postgresql/actions/runs/39281081599) completed successfully, all seven jobs. Only historical October 15 experimental prerelease exists.
- `git show HEAD`, `git diff --ignore-all-space fbdbaf1..71cdf6f`: five C++ indentation changes and Markdown table alignment only. No changed C++ tokens/literals/directives, public API, test logic or measured table values. Independent read-only review agrees.
- No applicable `AGENTS.md` found in repository or ancestors. At recovery, `git log --all -- audit/rc-continuity.md` had no entries.
- `python scripts/check_docs.py`: PASS, 69 natives and three callbacks, links/inventory validated (independent recovery reviewer).
- `git diff --check fbdbaf1..71cdf6f`: PASS.
- `clang-format --dry-run --Werror src/component.cpp src/engine.cpp src/session.cpp`: FAIL at five owner-edited indentation locations (component:186; engine:40/218/359; session:244). Style-only deviation; no runtime defect. Preserve intentional owner formatting; not a CI qualification gate.

## Files inspected and modified

Inspected: automation `memory.md`; Git history/status/branches; `.gitignore`, `.gitattributes`, `.github/workflows/ci.yml`; `src/component.cpp`, `src/engine.cpp`, `src/session.cpp`, `docs/performance.md`; `docs/rc-readiness.md`, `docs/prestable-cleanup.md`, `docs/logging-qualification.md`; `scripts/package.py`, `scripts/release.py`, `scripts/check_docs.py`; prior qualification helpers and reports under ignored `build-cleanup`, `build-log-output`, `build-runtime-qualification`.

Modified this run: `audit/rc-continuity.md`, `audit/qualification-71cdf6f.json`; `.gitattributes` adds `/audit/ export-ignore` so internal audit records are excluded from Git source archives as well as the binary package staging allowlist. No runtime, API, test, release workflow or master changes.

Local supplementary evidence: `build-resumable-audit/recovery-state.json`. Ignored local build evidence is convenient, but is not guaranteed to survive a new scheduled session. This tracked ledger, remote commits and CI artifact links carry cross-session continuity.

## Defects, concerns and limits

- No new confirmed runtime defect. Reproduce the style deviation with the clang-format command above; no automatic reversal of owner formatting.
- Existing residual: a stalled operating-system filesystem call can delay safe logger writer join indefinitely; no hard shutdown deadline, fsync or durable-flush claim.
- Not established: physical storage/network filesystem behavior, Windows kernel ENOSPC and privileged reparse races, fully instrumented libpq/OpenSSL, hours-long logging soak, isolated component tick execution cost. Original SQLSTATE/messages preserved; server-position mapping remains deferred.
- Prior measurements retain their original commit identities. The downloaded current CI captures below qualify `71cdf6f`; historical measurements in `docs/performance.md` retain their own identities.
- New audit files are outside packaged `docs/`, `examples/`, `pawn/`; `export-ignore` also excludes them from source archives. Check archive contents after committing.

## Exact-master verification obtained in this run

- `python build-cleanup/download.py 39281081599`: PASS, original three payload ZIPs, six test-log ZIPs and seven job logs downloaded. All nine outer SHA-256 digests match the authenticated API metadata.
- `python build-resumable-audit/verify-current-master.py 39281081599`: PASS. Reused prior verifier adapted to frozen master identity, without requiring audit HEAD to equal master. Exact Git tree `45c5fc022be9adac86afae861bc6b758452fa9ec`; 145-entry source archive bytes and commit comment match Git; Linux 67/Windows 70 manifest files; 50 source payloads matched per package; all seven inventory assets and 15 exact-commit note links verified.
- All six original JUnit inventories contain 104 executed cases, zero failures/errors/skips: Linux x86 18; Linux x64 OFF 18; ASan/UBSan 18; Clang 19 TSan 18; Windows x64 15; Windows x86 17. Additional fixture-only qualification output is not counted as another full suite. All seven job conclusions are success.
- Both official open.mp 1.5.8.3079 x86 source and freshly extracted package gates compiled 13 examples with warnings as errors and passed server/callback/section/logging/privacy/unload/reload checks. All four Linux configurations executed actual EACCES, one-byte RLIMIT_FSIZE short write and tiny-tmpfs ENOSPC cases. Specifically, the ENOSPC scenario preserves the same Engine SQL error/SQLSTATE/index/results and subsequent query; permission and short-write fixtures do not themselves run SQL.
- ASan/UBSan/TSan retained logs contain no reported diagnostic signatures; system dependencies are not fully instrumented.
- Four real Pawn captures verified against original console rows, source/runtime/server/package hashes and six active sink files per capture. Linux source/package each 7,200 rows and 980 resource samples; Windows source 7,198/900, package 7,196/891. Each capture validates/closes 6,144 jobs in 96 batches (5,376 measured jobs). Counted event drops exist; observed sink failures are zero. Component tick execution cost remains unmeasured.
- Current inner SHA-256: source `550878f1a8f094051fbd176d32ba2dbd9947a2513f2c6d9cf76079bab700e4a5`; Linux `387f11ef4abd6f3d3b8137d90c671e12038c7f1c9e23b1f6bd8223ab913caf25`; Windows `de352559edb769cefc2934b01cfa1ce34ea7994796a12a91cf4539512adfbc4d`.
- Durable machine-readable details: `audit/qualification-71cdf6f.json`, including all nine artifact IDs/digests, original job IDs, individual JUnit results and capture hashes. Original ZIPs remain GitHub CI artifacts and local ignored `build-cleanup/ci-artifacts/39281081599`; GitHub retention is finite. No new local runtime/benchmark/sanitizer workload was run.
- `python scripts/check_docs.py`: PASS again, 69 natives/three callbacks and links/inventory. `git diff --check`: PASS. Direct `git archive` of `441297c` plus ZIP inspection: PASS, audit paths excluded, ordinary source and commit comment retained. Binary package staging already excludes `audit/`.
- A second sandbox `git ls-remote` failed with the same network restriction; authorized retry succeeded and confirmed audit `441297c` and unchanged master `71cdf6f`. No conflicting writer observed.
- Independent read-only reviewer checked original ZIPs/logs, not the generated summary: all nine outer hashes/sizes; 104 individual JUnit cases with `status=run`; exact 145-entry/130-file source bytes, executable modes and comment; manifests and 50 source payloads plus eight root/legal/patch payloads per package; four capture hashes/counts and six sink files each. PASS, no discrepancy or runtime defect. Two ledger wording corrections applied (historical recovery state and ENOSPC-specific SQL evidence).
- Audit-only initial checkpoint automatically triggered CI [39349462096](https://github.com/itskoleban/omp-postgresql/actions/runs/39349462096), observed in progress, not a pass. Subsequent ledger-only pushes can supersede it. These runs are separate from qualified master run 39281081599; never relabel their status or artifacts as master evidence.

## Source-specific readiness decision

Verification completed: 2026-10-20T00:50:29.445496+00:00.

**Technical GO for owner RC review of `71cdf6f19f00534fd9748286673554e68cebe195` within the documented test scope.** No confirmed functional finding remains in this bounded continuation. Runtime/API work and the defined audit roadmap are complete; the deferred coverage limits above remain unqualified. No production guarantee or hard stalled-storage shutdown bound follows from these results.

**Publication NO-GO without separate owner authorization.** No RC tag or release was created, and master was not merged or modified. The audit branch exists only for durable internal continuity and is not the qualified publication source. No PR is necessary for this checkpoint.

## Current unfinished item and immediate next action

No unfinished implementation or required technical verification remains in this selected objective. Pending owner decision: whether to authorize RC tag/publication, accepting the stated scope and limits or explicitly requesting additional qualification first.

Next scheduled execution: read this ledger and automation memory; `git fetch origin`; inspect working tree, open PRs and relevant CI; reuse `codex/rc-audit-resumable`. Compare remote master with `71cdf6f`. If unchanged and no new reproducible issue or explicit scope, report that no further technical work is required. Inspect any failure of automatic audit-branch CI, but do not rerun the qualified master workloads merely because this automation replays. If master changes, qualify only affected work and bind any new package evidence to that exact source. Never overwrite another execution's uncommitted edits or force-push.

Completion evidence: original artifact hashes/source/archive/manifests and executed test outcomes checked; independent review passed; audit source-export exclusion checked; decision and limitations recorded. Final commit/push is the checkpoint containing this section. Confirm its remote identity with `git ls-remote --heads origin codex/rc-audit-resumable` and consult the automation memory for the last run's actual result. Local ignored files alone never guarantee continuation.
