# omp-postgresql 0.1.0-rc.3

## Overview

RC.3 prepares a testing Release Candidate for the asynchronous PostgreSQL component
for open.mp. It incorporates the distributed filename migration, worker-admission
correction and qualification improvements integrated since RC.2. These notes do not
publish a release or certify production suitability; stable `v0.1.0` remains blocked.

[RC.2](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.2) was
published from `3c3b3185c24362be12eb3e281383ad05d5dcd9b6`. Its tag, assets,
checksums and notes remain unchanged. The
[RC.2-to-current-source comparison](https://github.com/itskoleban/omp-postgresql/compare/v0.1.0-rc.2...master)
identifies integrated development changes; the final release inventory must identify
the exact qualified RC.3 commit. Consult the
[releases page](https://github.com/itskoleban/omp-postgresql/releases) for availability.

## Changes since RC.2

- [PR #43](https://github.com/itskoleban/omp-postgresql/pull/43) aligns installed
  filenames with open.mp conventions: `components/PostgreSQL.dll` on Windows,
  `components/PostgreSQL.so` on Linux and `qawno/include/omp_postgresql.inc`.
  Package names, the internal component name and UID keep the project identity.
- [PR #44](https://github.com/itskoleban/omp-postgresql/pull/44) closes queue admission
  when a worker terminates unexpectedly. A submission racing final worker shutdown is
  rejected synchronously instead of remaining in a stopped queue. Deterministic tests
  cover actual Engine/Lane admission and accepted-job terminal publication. This
  correction does not establish a cause for the historical issue #31 timeout.
- [PR #45](https://github.com/itskoleban/omp-postgresql/pull/45) preserves recovery
  cleanup and validates bounded network diagnostic captures. Linux ASan/UBSan retains
  the actual executable, client trace, server/activity/lock samples and source/runtime
  identities. Observer errors cannot qualify as successful activity collection.
  Workload deadlines, SQL-confirmation assertions and no-replay semantics are retained.
- [PR #46](https://github.com/itskoleban/omp-postgresql/pull/46) preserves campaign
  preflight failures and rejects inconsistent metrics. Missing tools, failed probes and
  malformed inventories leave negative evidence; impossible counters or percentile
  order cannot produce a passing campaign summary. The selected disposable PostgreSQL
  server has an explicit recorded identity.
- Preserve disposable PostgreSQL shutdown when retaining a wire-fault journal fails.
  A destination-directory collision or copy error previously could abort the fixture's
  remaining cleanup and replace the original workload exception. Regression cases cover
  both retention failures after successful and failed workloads. Cleanup now completes,
  the original workload failure is retained, and a successful workload still fails
  qualification if required evidence cannot be preserved. This fixture correction does
  not explain or resolve the historical issue #31 timeout.

These are bounded corrections and qualification controls within the existing
architecture. Diagnostic overhead can change scheduling. Finite successful runs do
not explain historical failures or prove freedom from leaks and concurrency defects.

The Windows-only `pg_windows_resource_probe` adds controlled observer, empty Engine,
thread, shell, Winsock and direct-libpq comparisons. The
[resource investigation](https://github.com/itskoleban/omp-postgresql/blob/master/audit/windows-resource-handles-2026-11-01.md) reproduces
the residual +64 handles without an Engine and distinguishes process initialization
from per-Engine cleanup. It records native object types and module inventories without
imposing universal handle-count thresholds. The
[fixture correction](https://github.com/itskoleban/omp-postgresql/pull/47) and its
[network evidence](https://github.com/itskoleban/omp-postgresql/blob/master/audit/network-recovery-2026-11-01.md) remain separate from #31's
unknown historical cause.

## Compatibility and migration

The public Pawn API retains **69 natives and three callbacks** with unchanged
signatures, constants and callback ownership contracts. Change the include directive
from `#include <omp-postgresql>` to `#include <omp_postgresql>` and recompile with the
matching include. Remove the old `components/omp-postgresql.dll` or `.so` before
installing the matching renamed component; keep only one binary with this UID.
See [installation and migration](installation.md#migrating-from-previous-release-candidates).

The distribution and SDK component version advance to `0.1.0-rc.3`, channel `rc`;
the numeric project version remains `0.1.0`. No database-schema migration is required.
Use the component, include and runtime files from the same exact-source package.
Unchanged API signatures do not qualify every provider, ABI or target environment.
`pg_prepare_file(connection, filename, parameters, prepared, section)` remains the
named-SQL entry point; the pre-RC.1 `pg_prepare_section` API remains unsupported.

## Supported test environments

- **Windows x86**, official open.mp **1.5.8.3079**. The distribution supplies patched
  libpq 17.11 and required non-system dependencies. Install the Microsoft Visual C++ v14
  **x86** Redistributable separately, including on 64-bit Windows.
- **Linux x86**, official open.mp **1.5.8.3079 dynssl**, qualified on Ubuntu 24.04.
  The distribution supplies `lib/libpq.so.5`. Compatible i386 OpenSSL 3, glibc,
  libstdc++ and libgcc remain system prerequisites; inspect symbol-version requirements.

x64 Engine tests do not qualify x64 open.mp distributions. Disposable PostgreSQL 17
fixtures provide the tested server scope, with Windows pinned to 17.11. This is not
an exhaustive server matrix. Source builds require CMake 3.24+, Git, C++20 and libpq
17+ with TLS/thread support, matching the target architecture and ABI.
Follow [installation](installation.md), [dependencies](../DEPENDENCIES.md) and the
[Windows](build-windows.md) or [Linux](build-linux.md) build guide.

## Exact-source qualification and publication gates

The pre-preparation integrated baseline is
`27e09b7cffa34968b78cb0025c121fce0a713e65`, with all seven jobs successful in
[master CI 41312759129](https://github.com/itskoleban/omp-postgresql/actions/runs/41312759129).
That run qualifies its RC.2-versioned development inputs, not later RC.3 edits.
RC.3 needs successful exact-head PR CI, followed after owner-approved integration by
all seven successful jobs on the exact final `master` commit. No tag or publication
is authorized by preparation or passing CI.

Both x86 distributions must pass source and fresh extracted-package official-server
and real-session checks, compile all thirteen public Pawn examples with warnings as
errors, and retain the eleven unattended example scenarios. The matrix also checks
Markdown/API/SQL examples, filesystem faults, sanitizer configurations, frozen evidence,
English policy and negative packaging/publication cases. Conditional tool-setup steps
are distinct from skipped qualification tests.

The final download job rechecks original tested distributions and prepares the tracked
source archive, dependency manifests, exact source inventory, notes and SHA-256 ledger.
Require version/channel/commit agreement across all assets and verify every checksum.
The [manual release procedure](releasing.md) requires separate owner authorization for
a tag and publication. Inventory hashes are unsigned integrity evidence; deterministic
assembly for identical inputs does not establish reproducible compilation or a signed
build attestation.

## Residual risks and limits

- [Issue #31](https://github.com/itskoleban/omp-postgresql/issues/31) remains open.
  The historical ASan network cleanup timeout remains **ROOT CAUSE UNKNOWN**.
  Later diagnostic passes do not establish its cause or correct that historical result.
  A recurrence or demonstrated Engine/Lane/fixture defect is a new release blocker.
- Controlled Windows experiments reproduce the +64 retained-handle vector through
  direct libpq TLS on a joined worker, without an Engine. Counts stay stable in the
  bounded cycles and reach zero after complete process exit. This supports an
  initialization/retention path in dependencies and Windows; exact allocator stacks
  of unnamed objects remain unavailable. Finite resource samples do not prove
  general leak freedom or replace long-duration qualification.
- Historical intermittent Windows connection-stall/startup timeouts and the Linux
  logging anomaly with incomplete original evidence remain unexplained.
- Local MinGW/PostgreSQL 18.4 failures retain their original failed outcomes. A libpq-only
  control reproduced a backend exception in that installation, without establishing a
  general upstream cause or explaining every observation.
- The **24-hour sustained campaign has not been completed**. Verified isolated non-root
  Linux infrastructure, source/dependency freeze, reviewed risks and bounded preflight
  are prerequisites; see [readiness](rc-readiness.md).
- Fully instrumented dependencies, exhaustive schedules/fuzzing, physical storage faults,
  every host/provider and rendered/encrypted official-client behavior remain unqualified.
  Diagnostic queues can drop events; file output has no durable-flush or hard
  shutdown-deadline guarantee.
- `UNCERTAIN` writes require application reconciliation and are never automatically
  replayed. Reconnection does not restore transactions or player authentication.

COPY, LISTEN/NOTIFY reception, Engine pipeline mode, ORM and migration management
remain outside this candidate. Review [operations](security.md),
[transactions](transactions.md) and [validation limits](validation.md).

## Upgrade from RC.1 or RC.2

1. Stop the test server and retain a rollback copy of its component/include/runtime files.
   Download the selected RC.3 platform ZIP and its SHA-256 sidecar after it is available.
2. Verify the sidecar and `SHA256SUMS`. Check `0.1.0-rc.3`, channel `rc`, the full commit
   and platform in `release-inventory.json` and the package's `manifest.json`.
3. Follow the filename migration above and replace the matching component, include and
   packaged runtimes together. Preserve external credentials and check system runtimes.
4. Recompile and validate Pawn programs in an isolated environment. Run the smoke example
   and application player/session, transaction and uncertain-write reconciliation checks.

These testing assets do not authorize production deployment.
