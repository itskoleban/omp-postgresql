# omp-postgresql 0.1.0-rc.1

First Release Candidate preparation. The version label identifies testing inputs; it
does not establish owner approval or production suitability. Publication requires a
separate authorized action and an exact successful CI run on the integrated commit.

## Included behavior

- C++20 open.mp component with libpq service profiles, exclusive session workers,
  explicit FIFO lanes, immutable parameter snapshots and configurable resource limits.
- 69 Pawn natives for parameterized statements, typed arrays, UNNEST, homogeneous
  executions, heterogeneous batches, transactions/savepoints and bounded streaming.
- Worker-side SQL files and strict named sections, with confined paths, UTF-8 validation,
  complete-file lexical validation and coherent per-job captures.
- Explicit job/item results, ownership by VM generation, cancellation, timeouts and
  reconnect without replaying writes with uncertain outcomes.
- Six compiled/runnable examples and a complete [native reference](native-reference.md).

## Corrections and qualification

The completed [GPT-6 Astra ultra audit](independent-audit.md) identified two High/P1
defects. Integrated corrections recover a session after a server-rejected deferred
COMMIT and reject ignored, untracked installation assets before package staging.
Windows packages also include the reviewed downstream libpq mutex correction and its
source patch/build record. No confirmed critical or High finding remains open in that
audit; its unproven observations and coverage limits remain explicit.

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
