# Transaction finishing identity reservation review

Date: 2026-11-04, America/Bogota. Baseline: `origin/master` source
`3a5df8439881bd514727a2c5e360f9ec0bafaf43`. Proposed change is review-only until its
PR is approved and merged. No stable tag, release, or 24-hour campaign is authorized.

## Decision

**A correction is required before v0.1.0 release.** The baseline protects a retained
job/result slot for every live BEGIN but does not protect identifier capacity. The
baseline can accept BEGIN with identifier `2147483646`, then reject both COMMIT and
ROLLBACK because the shared cursor has reached `2147483647`. Closing the connection
will eventually terminate the session, but that is not a substitute for the promised
explicit finishing admission capacity.

The constant-time negative control compiled the baseline Engine and baseline Engine
header extracted with `git show`, while substituting a deterministic Lane admission
model. It reproduced exactly that admission defect, without a server, network, or SQL:
`BEGIN=2147483646; next=2147483647; finish=Error::limit; job_capacity=available`.
See [control output](transaction-baseline-control.txt) and
[reproducer source](transaction-baseline-reproducer.cpp). The compiled control resides
at `build/transaction-baseline-control/reproducer.exe`; it is a diagnostic binary, not
a release artifact. A zero control exit status means **defect reproduced**, not baseline
qualification PASS.

## Minimal implementation

Only private Engine allocation/bookkeeping code and its focused test registration
change. Pawn signatures, Id width, jobs, worker execution, terminal classifications,
and the positive handle range are preserved. No emitted identifier is recycled and
the global cursor never decreases.

Let `U = INT32_MAX - next_` be the number of still-unallocated identifiers and `R` the
number of reserved lanes whose transaction is not in `ending_`:

- Every ordinary allocation requires `U > R`.
- BEGIN requires `U > R + 1` before advancing the cursor for the BEGIN job. After
  admission, its existing lane reservation also protects one free finishing identity.
- `finish()` allocates its ending marker before submitting. That marker excludes its
  own credit from R, allowing its finisher to use that capacity while protecting every
  other transaction's credit.
- A finisher previews the next identity without advancing the global cursor. Allocation
  of item metadata and registration in `jobs_` occur before enqueue. The cursor advances
  once, only after successful queue admission. An unaccepted preview is never issued to
  the public API. A retry can prepare that same still-unallocated value; this is not
  reuse of an emitted or burned identifier.
- Ordinary jobs preserve baseline behavior: failures after their identity was allocated
  can burn that identity. BEGIN rejection after allocation burns its BEGIN ID but creates
  no finishing credit. No unused future identity is burned by merely reserving capacity.

`finishing_slots()` centralizes the existing scan instead of adding an independent
credit counter or identity registry. It is also used by job/result capacity checks.
This prevents a second source of lifecycle bookkeeping that could drift or double-free.
The default profile has at most 8 connections by 8 lanes; allocation now scans those
reservations for non-job handles too. Broader performance qualification remains pending.

## Exception and lifecycle argument

All Engine methods and allocation state are main-thread owned. Production
`Lane::enqueue` holds its mutex, rejects a stopped worker before pushing, and performs
`queue_.push_back(shared_ptr)` before only atomic accounting and condition-variable
notification. `shared_ptr` copying is nonthrowing. A deque insertion allocation exception
leaves that insertion unaccepted. There is no Engine callback or reentrant allocation
inside production enqueue, diagnostics emit, or the optional network trace.
Consequently a throwing enqueue has not accepted the job, and a successful enqueue
cannot throw later in this path. The final cursor increment is bounded, nonallocating,
and nonthrowing, even if a worker has already begun processing the accepted job.

If make_job, ending-map insertion, items_status allocation, job-map insertion, readiness,
job limit, lane ownership, or queue admission rejects a finisher, the global cursor
has not advanced. `finish()` removes its ending marker; its lane still reserves the
transaction and capacity. A queue rejection after registration also erases that job.
Insertion failures have no partial unordered-map entry. Local shared ownership unwinds
normally; no accepted network operation is rolled back or replayed.

An unsuccessful BEGIN completion clears the lane reservation when polled. Every
finisher completion clears the reservation and ending marker under the existing rules,
regardless of CONFIRMED, FAILED, UNCERTAIN, or CANCELLED. No terminal classification or
SQL metadata is rewritten by identity management. Closing or unloading a connection
keeps its credit conservatively while its reservation remains registered; reaping the
stopped connection removes the capacity-only credit. Late finish publication removes
its ending marker even after its connection or unloaded job has been reaped. The
existing independent completion cleanup remains unchanged. Destructor close/join needs
no new allocation or credit release operation; destroying the Engine destroys its whole
nonrecycling namespace.

An interrupted/expired physical transaction may still need an explicit finish attempt
to retire its logical reservation. The reserved identity permits that attempt; it does
not change whether the worker reports definitive rejection or uncertainty. Existing
transaction deadlines, disconnect, and unload remain the fallback containment measures.

## New evidence and limits

The local build used Windows UCRT64 g++ and the existing `build` configuration. CMake
rebuilt `pg_core` and the four focused targets. `build/package-build-info-.json`
records baseline HEAD `3a5df8439881bd514727a2c5e360f9ec0bafaf43`, GNU 16.1.0,
64-bit Windows, and libpq 18.4. Its generated metadata does not encode this dirty
working-tree patch; the source and binary hashes in the artifact index supply that
missing local provenance. These are **baseline HEAD plus local patch test builds**,
not frozen final-candidate builds. The metadata's source-controlled `stable` channel
string confers no qualification or release approval. No Docker container or real SQL was
started. CTest's original detail output uses Windows CP1252; its reviewable .txt copies are
converted reversibly from CP1252 to UTF-8, with CRLF normalized to LF before hashing.
Raw initial .log copies and the final .raw.log are preserved locally without rewriting
and separately hashed; ignored raw .log files must also be copied into the eventual
evidence archive. The final original CTest log remains in `build/Testing/Temporary`.

Initial focused CTest selection: **4/4 PASS**, 2.41 seconds:

- `transaction_identity_unit`: 23 deterministic Engine scenarios.
- `lane_admission_unit`: five actual Lane worker/startup/admission cases using libpq
  seams; its `commands=1` counter describes a stubbed dispatch, not real SQL.
- `transaction_retirement_unit`: six independent terminal/reaping snapshots.
- `unit`: 32 existing unit checks.

See [initial summary](transaction-focused-ctest.txt) and
[initial details](transaction-focused-ctest-details.txt).

A new high-water probe case then verified successful create_batch/close with one free
ordinary identity plus an active finishing credit, followed by rejection of another
probe and successful use of the final identity by ROLLBACK. Only the changed identity
test was rerun: **24 scenarios PASS**, CTest 0.31 seconds. See
[final summary](transaction-identity-final.txt) and
[final details](transaction-identity-final-details.txt). This run uses the final formatted
test and Engine source. Other targets were rebuilt after format-only normalization;
no additional Engine semantic edit followed their initial passing run.

The 24 scenarios cover one/two IDs remaining, every ordinary allocating entry category,
statement batch restoration, both finishing commands across all four terminal statuses,
multiple transactions and owners, before-acceptance bad_alloc and not_ready rejection,
unsuccessful BEGIN cleanup, shutdown/unload before and after finishing admission, late
publication, exhaustion without wrap, and the high-water probe. Allocation injection
sweeps every allocating call before first successful admission: five FINISH allocations
and four BEGIN allocations on this toolchain. This covers job creation, ending-map
node/buckets, item-vector storage, job-map insertion/buckets, and retry state. There is
no production failure-injection hook added.

The new test uses the **actual Engine** with a deliberately modeled, single-threaded
Lane. It does not prove PostgreSQL semantics, real SQL outcomes, real transport failures,
or worker scheduling. Terminal outcomes in that test are controlled publication
snapshots. The existing actual-Lane admission regression supplies the separate
throw-before-admission check. No live PostgreSQL, ASan/UBSan, TSan, Linux, or production
component runtime result is claimed by these local runs.

The allocation test's custom new/delete delegate to malloc/free, allowing ASan's
malloc/free interceptors to remain active; actual ASan/UBSan qualification of this
change is still **PENDING**. Under `PG_THREAD_SANITIZE`, CMake sets
`PG_TEST_ALLOCATIONS=0`: custom global replacements are absent, both allocation sweeps
explicitly print **NOT RUN**, and 22 non-allocation identity scenarios remain. A passing
TSan invocation must not be reported as allocation-fault coverage. This model itself
has no worker concurrency; actual-Lane and integration TSan tests remain necessary.

`git diff --check` passed; clang-format checks passed for the new test and Engine
header. Existing unrelated Engine formatting was not rewritten. Binary/source/log
SHA-256 values are in [the identity artifact index](transaction-artifacts.json).

## Evidence invalidation and required approval

This is an Engine change. Baseline qualification remains preserved and valid for
its original source/dependencies/configuration. Its Engine-specific portions cannot
be promoted unchanged to qualifying evidence for the proposed new Engine candidate.
The existing 100,000-case semantic fuzz tests exercise unchanged helpers rather than
Engine submission; retain that evidence after checking helper/dependency hashes. Do
not rerun that extended campaign solely because this patch changes the Engine.
Before a new freeze: approve and merge the PR; record the exact new SHA/tree; rebuild
Windows/Linux component binaries and packages; verify dependency/loading manifests,
Pawn signature compatibility, and SHA-256 correspondence; rerun changed-Engine CI,
admission tests and relevant transaction, cancellation, network/TLS,
worker/unload/lifecycle, allocation/storage/sanitizer and stress/preflight qualification.
Use ordinary CI fuzz coverage; extended helper fuzzing needs repetition only if a
specific helper/dependency/environment contract changes.
The 600-second calibration and real supervisor deployment validation must then use
the newly frozen artifacts. Historical launcher/supervisor offline tests may be reused
where those scripts and their contracts are unchanged; they are not real deployment
PASS evidence.

No general audit or successful unrelated campaign was repeated here. Full frozen
candidate CI, live tests, actual sanitizers, calibration, 24-hour campaign, and stable
release are **NOT RUN / PENDING** for this proposed change. This correction does not
resolve Issue #31, and the historical cleanup timeout's cause remains **UNKNOWN**.

## Follow-up: portable allocation-sweep assertion

This section supersedes the assertion-portability assumption in the original local
snapshot above. The original report and manifest are preserved byte-for-byte as
[the 83edd05 report snapshot](transaction-identity-review-83edd05.md) and
[the 83edd05 artifact snapshot](transaction-artifacts-83edd05.json). The original
`transaction-artifacts.json` remains untouched; new hashes are recorded separately
in [the portability artifact index](transaction-artifacts-portability.json).

PR #55 head `83edd0551e819a60142fc88e6c30b98e48ad271d` had a real Windows x86 CI
**FAIL** in job `127036554837`, run `41963848824`:
[original failed job](https://github.com/itskoleban/omp-postgresql/actions/runs/41963848824/job/127036554837).
The test printed `FINISH denied_sites=4 PASS`, then rejected the BEGIN allocation
sweep using its hard-coded `denied >= 4` minimum. The BEGIN count was not printed
before that rejection, so its exact count is not claimed. The allocator/STL is free
to implement a valid operation with fewer allocating calls than the local UCRT64
build. That test assertion was not portable. The failed CI result and original raw
job log remain preserved; this change does not relabel them PASS or attribute the
failure to an Engine defect.

Only the test's success guard changes. The sweep still starts at countdown zero,
increments exactly once after each failed allocation, checks rejection/registry/
reservation/cursor invariants at every failed position, and stops only at its first
successful admission. On that success it now requires one accepted job, at least
one denied allocation, and `denied == countdown`. No fixed toolchain call count is
required. Its 32-position watchdog and all state checks remain intact. Engine,
header, CMake, worker code, and public signatures are unchanged by this follow-up.

Only `transaction_identity_unit` was rebuilt and rerun locally for this test change:
**24 scenarios PASS**, CTest total 0.41 seconds (test body 0.38 seconds). The UCRT64
run still observed FINISH five and BEGIN four denied positions. See
[portability test summary](transaction-identity-portability.txt) and
[portability test details](transaction-identity-portability-details.txt). No other
successful suite was repeated, and no SQL, Docker, or prolonged load was started.
The raw CTest detail copy remains local as `.raw.log`; its readable copy uses the
same reversible CP1252-to-UTF-8 and CRLF-to-LF normalization as earlier evidence.

The new local test executable is a parent-83edd05 build plus this uncommitted test
patch, not a frozen release artifact. New source/binary/log hashes and the preserved
CI failure hash are in the separate portability index. Windows x86 CI confirmation
of this correction is **PENDING** until the new PR commit runs; local UCRT64 PASS
alone does not establish MSVC PASS. Existing production/campaign blockers remain.
