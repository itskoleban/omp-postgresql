# Independent GPT-6 Astra audit: omp-postgresql

Date: 2026-10-18 (America/Bogota). Reviewer: **GPT-6 Astra**, reasoning effort **ultra**, the highest available requested setting. This is the completed independent audit report, not a prepared review prompt. Earlier Astra attempts were interrupted before delivering a report; their surviving source probes and logs are identified below and are not represented as completed audits.

## Identity, scope and conclusion

The frozen audited source is **`962ee8ac16ae8cb85b9c879fb27b5209e6898990`**. Source inspection used `git show <SHA>:<path>` even while the parent task implemented fixes in the shared checkout. This report does not conflate that source with the later integrated/format-only `master` commit `556dca6fc3797f1646dab71692d8f5d6d098c5af`, or with CI's preview merge `dfbd7caac35ef21cdc1e896bb3f8c44383acd725`.

**Two actionable defects are confirmed:** stale worker transaction ownership after a rejected COMMIT, and ignored local installation assets passing the package cleanliness gate. Both should be corrected before assembling a release candidate. No critical finding was established. The two narrow proposed corrections were independently reviewed; their final validation is recorded separately below. A synthetic callback-reset observation and a Windows reserved-name portability concern are not counted as confirmed deployed defects.

Reviewed implementation includes the real C++ Engine, Lane/libpq worker, transaction and failure paths, completion/stream ownership, all native wrapper dispatch cases, component VM lifecycle/callback restoration, parameter encoders, SQL file/section parsing, resource configuration, package assembly/verification, CMake, dependency patch/build procedure, and CI qualification checks. The review used the contracts in `docs/design.md`, `api.md`, `native-reference.md`, `security.md`, `types.md`, `sql-sections.md`, `validation.md`, `performance.md`, `packaging.md` and `rc-readiness.md`, plus corresponding tests/examples. This is a source-and-evidence audit, not production certification, exhaustive fuzzing, or a new performance qualification.

## Confirmed findings

### A1 — High / P1: rejected COMMIT leaves a healthy lane unusable until transaction expiry

**Location at the frozen SHA:** `src/session.cpp:549-563`, especially the SQL-error throw at 552-553 before finish-state clearing; related catch logic at 684-695 and reservation release in `src/engine.cpp:479-484`.

**Trigger:** a transaction's statements succeed, but a deferred constraint rejects COMMIT. A temporary table with `UNIQUE DEFERRABLE INITIALLY DEFERRED` and two identical values reliably produces SQLSTATE `23505` at COMMIT. Deferred foreign keys can similarly fail at the finish operation.

**Mechanism:** the worker records the PostgreSQL error result and throws before clearing `transaction_`. The catch marks `transaction_failed_`, but a healthy connection and ordinary SQL error do not cause `drop()`. Meanwhile `Engine::poll()` releases the logical lane reservation for every terminal COMMIT/ROLLBACK. PostgreSQL has returned to idle, but the worker still owns the old transaction. A new ordinary query is accepted by Engine and then rejected by Lane with `Transaction no longer owns this session`; a fresh BEGIN is accepted and then rejected with `Session already owns a transaction`.

**Consequence:** normal expected constraint rejection creates an availability failure on the affected lane. The connection remains reported connected/ready. Work fails until the old total transaction deadline closes/reconnects the session, or the owner explicitly replaces the connection. The default transaction deadline is 60 seconds; it is configurable up to one hour. This reproduction establishes spurious rejection and temporary lane loss, not an uncertain COMMIT, silent durability success, or data corruption.

**Independent reproduction/evidence:** `build/astra-audit/repro.cpp` uses actual `pg::Engine`, worker threads and libpq against the disposable PostgreSQL runner. The original run's `build/astra-audit/out/Testing/Temporary/LastTest.log` records:

```text
deferred commit status=3 error=4 message=Transaction command failed results=1 sqlstate=23505
first post-commit query status=3 error=9 message=Transaction no longer owns this session results=0
next begin status=3 error=9 message=Session already owns a transaction results=0
lane state=2 ready=1
post-deadline query status=2 error=0 message= results=1 sqlstate=
COMMIT_STATE_REPRODUCED=1
```

The isolated `audit_commit` case passed its reproduction assertions in 5.08 seconds with a four-second transaction limit. The surrounding custom runner subsequently failed its required proxy check because its `pg_tests` invocation omitted `--integration`; **that entire custom run is not a passing integration qualification**. The surviving successful defect reproduction and the wrapper failure are separate evidence.

The parent task independently added a normal integration regression and ran it against the implementation before the fix: the complete 15-case CTest run passed 14 cases and failed `integration` with `Transaction no longer owns this session` in 71.06 seconds. See `regression-before-CTest.log` and `regression-before.xml` in the task-local audit evidence directory. This corroborates the source reasoning without relying on the custom runner as a full pass.

**Focused correction:** after retaining a failed COMMIT/ROLLBACK's result, inspect the authoritative libpq transaction state. If it is `PQTRANS_IDLE`, clear worker transaction ownership/failure state. Otherwise discard the physical session so normal recovery cannot reuse a session whose logical reservation is being released. Preserve the original failed job/category/SQLSTATE and never retry the finish operation. Ordinary savepoint failures must retain their transaction semantics.

**Regression required:** deferred UNIQUE (`23505`) and foreign-key (`23503`) failures; failed job/item status, SQL error category, retained SQLSTATE and zero confirmed operations; zero committed rows; an immediate query returning the **same backend PID**; then an immediate new BEGIN/COMMIT. Existing cancelled/uncertain COMMIT and savepoint regressions must continue passing. Same-backend verification prevents a reconnect-only workaround from hiding stale-state handling.

### A2 — High / P1, conditional confidentiality risk: ignored private documents can enter a valid package

**Location at the frozen SHA:** `scripts/package.py:303-322`, especially `git ls-files --others --exclude-standard`; recursive documentation installation at `CMakeLists.txt:287-293`; permissive public-document filename policy in `validate_payload()`.

**Trigger:** a clean tracked checkout contains an ignored, untracked installable file, for example `docs/credentials.md` listed in `.git/info/exclude`. Ignoring a local note is a plausible operator attempt to keep it out of source/distribution. The filename fits the permitted `docs/[a-z0-9-]+.md` payload policy.

**Mechanism:** the first cleanliness check suppresses all untracked files. The second check expressly applies Git's ignore exclusions, so it also suppresses the private file. CMake's directory installation still includes matching Markdown files, without consulting Git's tracked-file inventory. Package assembly enumerates the staged files and hashes them into the manifest; payload verification accepts the extra document. Integrity checks faithfully verify the mistakenly included bytes.

**Consequence:** a supported local package operation can disclose an ignored private document when its resulting archive is distributed. This is conditional on such an asset being present and the archive being shared; no real credential was accessed and no disclosure in the reviewed CI archives was established. Clean ephemeral CI checkouts reduce that particular precondition but do not repair the local package guarantee.

**Independent reproduction/evidence:** `build/astra-audit/sql-supply/repro_ignored_asset.py` loads the frozen package implementation, initializes a small disposable Git fixture, adds only a fake private sentinel, applies the actual matching CMake installation rule, and validates a manifest containing the staged file. It was rerun during this audit completion and printed:

```text
ensure_checkout: ACCEPTED ignored, untracked docs/credentials.md
CMake docs/*.md install: AUDIT_SENTINEL_PRIVATE_INFORMATION
validate_payload: ACCEPTED private document in declared payload
```

This probe verifies the real gate, CMake file installation and payload policy; it is not a claim to have published a credential-bearing package. The parent task's four new ignored-asset regression subcases all failed before the fix because no `ValueError` was raised (`package-regression-before.log`).

**Focused correction:** reject all untracked assets under installation input directories, including ignored files; removing `--exclude-standard` from this scoped `git ls-files --others` check does that. Continue allowing generated/downloaded build inputs outside those directories. This requires no package format, binary or API change.

**Regression required:** ignored fake files under `docs`, `examples`, `pawn` and `scripts/patches` are each rejected; a clean fixture and unrelated build/download artifacts remain accepted. Retain the existing tracked-dirty, inventory, hash and extraction tests.

## Proposed-fix review and validation

The independent reviewer inspected the working diff in `src/session.cpp`, `tests/tests.cpp`, `scripts/package.py` and `tests/test_package.py` after reproductions, while continuing to use the frozen SHA for findings. The proposed changes implement the two focused corrections above. No blocking issue was found in that diff. The transaction result is retained before reconciliation; non-idle or unknown finish state drops the connection; the existing catch/recovery path fails queued work without replay. Clearing ownership in the idle case occurs before terminal publication. The package change stays scoped to installation inputs.

The expanded transaction regression covers both deferred SQLSTATEs, failed status/category/index/item status, rolled-back rows, same-backend immediate use and a subsequent successful transaction. The ignored-file tests cover all four guarded directories. These are behavioral regressions for the reproduced failures.

Observed validation so far:

- Before correction: actual Windows x86 CTest **14/15 passed, integration failed as expected**, with the stale transaction error. This is retained failure evidence, not a qualified pass.
- Package correction: **18/18 package test methods passed**, including the four new ignored-asset subcases; `package-regression-after.log`.
- Complete Python regression discovery after correction: **33 tests passed in 10.217 seconds**; `python-all-after.log`.
- Final local correction run: **15/15 CTest cases passed with no skips in 82.46 seconds**, Windows x86 MSVC 19.51, patched libpq 17.6, disposable TLS PostgreSQL 17.11, stress seed 42. The independent reviewer read `regression-after.log` and checked the current CTest JUnit report's 15-case, zero-failure inventory. The run includes both new deferred-COMMIT cases and the existing transaction/commit-loss, reconnect, native, stream, lifecycle, SQL-section and benchmark-smoke coverage. Each SSL/TLS/startup stall fixture recorded 20 accepted, ready and closed connections; the runner stopped the temporary cluster afterward. This is local correction evidence, not a new sanitizer run or a repeated real open.mp server/package run.
- Exact corrected-commit CI and package qualification remain for the parent task after committing the fix. The frozen audited commit itself remains defective regardless of later successful correction.

## Conditional observations, rejected claims and coverage limits

### Callback reset while the same VM is executing

The frozen `Component::callback()` at `src/component.cpp:84-87` returns without restoring the saved frame if the original owner generation no longer exists. That protects against dereferencing an unloaded VM. `Component::reset()` at 122-127, however, changes owner generations while retaining the same script pointers. A synchronous reset during `Exec()` can therefore trigger the early return while the original VM is still alive.

An audit-only SDK-double probe now **actually builds and runs**, using frozen `component.cpp`/`natives.cpp` and a separately copied core library. The initial probe did not link because changing private/public access changed MSVC's symbol decoration; the completed probe uses a test-only friend accessor without changing production source. `amx/callback-result.txt` records full restoration for ordinary success, `AMX_ERR_SLEEP`, divide error and both partial-push failures, then `same_vm=1 owner_changed=1 restored=0 reports=0` for a directly injected reset during `Exec()`.

This confirms the conditional branch behavior. It does **not** establish that an ordinary supported Pawn action invokes component reset synchronously before that callback returns in official open.mp 1.5.8.3079. The real-server scenarios cover regular GMX/unload and callback sleep/fault handling, not this exact interleaving. Do not present the synthetic probe as a confirmed user-facing crash or change lifecycle identity logic without a reachable server reproduction. A future test should establish the actual reset/unload ordering and distinguish the same live VM from a newly loaded VM at a reused address.

### Reserved Windows archive filename

The frozen verifier accepts `docs/nul.md` when its declared hash and inventory are valid. The earlier `sql-supply/repro_reserved_path.py` claimed successful extraction silently discarded it. On this completion's Windows/Python 3.13.14 host, both the original extraction and a fresh extraction list `nul.md`, and reading each returns all 29 sentinel bytes. The original probe's `assert "nul.md" not in entries` **fails**. Acceptance is observed; silent loss is not reproduced and is not a confirmed finding. Portable device-name rejection could be reviewed separately with an affected supported extractor/OS, but is not required to close A1/A2.

### Native memory and parser boundaries

Native inspection found checked per-cell addresses, alignment/int64 overflow checks, whole-output preflight, a noexcept wrapper, copied input strings/arrays, supported OID checks and terminal-only access to worker-written results. The flat-slot documentation calls for unpacked strings; acceptance of packed input in the generic input helper is not by itself a vulnerability, and no packed-string memory overrun was established. The native unit suite uses an SDK test double; it does not fuzz the AMX loader or prove safety against corrupted VM internals.

SQL section review covered nested comments, escaped and concatenated strings, dollar-delimiter matching, physical-line declarations, whole-file duplicate/empty/malformed rejection, UTF-8/NUL, source/selection capture budgets and worker-time capture. File confinement uses canonical paths and regular-file/size checks. Administrator symlink replacement races and hostile operator-controlled SQL remain outside the documented trust boundary. No additional reproducible SQL-section or native memory defect was established by this review; that is narrower than a proof of absence.

### Concurrency, transport, memory and performance

The reviewed ownership model keeps each PGconn and its RAII destruction on one worker; queue mutation is locked; terminal status publishes results; main-thread registries and callback dispatch do not cross into workers. Preparation is invalidated on direct mode/reconnect/appropriate server-plan failures. Reconnect drains pending jobs without replay, transaction finish capacity is reserved, and streaming has separate pending/current/producer ownership with cancellation/deadline checks. Review of these invariants exposed A1 but did not establish a separate data race or replay defect.

Finite sanitizer/stress passes do not prove all schedules safe. The actual Clang 19 TSan configuration instruments project code, not system libpq/OpenSSL. The recorded 600-second soak and 2,040,672 workload SQL operations provide finite duration, seed, counter and ownership/capacity evidence; process high-water memory is not a leak bound. Comparative Pawn callback/tick latency, fully instrumented dependencies, hours-long network partitions/soaks, systematic allocator-failure injection and AMX loader fuzzing remain unqualified. Shutdown still depends on library/OS operations such as DNS/authentication/filesystem calls, as documented. No architecture overhaul or unsupported performance conclusion follows from this audit.

## Evidence provenance and release boundary

- The parent task supplied successful exact-branch CI run **39041936114**, seven jobs, using preview checkout `dfbd7caac35ef21cdc1e896bb3f8c44383acd725`. `build-api-ci-artifacts/evidence.json` records the audited branch SHA separately from that preview SHA, artifact IDs, both archive manifests and hashes. The manifests explicitly identify preview-merge bytes; they are not relabeled as source-commit `962ee8a` bytes.
- The local exact-source Windows package evidence is `build/api-contract-review/package-evidence.json`: archive `omp-postgresql-0.1.0-experimental.962ee8ac16ae-windows-x86.zip`, SHA-256 `364908dd65c67cf417876f7079b0d7b0069d19adaa9d3814a8a2a2a9f2c9385b`, repeat-identical assembly, and a successful fresh official open.mp 1.5.8.3079 packaged run. The corresponding prior core qualification is `qualification-962ee8a.xml` (15 cases, 69.16 seconds per supplied run evidence).
- `baseline-ci.json` records the **older** master run 39020102014 at `3a5215d03f6d00b70c4ffc8b10cab28f119a5c00`; it is contextual baseline evidence only. Existing API evidence covers 69 signatures, 15 configuration ceilings, 69 compiled expression examples and six complete Pawn examples. Those checks do not establish every runtime branch.
- Package source review verified scoped installation, x86 Release checks, checksums/inventory, dependency/license rules, the Windows libpq mutex correction and its source/DLL/patch hash record, and extracted-package tests. The hash record is not a signed attestation; deterministic ZIP assembly is not hermetic compilation. CI uses read-only repository permissions and runs qualification that rejects skipped or missing configured CTest cases.

This review did not merge, tag, publish, send messages to other chats, operate an existing database, or certify production readiness. Corrections need exact corrected-commit CI and regenerated package qualification before an RC decision; authorizing a review or a successful test run does not authorize publication.

## Correction tracking by the implementation task

The independently reviewed correction is saved as commit
`4c65070c2088b3afea4313182872a1bf8b1f500c` on
`codex/audit-transaction-recovery`, based on owner-integrated `556dca6`.
[PR #13](https://github.com/itskoleban/omp-postgresql/pull/13) contains the two bounded fixes,
behavioral regressions and contract updates. The report above reviews frozen `962ee8a`
and the correction diff; it does not retroactively declare that original source corrected.
Final CI/package evidence must identify its own exact checkout/manifest commit.

## Bounded RC tooling review, October 18, 2026

A second, actual independent GPT-6 Astra **ultra** review examined the RC preparation
changes against integrated baseline `c1f4d48380ca13a1c497d1a1eb23f81a49692f30`.
It reviewed the new source/package/CI/tag binding, version metadata, release bundle,
manual publication workflow, permissions, tests and documentation. This was a bounded
review of the new tooling, not a repeated runtime audit or public-release authorization.
The implementation task records the final committed identity and CI separately.

- **R1, High/P1, corrected:** `release.yml` initially relied on `gh release create
  --verify-tag --target` after an earlier tag check. For an existing tag the API ignores
  target_commitish; an authorized operator moving the tag between jobs could attach
  commit A's packages to commit B's tag. The write job now remotely resolves lightweight
  and annotated tags immediately before draft creation and again before publication.
  `release.py check-remote-tag` rejects changed/deleted tags, unsupported objects and
  excessive nesting. Regressions mock these API conditions. This is not an established
  unauthorized attack; tag protection/owner coordination still governs the non-atomic
  privileged-mutation window described in [releasing](releasing.md).
- **R2, Medium/P2, corrected:** generated standalone `RELEASE-NOTES.md` retained five
  relative documentation links whose targets were absent beside that generated file
  and in the GitHub release body. An actual preparation probe confirmed all five missing
  targets. Preparation now rewrites those links to GitHub blob URLs pinned to the exact
  source commit; installed documentation remains relative. Regressions check all five
  destinations and preserve source-document content.

Astra independently reviewed both corrections and reported **no new blocking finding**.
Its actual correction run passed all eight release test methods in **16.446 seconds**,
including disposable real Git/source/tag fixtures, package verifier gates, repeatable
bundles and both regressions. Evidence remains in the automation workspace under
`build-rc/astra-review/correction-release-tests.log`; initial sandbox temporary-directory
ACL failures were environmental and are not passing test evidence. No release workflow
was invoked and no project tag, release, merge or repository protection was changed.
