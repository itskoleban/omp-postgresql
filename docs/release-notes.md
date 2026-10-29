# omp-postgresql 0.1.0-rc.2

## Overview

RC.2 is a testing Release Candidate for the asynchronous PostgreSQL component for
open.mp. It incorporates runtime corrections, qualification coverage and release
engineering integrated since RC.1. It is intended for testing and evaluation, not
production certification. No stable `v0.1.0` release is established by these notes.

The historical [RC.1 Pre-release](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.1)
was published on October 21, 2026 from `db2bfb9b816b0042b8ef36e847cf9fb1c24e94fc`.
Its tag, notes and assets remain unchanged. The
[RC.1-to-RC.2 comparison](https://github.com/itskoleban/omp-postgresql/compare/v0.1.0-rc.1...v0.1.0-rc.2)
identifies the changes when the qualified RC.2 tag is created. Use the
[releases page](https://github.com/itskoleban/omp-postgresql/releases) to confirm availability;
preparing a branch, CI artifact or these notes does not itself publish a release.

## Changes since RC.1

### Runtime corrections

- Preserve cancellation and timeout causes across socket-wait races, distinguishing
  interruption before dispatch from a potentially uncertain SQL outcome after dispatch.
- Publish accepted jobs without further allocation during worker recovery and shutdown,
  retain FIFO ownership, and preserve completion delivery when diagnostic allocation fails.
  These targeted corrections do not guarantee recovery from arbitrary out-of-memory faults.
- Bound SQL-file reads and enforce size limits when a file grows during reading.
  Handle allocation uses directly tested checked 32-bit counters.
- Make owner teardown allocation-free and retire transaction finish markers with their
  resources. When an earlier transaction failure causes COMMIT to be rejected locally,
  keep the substitute ROLLBACK from becoming a confirmed COMMIT if result retention
  encounters an allocation failure.

These runtime changes are included in PRs
[#24](https://github.com/itskoleban/omp-postgresql/pull/24),
[#25](https://github.com/itskoleban/omp-postgresql/pull/25),
[#30](https://github.com/itskoleban/omp-postgresql/pull/30),
[#32](https://github.com/itskoleban/omp-postgresql/pull/32),
[#33](https://github.com/itskoleban/omp-postgresql/pull/33),
[#36](https://github.com/itskoleban/omp-postgresql/pull/36) and
[#37](https://github.com/itskoleban/omp-postgresql/pull/37).

### Tests and qualification

PRs [#26](https://github.com/itskoleban/omp-postgresql/pull/26) through
[#38](https://github.com/itskoleban/omp-postgresql/pull/38) extend bounded coverage for
network ambiguity/recovery, TLS rejection, worker allocation failures, descriptors,
SQL-read faults, numeric/BYTEA round trips, batch atomicity and resource/result lifecycles.
Real open.mp x86 sessions exercise ordinary-protocol clients, external samp-bcrypt,
player-slot reuse, late callbacks, token exhaustion, GMX and uncertain INSERT reconciliation.
Source and extracted-package component/runtime identities are checked separately.
TSan detector controls and diagnostic captures have their own qualification.

PR [#40](https://github.com/itskoleban/omp-postgresql/pull/40) keeps disposable TLS
certificates valid through the configured fixture lifetime and retains stalled-connection
diagnostics for each attempt. This fixture work does not complete a prolonged campaign.
Not every PR introduces a user-visible feature. Historical failures retain their original
outcomes; later passes do not explain them.

### Documentation and release engineering

PRs [#22](https://github.com/itskoleban/omp-postgresql/pull/22) and
[#23](https://github.com/itskoleban/omp-postgresql/pull/23) preserve historical evidence
and validate classified Markdown examples. Current documentation identifies RC.2,
clarifies transaction/session behavior and provides consistent x86 installation guidance.
Source-controlled version/channel metadata supports stable and RC identities without
relabelling experimental packages.

PR [#39](https://github.com/itskoleban/omp-postgresql/pull/39) normalizes source ZIP
timestamps to UTC. PR [#41](https://github.com/itskoleban/omp-postgresql/pull/41) fixes
cross-platform `SHA256SUMS` filename ordering and preserves frozen evidence byte identities.
Deterministic assembly for identical inputs does not establish reproducible compilation.

## Compatibility

The public Pawn include, including **69 natives and three callbacks**, is unchanged from
RC.1. No new signature migration is required for RC.1 users. Named SQL uses
`pg_prepare_file(connection, filename, parameters, prepared, section)`;
`pg_prepare_section` was removed before RC.1 and remains unsupported. See the
[historical migration guidance](prestable-cleanup.md) for older programs.

Version metadata advances to `0.1.0-rc.2`, channel `rc`; the numeric project version stays
`0.1.0`. Runtime corrections change failure-path behavior. Unchanged Pawn signatures do
not establish full binary compatibility with every provider or toolchain. Use matching
component, include and runtime files. No new installation or database-schema migration
is introduced relative to RC.1; review the actual dependency manifest for your host.

## Supported platforms

- **Windows x86**, tested with official open.mp **1.5.8.3079**. The package supplies its
  patched libpq 17.11 and required non-system dependencies. Install the supported Microsoft
  Visual C++ v14 **x86** Redistributable separately, including on 64-bit Windows.
- **Linux x86**, tested with official open.mp **1.5.8.3079 dynssl** on Ubuntu 24.04.
  The package supplies `lib/libpq.so.5`; compatible i386 OpenSSL 3, glibc, libstdc++ and
  libgcc remain system prerequisites. Review manifest symbol-version requirements.

x64 Engine tests do not qualify x64 open.mp distributions. x86 CI uses disposable
PostgreSQL 17 servers (Windows pins 17.11; Linux installs the maintained PostgreSQL 17
package). This is not an exhaustive server compatibility matrix.
Source builds need CMake 3.24+, Git, a C++20 compiler and libpq 17+ with TLS/thread support,
all matching the target architecture and ABI.

## Installation

Follow [installation](installation.md), [dependencies](../DEPENDENCIES.md) and the
[Windows](build-windows.md) or [Linux](build-linux.md) build guide. Keep service profiles
and credentials outside Pawn/source control; verify remote TLS identity. Password hashing
requires an external provider and is not built into the component.

## Verification

Publication requires seven successful `build-and-test` jobs on the **exact integrated
master commit**, followed by the separate [manual release workflow](releasing.md).
PR CI alone does not qualify the release. The workflow validates tag/version/channel/commit
agreement, downloads both original tested distributions, rechecks packages and assembles
the tracked source ZIP, notes, dependency inventory and SHA-256 ledger.

For a published RC.2, both distributions have passed source and extracted-package checks
in fresh official open.mp servers, compiled all thirteen public Pawn examples with
warnings as errors, and run unattended examples and separate real-session fixtures.
The matrix checks Markdown examples/links, disposable documented SQL, English policy,
frozen evidence, negative publication cases and sanitizer configurations. Conditional
workflow setup steps are distinct from skipped qualification tests.

The inventory records the full commit, build/dependency metadata and payload/archive
hashes; standalone notes append the exact source commit. Verify the successful master
CI and public asset hashes independently. See [validation and limits](validation.md).
The inventory is unsigned and does not constitute a signed build attestation.

## Known limitations and residual risks

- [Issue #31](https://github.com/itskoleban/omp-postgresql/issues/31) remains open: the
  historical ASan network cleanup timeout has an unknown root cause. Later passes
  neither explain nor correct that failure.
- Historical intermittent Windows connection-stall/startup timeouts remain unexplained.
- The historical Linux logging anomaly remains unresolved. The retained original
  artifact omitted the component log, leaving incomplete evidence of the failure.
- Local GCC/MinGW/PostgreSQL 18.4 full-suite failures remain failures. A component-free
  libpq control reproduced a backend exception in that installation; this does not
  establish a general upstream cause or explain every observed failure.
- The **24-hour sustained campaign has not been completed**. Finite CI is not prolonged
  stability or production certification. A new blocking failure requires investigation
  and requalification.
- Rendered/encrypted official-client behavior, external provider multi-VM dispatch,
  arbitrary OOM, fully instrumented dependencies, exhaustive schedules/fuzzing, physical
  storage faults and every target host remain unqualified. Diagnostic queues can drop
  events; file output has no durable-flush or hard shutdown-deadline guarantee.
- `UNCERTAIN` writes require application reconciliation and are never automatically
  replayed. Reconnection does not restore transactions or player authentication.

Passing CI does not prove the absence of every concurrency or network issue. COPY,
LISTEN/NOTIFY reception, Engine pipeline mode, ORM and migration management remain
outside this candidate. Review [operations](security.md) and [transactions](transactions.md).

## Upgrade from RC.1

1. Stop the test server and retain a rollback copy of its component/include/runtime files.
   Download the selected RC.2 platform ZIP and its SHA-256 sidecar.
2. Verify the sidecar and `SHA256SUMS`; check version, full commit and platform in
   `release-inventory.json` and the package's `manifest.json`.
3. Replace the compatible component, include and packaged runtime files together. Preserve
   external credentials and check system dependencies. Do not substitute x64 or unrelated
   provider libraries.
4. Recompile and validate your Pawn programs with the supplied include. Run the smoke
   example and application player/session, transaction and uncertain-write checks in an
   isolated environment. RC.1 users need no new signature migration.

These testing assets do not authorize production deployment.
