# Allocation-free owner teardown and transaction retirement

Base: `e371ccb3a0dd01cca5dc92b197250de7f55226f2`, merged PR #35.
No qualification PR was open at start. Continue in PR #36 on
`qualify/v0.1.0-resource-lifecycle`. Runtime and regression source commit:
`c612e9f1ed258d081d91fd929c5333bf77b78d28`.
`manifest.json` binds the local observations to exact source hashes. Subsequent
evidence-only commits do not change those sources; the PR must record the final
head and its exact-head hosted qualification separately.

## Confirmed defects and corrections

`Engine::unload` allocated an owner tombstone before disconnecting connections,
removing statements/batches, requesting cancellation or suppressing callbacks.
Failure of that first allocation escaped teardown. `Component::onAmxUnload`
then could not erase its AMX registry entry. The negative unit control denied
main-thread allocation and observed `denied=1 escaped=1`, exit 1. This establishes
the Engine failure; the retained dead-VM risk follows from the component caller's
control flow, rather than an observed VM dereference.

Replace the allocating owner list with a main-thread-only `Job::owner_unloaded`
flag. Unload marks existing jobs and retires all the same resources without
allocating. The worker and terminal queue retain shared ownership until publication
and consumption, including when terminal status is visible before queue insertion.
Supported AMX owner generations remain unique. No worker reads the new flag.

`Engine::poll` also retained COMMIT/ROLLBACK `ending_` markers if their connection
had already been reaped, or an unloaded terminal job had been erased before its
completion was consumed. Repeated teardown could accumulate these map entries.
Reclaim finish markers from the completion independently of both registries, and
when retiring an unloaded terminal job before publication. Never clear unrelated
live transaction markers or change SQL terminal metadata.

Six controlled snapshots exercise the actual `Engine::poll`: COMMIT and ROLLBACK
with a retained finish and missing connection, an already-retired job, and an
unloaded terminal job before queue insertion. All six fail before the marker fix
and pass afterward. These are deterministic reachable-state tests, not a forced
worker-scheduling experiment. The private test accessor introduces no runtime
timing hooks or public methods.
`marker-before.patch` restores only the previous marker cleanup on the corrected
source, allowing the six-case negative control to be reproduced in a disposable
checkout without reverting the independent owner-unload correction. Apply it
with `git apply --unidiff-zero`; it contains zero context to preserve tabbed source
lines without treating patch context prefixes as source indentation.

## New regression scope

- A denial-control allocation proves the test allocator is active. Repeated empty
  owner unloads must perform zero allocation attempts and throw no exception.
- A disposable PostgreSQL observer confirms the exact active `pg_sleep` before
  unloading an owner with terminal, active and queued jobs, callback descriptors,
  a statement and a batch. Allocation remains denied only during `unload`.
- Verify cancellation, callback suppression and descriptor release, resource
  retirement, active UNCERTAIN and queued CANCELLED outcomes, exactly two SQL
  commands with no replay, an independently retained confirmed result, and an
  unaffected second owner's work and resources.
- TSan retains its allocator interceptors and uses distinct `owner_unload_*`
  lifecycle tests. Their `TSAN_RETAINED` output makes no global-denial claim.

## Local validation and original negative evidence

Toolchain: Windows x64, GCC 16.1.0 / MinGW UCRT64, C++20, Debug,
PostgreSQL/libpq 18.4. Core-only CMake build: PASS. Both new standalone tests also
compile with `-Wall -Wextra -Wpedantic -Werror`; both allocator modes compile.
No new local open.mp or sanitizer execution is claimed.

- New CTest cases: PASS 3/3, including real PostgreSQL owner-unload integration.
- Python regression suite: PASS 143/143, zero skips.
- Documentation SQL: PASS in both disposable local clusters; no Pawn runtime claim.
- Clean base full CTest: **FAIL**, 16/19 pass, zero skips. `integration`,
  `types_integration` and `extensions_integration` failed.
- Corrected source full CTest: **FAIL**, 20/22 pass, zero skips. `integration`
  and `types_integration` failed. The extension pass does not explain its earlier
  connection deadline. The new lifecycle tests and network recovery pass do not
  qualify the failed overall suite.

`baseline-negative.zip` and `retirement-negative.zip` preserve complete console,
command, CTest/JUnit and PostgreSQL logs. Their companion JSON files list hashes
of the original archive members; `manifest.json` also records archive identities.
Raw Windows captures retain localized tool output inside these archives, as
historical evidence; maintained explanatory prose is English. No database data,
private TLS keys, player credentials or production material is included.

At `2026-10-26 01:39:23.141 UTC`, the corrected-source server log reports backend
exception `0xC0000028` while running `SELECT 1; SELECT 2`, followed by termination
of other backends and server reinitialization. A fresh isolated, verified-TLS
libpq-only control reproduces this with no component code linked: initial SELECT
passes, a single `PQsendQueryParams("SELECT 1; SELECT 2")` receives no expected
`42601` within 2003 ms (exit 2), and the server records the same backend exception.
A fresh connection's SELECT passes after recovery. The cluster is stopped and its
`postmaster.pid` is absent. `direct-libpq-stacked-control.zip` preserves source,
runner, manifest and raw logs, excluding data and TLS keys (SHA-256
`113846a1d0f6d486bb6c004076ca3f25ac2c9c5777e9c99538ff06d675b544f5`).
This isolates a failure of this local server installation without demonstrating
a general PostgreSQL 18.4 defect or an underlying compiler/OS cause. Server-wide
termination overlaps `types_integration`, but its exact failed SQL was not logged;
no claim that all possible type failures are thereby explained is justified.
Do not weaken the stacked-query rejection, timeouts, type checks or no-replay
assertions to make this local installation pass.

## Continuation and release gates

The prior PR #35 head passed hosted run `40252327804`; its base master passed
`40251830291`. Current master `e371ccb` passed hosted run `40254135260`.
The runtime/test candidate `c612e9f` passed hosted run `40255510980`, attempt 1:
all seven jobs, 194/194 configured CTest executions with zero skips, 23/23 real
sessions on each x86 platform, Pawn compilation and independent downloaded-package
verification. `hosted-ci-c612e9f.json` records exact jobs, artifact IDs/digests,
direct JUnit and source/provenance verification. The six downloaded log archives
match GitHub's byte lengths and SHA-256 values. The earlier push run `40255252957`
is CANCELLED, not passing evidence. Evidence-only descendants need their own
reported CI disposition; never silently substitute their SHA for this candidate.

Phases 1 and 3 receive concrete runtime fixes; phase 5 gains three regressions and
phase 6 gains preserved local evidence. Earlier SQL-read, token, parameter and
batch fixes remain covered by their hosted suites. The newly requested first
independent Astra review of this exact candidate is now complete; see
[the full report](astra-first-review.md). It confirms no new blocking defect and
permits campaign preparation within its stated limits. It does not establish
prolonged stability or authorize a campaign, merge or release.

Issue #31 remains OPEN / ROOT CAUSE UNKNOWN; original historical network FAIL
`40164306733` is unchanged. The Linux logging observation preserved in PR #32 also
remains causally unresolved. None of these changes explains either observation.
The first independent Astra audit (`gpt-6-astra`) started after the full candidate
gate. Following an initial usage-limit interruption, it resumed and completed on
October 26. Its report includes a dependency/binary-identity addendum. Linux's
build-tree component and installed package have distinct hashes and distinct test
coverage; installation logs record a runtime-path change without proving that this
alone explains every differing byte. Exact manifests and dependency hashes are in
`candidate-dependencies.json`; campaign preparation gates are in
`stability-preparation.json`.
The 24-hour campaign requires separate express authorization and isolated
infrastructure verification. Stable publication requires final owner approval.
No master mutation, merge, branch deletion, tag, release or production use occurs.
