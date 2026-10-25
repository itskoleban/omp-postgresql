# PR #30 final qualification and resumed audit

The resumed task recovered the original workspace, branch and all local evidence.
Remote PR #30 and its branch still pointed to
`194ae7f5b27d8ffd9653f43ddfe877dfa67b8a84`; master remained
`d0494d2bf45c03757864af0f5dfa2c7b868f4810`. There were no later commits, review
threads, comments or review submissions to overwrite. Work continues on the same PR.

The [original detector investigation](tsan-detector-investigation.md),
[focused concurrency audit](tsan-focused-concurrency.md) and all negative captures
remain intact. The detector fixture remains **WORKAROUND VALIDATED** for the
demonstrated LLVM 19 shadow-snapshot limitation. The exact internal schedule of
master's historical missed detection remains **ROOT CAUSE UNKNOWN**, and run
40142964957 remains **FAIL**. The separate `Engine::poll` correction remains valid.

## Completed checkpoint and publication defect

Workflow [40146291384](https://github.com/itskoleban/omp-postgresql/actions/runs/40146291384)
completed seven successful jobs at 194ae7f: all **154 CTest cases** ran successfully,
with no mandatory skips. The [checkpoint record](tsan-detector-evidence/ci-194ae7f.json)
contains job IDs, independent downloaded artifact digests, test inventories, detector
results and session/package verification. Both x86 platforms passed **21/21** real
scenarios, including the original nineteen and the two PR #29 additions. Each public
session inventory verified 67 files and a 347-event timeline. Both downloaded x86
packages independently passed the repository verifier at that exact source.

The TSan artifact's manifest listed 73 public files, but the uploader omitted
`source/.github/workflows/ci.yml` because hidden directories are excluded by default.
The other 72 files verified. The missing source was recovered from the exact Git
commit and matched its recorded SHA-256; it was not retroactively inserted into the
original ZIP. That check suite remains PASS, while its publication is recorded as
incomplete. A separately observed Windows long-path extraction omission was addressed
by reading original ZIP members; it did not change the archive or its contents.

The recorder now copies the workflow's exact bytes to
`source/github-workflows/ci.yml` and rejects hidden destination paths. It also retains
both oracle scripts and the recorder source. It does not enable broad hidden-file
publication. The new regression failed on the old recorder and passed after this
correction. The final artifact must verify every manifest entry directly from the
downloaded ZIP before readiness is asserted.

## Additional confirmed component defect

Although `Engine::poll` safely pre-reserved Dispatch storage, `Component::callback`
could allocate while storing a missing-public, argument-push or execution error.
An exception there escaped `onTick` after poll had consumed the entire batch and
marked its jobs delivered. Destroying that Dispatch permanently lost later callbacks.
Similarly, constructing `Failure` for a handle released by an earlier callback could
allocate during validation and abort later valid deliveries.

The original public-API reproduction used two real, confirmed SQL jobs and the
unchanged Component at 194ae7f. One main-thread allocation failed while storing the
first missing-public error. The binary returned 1 with both jobs marked delivered,
zero later callback executions and the second callback metadata still retained.
Relinking the unchanged regression with the corrected Component returned 0, with one
later callback and no late duplicate. Binary and source hashes, complete commands,
results and independently stopped disposable clusters are recorded in
[callback allocation evidence](tsan-detector-evidence/callback-allocation-verification.json).

The four maintained scenarios separately test missing-public, push-error, Exec-error
storage and a stale handle released by an earlier callback. To exercise each old path
independently, the original Component received only the same test arming seams, without
any recovery correction. All four executions failed with a real denied allocation,
an escaped exception and zero later valid callbacks. The unmodified two-job
reproduction remains separate evidence; the instrumented baseline is never represented
as an unmodified binary.

## Narrow correction and review

Callback error recording now follows the existing native boundary's best-effort,
nonthrowing policy. If assigning diagnostic text fails, the text is cleared without
another allocation. The existing callback event is submitted independently; disabled
or saturated logging does not guarantee its retention. This does not alter SQL status,
retry SQL or promise a successful application callback when its public is missing.

Push and Exec failures restore the complete VM call frame before recording their
error. The recorder checks the current AMX owner before accessing its error storage.
The existing post-Exec owner check still avoids touching a VM unloaded by a callback.

The two handle-validation loops now also catch `std::bad_alloc`. A valid
`Engine::job` lookup performs a map lookup and shared-pointer copy without allocating;
the allocating failure path denotes a stale handle, which should be skipped after
an earlier callback deliberately released it. The catch surrounds only validation,
so it does not silently treat arbitrary SDK exceptions as stale jobs.

Normal and ASan regressions deny one actual main-thread C++ allocation at each selected
internal boundary. TSan uses named checkpoints in the actual Component implementation
and retains its global allocator interceptors. All four cases passed independently in
normal, ASan/UBSan and TSan, preserving later exactly-once delivery, VM frames, consumed
metadata and confirmed SQL status. The original worker, shutdown, recovery and poll
allocation regressions remain enabled.

The final review reconfirmed the poll bounds: workers cannot grow the main-owned
registries, each terminal or stream entry is inserted at most once, and stale unloaded
entries are skipped. Connection cleanup can only reduce the connection bound. After
reserving, shared-pointer/Event insertion, fixed LogRecord construction, map erasure,
atomic state reads and the noexcept diagnostics paths introduce no further C++
allocation in the reviewed poll path. The bounded per-tick reserve cost remains a
documented tradeoff, without a public API or ABI change.

## Qualification boundary

The final immutable head's seven-job suite and artifact identities are recorded in
the PR description and the owner delivery after completion. The 194ae7f results do
not qualify the new source. No workflow is rerun merely to obtain a passing outcome.
The required final checks include all three Linux modes, both x86/open.mp platforms,
Windows x64 and downloaded package verification, including 21 real scenarios per x86
platform and complete English/frozen-identity enforcement.

Before the final commit, the complete local configured suites passed TSan **31/31**,
ASan/UBSan **25/25** and normal **26/26**, including native callbacks, poll/worker
allocation recovery and network/TLS/PostgreSQL recovery. These builds enable the
component and benchmarks, use PostgreSQL 17.11, stress rounds 5 and sustained seconds
0, and omit only the CI-specific ENOSPC mount. Complete Python passed **139 tests**;
English validation passed with **409 maintained texts and 173 frozen identities**.
They were recorded against modified source based on 194ae7f with input hashes; only
the final clean-head CI qualifies the exact proposed commit.

The separate stale-stream validation branch is reviewed and corrected consistently
but not independently covered by the new runtime scenario. Arbitrary C++ exceptions
inside third-party SDK Push/Exec/PrintError methods, persistent global OOM, libpq or
OpenSSL allocator failure and unrestricted scheduler exploration remain outside this
demonstrated fault model. Detailed diagnostic text may be unavailable during OOM.
No general out-of-memory safety claim is made.

Remaining memory/resource, storage and fuzzing qualifications still precede the
24-hour campaign. The candidate must be formally frozen, infrastructure independently
verified, the pre-24-hour audit completed acceptably and the owner must explicitly
authorize execution. Campaign results require technical review before stable-release
authorization. **The 24-hour campaign remains BLOCKED; no stable release is authorized.**
