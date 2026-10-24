# omp-postgresql 0.1.0

**Unpublished qualification source.** Stable publication remains blocked by the final
qualification report. No tag or release is created by preparing these notes.

## Historical RC.1 (unchanged public artifacts)

[v0.1.0-rc.1](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.1)
was published on October 21, 2026 as a testing Pre-release, not a stable or
production-certified release. Its source is `db2bfb9b816b0042b8ef36e847cf9fb1c24e94fc`;
all seven jobs passed in [CI 39515353213](https://github.com/itskoleban/omp-postgresql/actions/runs/39515353213).
The older experimental release retains its own source identity. These repository notes
may evolve after publication; the RC's tag, uploaded notes and assets remain unchanged.

## Included behavior

- C++20 open.mp component with libpq service profiles, exclusive session workers,
  explicit FIFO lanes, immutable parameter snapshots and configurable resource limits.
- **69 Pawn natives and three callbacks** for parameterized statements, typed arrays,
  UNNEST, homogeneous executions, heterogeneous batches, transactions/savepoints,
  bounded streaming and diagnostic configuration.
- Worker-side SQL files and strict named sections, with confined paths, UTF-8 validation,
  complete-file lexical validation and coherent per-job captures. Parser errors identify
  sanitized relative files, sections and original lines/byte columns where known,
  including first duplicate locations and bounded corrective hints.
- Unified `pg_prepare_file(..., section)` keeps whole-file behavior and the old compiled
  `pg_prepare_file` call. `pg_prepare_section` is intentionally removed before 1.0;
  migrate to the final section argument and recompile AMX files requiring that native.
  Old binaries requiring it cannot load against the new component. See [migration](prestable-cleanup.md).
- Optional custom completion handlers on all eleven job-submitting natives, with owned
  bounded integer, Float, boolean and string arguments. Existing `OnPgComplete` remains
  supported when no custom handler is selected; streams retain `OnPgRows` and can customize
  final completion. See the [callback contract and migration rules](callbacks.md).
- Explicit job/item results, ownership by VM generation, cancellation, timeouts and
  reconnect without replaying writes with uncertain outcomes.
- `pg_log_config` selects bounded console/file/both diagnostics. Startup defaults to
  WARN/console, with `OMP_PG_LOG_LEVEL` support; the last valid Pawn configuration applies
  component-wide. File output uses an asynchronous writer, 1 MiB rotation and one archive.
  Acceptance does not guarantee an opened file or durable delivery; bounded queues can
  drop records with accounting. See [logging operations](debugging.md) and [design](log-output-design.md).
- **Thirteen compiled examples**, a progressive [learning path](examples.md) and a complete
  [native reference](native-reference.md). Eleven public examples run unattended; accounts
  and custom gameplay require separate player/provider qualification. The real Pawn
  measurement fixture is separate from the public example inventory.

## Integrated corrections and verified behavior

The completed [GPT-6 Astra ultra audit](independent-audit.md) identified two High/P1
defects. Integrated corrections recover a session after a server-rejected deferred
COMMIT and reject ignored, untracked installation assets before package staging.
Windows packages include the downstream libpq mutex correction and its source patch/build
record. No confirmed critical or High finding remains open in that bounded audit; its
unproven observations and coverage limits remain explicit. It predates callback
modernization and does not certify later changes; see the [API audit](api-modernization.md).

Configurable logging/source diagnostics and the filesystem/Pawn qualification follow-up
are integrated through PRs [#19](https://github.com/itskoleban/omp-postgresql/pull/19) and
[#20](https://github.com/itskoleban/omp-postgresql/pull/20). They are included in this
candidate, but were not part of the older experimental release.

The [verified master baseline](validation.md#verified-master-baseline-october-20-2026-utc)
is `71cdf6f19f00534fd9748286673554e68cebe195`: all seven CI jobs succeeded, with
104 executed CTest cases and no failures/errors/skips across six configurations.
Original artifacts establish source/package identity, thirteen-example compilation
with warnings as errors, and official open.mp 1.5.8.3079 source/extracted-package checks
on both x86 platforms. ASan/UBSan and Clang 19 TSan reported no diagnostic findings;
system dependencies were not fully instrumented.

Actual Linux EACCES, positive one-byte resource-limit short writes and isolated tmpfs
ENOSPC were executed. The ENOSPC case also preserves SQL errors, SQLSTATE, results and
subsequent queries through the same Engine while its file sink fails. Four real Pawn
captures validate 6,144 completed/closed jobs each and record callback/timer scheduling,
whole-process CPU/RSS and per-sink drop reports. Counted event losses occurred; no
observed sink failures in those captures does not establish lossless delivery.
See [methods and limits](logging-qualification.md) and [historical measurements](performance.md).

These results qualify that source only. The final documentation merge and its packages
passed their own exact-master CI before tagging, followed by the verified
[release workflow and public asset checks](validation.md#published-release-candidate-october-21-2026-utc).
PR/branch CI and changes made after publication remain separate evidence.

## Platforms and dependencies

Distributions are **Windows x86** and **Linux x86** only, tested with official open.mp
1.5.8.3079. Linux's tested baseline is Ubuntu 24.04 i386 libraries with the dynssl server.
x64 Engine tests do not qualify x64 server components. Packages are not standalone:
install the Windows Visual C++ v14 x86 runtime or documented Linux i386 system libraries.
Source builds require CMake 3.24+, Git, a C++20 compiler and libpq 17+ with TLS/thread
support, all matching the target architecture and ABI.

Each package's manifest inventories actual compiler/libpq versions, SDK/Pawn revisions,
imports, symbol requirements, redistributed licenses and payload hashes. The release
inventory binds both manifests and archives to the exact source ZIP and SHA-256 ledger.
See [dependencies](../DEPENDENCIES.md) and [installation](installation.md).

## Remaining limits and excluded scope

Finite stress and memory samples are not leak bounds or throughput guarantees. The
historical ten-minute, 2,040,672-operation load retains its original source identity.
Hours-long network/logging soaks, AMX loader fuzzing, fully instrumented libpq/OpenSSL,
physical storage faults, Windows kernel ENOSPC, privileged reparse races and network
filesystems remain unqualified. A stalled OS filesystem call can delay safe writer join
indefinitely; there is no hard shutdown deadline, fsync or durable-flush guarantee.
Real Pawn callback/timer measurements do not isolate component tick execution cost,
establish per-destination resource bounds or compare performance with other components.
Interactive player/account/provider behavior requires operator validation.

PostgreSQL server-position mapping is deferred; original SQLSTATE/messages remain
available. COPY, LISTEN/NOTIFY reception, Engine pipeline mode, automatic lane selection,
global SQL caches, ORM, query builders and migration management are outside this candidate.
Package provenance is an unsigned content inventory, not a signed build attestation or
hermetic-compilation claim. Keep credentials outside Pawn/source control and review SQL
permissions, TLS identity and uncertain-result handling before deployment.
