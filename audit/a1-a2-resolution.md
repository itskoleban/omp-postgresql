<a id="resolución-de-a1-y-a2--pr-24"></a>

# A1 and A2 resolution - PR #24

English translation of the maintained narrative at `c8528bf89068dfd33fd96bf7350ee59e599d278f`. Historical statements retain their original dates and scope; this translation does not rerun tests or change evidence. Numeric spelling and legacy heading IDs are preserved.

Date: October 23, 2026. Corrected code: **`ba384fffbdef757e3a22c0dc00901af18c6c9512`**.
Review base: `0fbea5f7c7a3e209be3f38b516490b4ea9414f21`; master remains
`3cbf99849de57de09e4fa8317b9bb40f8e2d2f8f`. This document and its evidence do not change runtime behavior.

<a id="a1-conservar-la-causa-observada"></a>

## A1: preserve the observed cause

`Lane::wait_socket()` now returns `Error`, using `Error::none` to continue.
It captures cancellation/shutdown or deadline expiry on entry; an invalid socket,
a descriptor outside FD_SETSIZE and a nonretryable select failure return connection.
`receive()` propagates that result without inferring it again from later flags or
clock readings. A transport error observed by select remains connection even if
cancel is set or the deadline expires during the call. If interruption is observed
before attempting I/O, that interruption is preserved; this does not claim to
diagnose a network failure that has not yet been observed.

EINTR/WSAEINTR is also corrected: the loop retries and checks cancellation, stop
and deadline again. A signal interruption alone does not imply a broken connection.
Flush/consume errors remain connection errors.

Terminal publication is unchanged: dispatched SQL without a definitive response
remains uncertain, with completed=0 and an uncertain item; the session closes and
its transaction and prepared state are invalidated. Session recovery does not resend
SQL. An already processed definitive response can win over later cancellation;
requesting cancellation does not mean ROLLBACK. Pawn signatures and constants are unchanged.

<a id="a2-demostrar-el-punto-de-ejecución"></a>

## A2: demonstrate the execution point

The new `socket_wait_unit` compiles the actual `src/session.cpp` body with libpq,
clock and select seams; Engine, queues, publication and reconnection remain real.
The controlled clock advances without sleeps. Condition-variable synchronization
publishes the job before injection. Counters establish send, select entry and result
consumption. The only new syscall selector, PG_SOCKET_SELECT, resolves directly to
::select in the normal component: it adds no runtime hooks or fake calls to the package.

The tests cover 20 Windows cases and 21 Linux cases (including the descriptor limit):
cancel/timeout/stop on entry; transport alone and concurrent with cancel/deadline/both;
invalid socket; EINTR with recovery/cancel/timeout; readiness and poll timeout;
provisional response and cancel; complete response winning over cancel/deadline;
predispatch cancellation; flush/consume failures coinciding with cancel. Every case
requires the state, cause, metadata, reached phase, one send (or zero before dispatch),
one completion and recovery where applicable. A watchdog bounds failures; injection
points do not depend on sleeps.

The real test retains its 32 rounds and now uses another backend to observe its own
PID, exact SQL, state=active and wait_event=PgSleep in pg_stat_activity before
cancelling. RUNNING is no longer proof of dispatch. The test requires
uncertain/cancelled, completed=0 and an uncertain item. Polling only waits for the
observable condition; it does not decide the critical schedule. The deterministic
test complements real PostgreSQL rather than replacing it.

<a id="pruebas-negativas-y-reproducción-original"></a>

## Negative tests and original reproduction

Disposable copies were built with the historical runtime and the new test, adding
only the syscall selector needed for injection (without changing historical wait/receive).

| Runtime                                  | Case              | Verified phase    | Expected and observed result                                   |
| ---------------------------------------- | ----------------- | ----------------- | -------------------------------------------------------------- |
| 3cbf99849de57de09e4fa8317b9bb40f8e2d2f8f | cancel_entry      | sends=1, select=0 | Exit 1: uncertain/connection; should preserve cancelled        |
| 0fbea5f7c7a3e209be3f38b516490b4ea9414f21 | transport_cancel  | sends=1, select=1 | Exit 1: uncertain/cancelled; should preserve connection        |
| 3cbf99849de57de09e4fa8317b9bb40f8e2d2f8f | timeout_entry     | sends=1, select=0 | Exit 1: connection instead of timeout                          |
| 0fbea5f7c7a3e209be3f38b516490b4ea9414f21 | transport_timeout | sends=1, select=1 | Exit 1: timeout instead of connection                          |
| 0fbea5f7c7a3e209be3f38b516490b4ea9414f21 | eintr_retry       | sends=1, select=1 | Exit 1: uncertain/connection instead of completing after retry |
| ba384fffbdef757e3a22c0dc00901af18c6c9512 | All               | 20 Windows cases  | PASS; metadata and completion verified                         |

The original audit reproduction, linked to corrected pg_core, publishes connection
in its three select-error modes, including concurrent cancel/deadline; its fourth
mode preserves predispatch cancellation with zero sends. See
[evidence and hashes](stability-evidence/a1-a2/SHA256SUMS.txt).

<a id="validación-local-y-ci"></a>

## Local validation and CI

Local evidence includes Windows x64 JUnit (18/18), Windows x86/MSVC (20/20),
81 Python methods, five negative executions and the original reproduction. Both
builds were configured with source_tree=clean at the stated code commit, official
PostgreSQL 17.11/TLS and PG_STRESS_SECONDS=120. They include COMMIT, ROLLBACK,
reconnection and the previous suites. The public transformation redacts workspace
paths and hostname; the ledger records original and public-copy hashes. Earlier
historical logs are not reinterpreted.

The final integration gate is **CI for the current PR head**, including its merge
candidate: [PR #24 checks](https://github.com/itskoleban/omp-postgresql/pull/24/checks).
This document records local validation before push and does not anticipate a remote
pass. The PR description and follow-up report record the final run and SHA after
waiting for GitHub Actions. Linux x86/open.mp, Windows x86/open.mp, Windows x64,
Linux OFF/ASan-UBSan/TSan and the package gate are required.

Reproduction in an isolated development environment:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPG_BUILD_BENCHMARK=ON -DPG_STRESS_SECONDS=120
cmake --build build --parallel 4
ctest --test-dir build -R '^socket_wait_unit$' --output-on-failure
python tests/run_postgres.py --build build --config Debug --doc-sql
python -m unittest discover -s tests -p 'test_*.py' -v
```

On Windows, select a toolchain and libpq of the same architecture and provide
--postgres-bin/--openssl as described in the runbook. The test accepts a case name
as a pg_socket_wait_tests argument to reproduce an individual interleaving.

<a id="riesgos-restantes-y-decisión"></a>

## Remaining risks and decision

A1 and A2 are corrected and covered by deterministic oracles and real integration.
The local technical recommendation is **APPROVE WITH CONDITIONS** until final-head
CI passes; the later verdict is recorded in the PR description after verification.
No merge, force push, tag, release or RC.1 asset modification was performed.

Stable v0.1.0 publication remains a separate decision: no 12-24-hour campaign,
systematic allocator/FD exhaustion, every network partition, AMX-loader fuzzing or
real playerid reuse was executed. R1 (allocation during terminal publication)
remains an unreproduced risk, not a newly confirmed defect. Incompletely instrumented
external dependencies and OS-dependent shutdown retain their earlier limitations.
Hostname metadata in historical evidence remains a Low finding.
