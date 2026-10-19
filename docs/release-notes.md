# omp-postgresql 0.1.0-rc.1

First Release Candidate preparation. The version label identifies testing inputs; it
does not establish owner approval or production suitability. Publication requires a
separate authorized action and an exact successful CI run on the integrated commit.

## Included behavior

- C++20 open.mp component with libpq service profiles, exclusive session workers,
  explicit FIFO lanes, immutable parameter snapshots and configurable resource limits.
- 68 Pawn natives for parameterized statements, typed arrays, UNNEST, homogeneous
  executions, heterogeneous batches, transactions/savepoints and bounded streaming.
- Worker-side SQL files and strict named sections, with confined paths, UTF-8 validation,
  complete-file lexical validation and coherent per-job captures.
- Unified `pg_prepare_file(..., section)` keeps whole-file behavior and the old compiled
  `pg_prepare_file` call. `pg_prepare_section` is intentionally removed before 1.0;
  migrate to the final section argument and recompile AMX files requiring that native.
  Old binaries requiring it cannot load against the new component. See [migration](prestable-cleanup.md).
- Optional custom completion handlers on all eleven job-submitting natives, with owned
  bounded integer, Float, boolean and string arguments. Existing `OnPgComplete` remains
  supported; streams retain `OnPgRows` and can customize final completion. See the
  [callback contract and migration rules](callbacks.md).
- Explicit job/item results, ownership by VM generation, cancellation, timeouts and
  reconnect without replaying writes with uncertain outcomes.
- Twelve compiled examples, a progressive [learning path](examples.md) and a complete
  [native reference](native-reference.md). Ten scenarios run unattended; the interactive
  account and custom gameplay tutorials require separate player/provider qualification.

## Corrections and qualification

The completed [GPT-6 Astra ultra audit](independent-audit.md) identified two High/P1
defects. Integrated corrections recover a session after a server-rejected deferred
COMMIT and reject ignored, untracked installation assets before package staging.
Windows packages also include the reviewed downstream libpq mutex correction and its
source patch/build record. No confirmed critical or High finding remains open in that
audit; its unproven observations and coverage limits remain explicit.
That independent audit predates the callback modernization; its internal review and
current test evidence are recorded in the [API audit](api-modernization.md).

The CI matrix requires Engine/native/database tests, Clang 19 TSan, ASan/UBSan, Pawn
compilation with warnings as errors, repeated identical package assembly, dependency
and license checks, and fresh installations of both extracted packages in official
open.mp 1.5.8.3079. See [validation history](validation.md) for exact completed runs,
including failures. New candidate inputs require their own successful CI; earlier
experimental runs are not evidence for a different source commit.

## Platforms and dependencies

Distributions are **Windows x86** and **Linux x86** only. Linux's tested baseline is
Ubuntu 24.04 i386 libraries with the official dynssl server. x64 Engine test builds do
not qualify x64 server components. Packages are not standalone: install the Windows
Visual C++ v14 x86 runtime or the documented Linux i386 system libraries separately.

Each package's manifest inventories actual compiler/libpq versions, SDK/Pawn revisions,
imports, symbol requirements, redistributed licenses and payload hashes. The release
inventory contains both manifests and binds their archives to the exact source ZIP and
SHA-256 ledger. See [dependencies](../DEPENDENCIES.md) and [installation](installation.md).

## Remaining limits and excluded scope

Finite stress and memory samples are not leak bounds or throughput guarantees. Prior
qualification includes a ten-minute, 2,040,672-operation load; hours-long network partition
soaks, AMX loader fuzzing, fully instrumented libpq/OpenSSL and comparative Pawn/tick
latency are not established. Package provenance is an unsigned content inventory,
not a signed build attestation or a hermetic-compilation claim.

COPY, LISTEN/NOTIFY reception, Engine pipeline mode, automatic lane selection, global
SQL caches, ORM, query builders and migration management are outside this candidate.
Keep PostgreSQL credentials outside Pawn/source control and review SQL permissions,
TLS identity verification and uncertain-result handling before use.

## Pre-stable cleanup and diagnostics

Bounded internal logs use `OMP_PG_LOG_LEVEL` (default WARN; OFF disables them), no new
Pawn natives or external dependency. See [operator guide](debugging.md) and
[qualification status](prestable-cleanup.md). Callback, transaction and section semantics
remain supported; prior audit evidence does not certify the new code.

## Unreleased feature branch: configurable outputs and source diagnostics

Adds one native `pg_log_config` (69 total, three forwards), independent levels/destinations,
optional bounded asynchronous file output and 1 MiB/one-archive rotation. Default remains
WARN/console and startup environment support remains. No existing native signature changes.

Named-file errors now identify sanitized relative files, offending sections and original
byte columns/lines where known, with first duplicate lines and bounded corrective hints.
Grammar, capture/preflight, callback and job/result APIs remain unchanged. Server-position
mapping is explicitly deferred; original SQLSTATE/messages remain available. This entry
describes new branch work, not features of the older experimental release or prior audit.
See [design](log-output-design.md), [logging](debugging.md), [sections](sql-sections.md).

## Qualification follow-up: filesystem failures and real Pawn

Adds actual Linux EACCES, positive short-write and isolated tmpfs ENOSPC checks,
including retained SQL errors/results through the same Engine's failed sink.
Adds a bounded official-server Pawn workload and raw timing/resource capture for
console/file/both. No runtime API changes or publication authorization follow.
See [reproduction, measurement definitions and limits](logging-qualification.md).
