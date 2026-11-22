# omp-postgresql 0.1.0-rc.4

## Overview

RC.4 is an evaluation candidate for the asynchronous PostgreSQL component for open.mp.
The distribution and SDK component version is `0.1.0-rc.4`, the release channel is `rc`,
and the numeric CMake project version is `0.1.0`. The intended tag is `v0.1.0-rc.4`.
Check the [Releases page](https://github.com/itskoleban/omp-postgresql/releases) for
published distributions. Stable v0.1.0 still requires its own final-source qualification
and a separately authorized 24-hour sustained stability campaign.

[RC.3](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.3) was
published from `b715a971e71047a9c73f6b85489fc342317ba86b`. RC.1 through RC.3 retain their original tags, assets, checksums, and release notes.

## Changes since RC.3

### Connection startup and recovery

[PR #59](https://github.com/itskoleban/omp-postgresql/pull/59) follows libpq's
nonblocking connection contract. After `PQconnectStartParams`, the driver waits for
initial write readiness before calling `PQconnectPoll`. It then waits in the direction
requested by libpq and uses the current socket for each wait. A timeout or interruption
does not advance the polling state. On Winsock, readiness in the exceptional set lets
libpq inspect the socket error, report a refused connection, and fall back to another
address when available.

Controlled Windows tests reproduced the RC.3 Winsock 10057 error with a pending TCP
connection and with a refused address followed by a working address. The corrected
driver completed TLS and queries once a connection was available. The controlled
one- and two-lane tests also exercised backend loss, delivered interrupted jobs as
`PG_UNCERTAIN`, and resumed successful queries after reconnecting.
See [connection readiness](connection-readiness.md) for the test scope and deadlines.

### Windows x86 distribution

During PR #59 qualification, the rolling MSYS2 MINGW32 OpenSSL package had moved to
OpenSSL 4 while runtime staging still expected OpenSSL 3 DLL names. The build now
reads the actual PE imports of libpq 17.11 and stages the required, allowlisted x86
DLLs from the same MinGW installation used for linking. The qualified OpenSSL 4.1.0
build imported `libssl-4.dll` and `libcrypto-4.dll`. CI checked PE32 architecture,
runtime imports, staged file hashes, and fresh installed-package tests. Package
manifests record the dependency and license inventory.

The [PR #59 qualification](https://github.com/itskoleban/omp-postgresql/actions/runs/45030477428)
and the [integrated baseline at `9932370`](https://github.com/itskoleban/omp-postgresql/actions/runs/45062137286)
each passed seven jobs with zero failures and zero skipped jobs. Both runs predate
the RC.4 version change.

### Reporter validation against Neon

The [original reporter](https://github.com/itskoleban/omp-postgresql/issues/58#issuecomment-7084324361)
tested the corrected Windows x86 development artifact on Windows 11 with
open.mp 1.6.7.3218. The direct Neon connection and SQL checks passed on every requested lane:

```text
PG NEON READY lanes=1 elapsed_ms=1134
PG NEON PASS lanes=1
PG NEON READY lanes=2 elapsed_ms=552
PG NEON PASS lanes=2
```

This evidence is specific to the reporter's setup. CI used disposable PostgreSQL
fixtures. A same-session RC.3 versus corrected Neon comparison and forced live Neon
recovery remain unverified. The controlled tests establish a path to the observed
Winsock error under the tested conditions. [Issue #58](https://github.com/itskoleban/omp-postgresql/issues/58)
records that incident. [Issue #31](https://github.com/itskoleban/omp-postgresql/issues/31)
tracks a separate historical Linux ASan cleanup timeout whose cause remains unknown.

### Other integrated changes

- The release note exporter pins relative documentation links to the tested source.
  Disposable PostgreSQL fixtures preserve the primary failure and continue cleanup
  and evidence retention when finalizers fail.
- Bounded transaction interruption and reconnect exhaustion checks verify uncertain
  outcomes with independent SQL and backend observations. The
  [finite pre-soak checklist](pre-soak-checklist.md) records the preflight and
  100,000-case semantic fuzz results for their tested inputs.
- BEGIN reserves a public identifier for COMMIT or ROLLBACK. A finalizer rejected
  before queue acceptance leaves that identifier available for retry.
  See [handle lifetime and exhaustion](api.md#handle-lifetime-and-exhaustion).

## Compatibility and installation

The public Pawn API retains 69 natives and three callbacks. No database-schema migration
is required. Use the component, include, and runtime files from one exact-source
package. RC.1 and RC.2 users should remove the old binary, replace the include with
`omp_postgresql.inc`, and recompile. See [installation and migration](installation.md).

- **Windows x86:** CI uses official open.mp 1.5.8.3079 and patched libpq 17.11 with its
  required x86 runtime DLLs. Install the Microsoft Visual C++ v14 x86 Redistributable
  separately, including on 64-bit Windows. OpenSSL 4 removed ENGINE support, so
  ENGINE-backed `sslkey` is unavailable with that build. MSYS2 has reduced 32-bit
  package coverage over time. Review actual imports and requalify dependency updates.
- **Linux x86:** CI uses official open.mp 1.5.8.3079 dynssl on Ubuntu 24.04. The ZIP
  includes `lib/libpq.so.5`. Compatible i386 OpenSSL 3, glibc, libstdc++, and libgcc
  are system prerequisites.
- **Recovery and handles:** Reconnection does not restore transactions or player
  authentication. Applications must reconcile uncertain writes. The component
  never replays them automatically. Each Engine lifetime has a cumulative limit of 2,147,483,646 public identifiers. See [transaction behavior](transactions.md) and
  [handle lifetime](api.md#handle-lifetime-and-exhaustion).

The [validation record](validation.md) retains the historical network, logging, and
resource findings. Bounded Windows tests reproduced the residual 64 handles using
direct libpq TLS without an Engine. The count stayed stable in those tests and
cleared at process exit. Longer runs and other host, provider, and runtime
combinations remain outside that evidence. x64 Engine builds do not qualify x64
open.mp distributions.

## Exact-source verification

Before publication, require all seven CI jobs on the exact integrated RC.4 commit.
Verify both downloaded x86 packages, their manifests and SHA-256 sidecars, the source
ZIP, standalone notes, and `release-inventory.json` against the same full commit,
version, and channel. The x86 jobs exercise fresh installed official servers, compile
all thirteen public Pawn examples, and run eleven unattended examples alongside
real-session tests. The [release procedure](releasing.md) describes the owner gates.

To evaluate a package, verify its checksum and confirm `0.1.0-rc.4`, channel `rc`,
and the full source commit in the manifest. Install it in a disposable server,
recompile your Pawn programs with the matching include, and exercise application
sessions, transactions, and uncertain-write reconciliation.
