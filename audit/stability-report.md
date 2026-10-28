<a id="auditoría-de-estabilidad-v010--23-de-octubre-de-2026"></a>

# v0.1.0 stability audit - October 23, 2026

English translation of the maintained narrative at `c8528bf89068dfd33fd96bf7350ee59e599d278f`. Historical statements retain their original dates and scope; this translation does not rerun tests or change evidence. Numeric spelling and legacy heading IDs are preserved.

> **A1/A2 follow-up:** [resolution, negative tests and new evidence](a1-a2-resolution.md).
> The corrected runtime is `ba384fffbdef757e3a22c0dc00901af18c6c9512`; the executions
> below belong to the historical state and are not reassigned to that SHA.
> Final-head CI is checked and recorded separately in PR #24.
> Resolving these integration blockers does not authorize stable v0.1.0 publication.

**Verdict: INSUFFICIENT EVIDENCE to approve a stable version.**
Executed Windows validation is satisfactory after correcting a Medium defect,
but final-revision Linux qualification, newly extracted packages and the remaining
long/adverse campaigns are missing. This report does not authorize a stable release.

<a id="identidad-y-alcance"></a>

## Identity and scope

- Audited base: `3cbf99849de57de09e4fa8317b9bb40f8e2d2f8f`.
- Executed code revision: `6cf0c05893c79cb8dbdb0b54f2eeacf585ac3f91`, clean tree.
- Branch: `audit/v0.1.0-stability`. The later documentation/evidence commit does not
  change tested binaries. These executions are not attributed to another SHA.
- Functional correction: `6303e0f32cb6bc708b5902abda7d16c61d8cf726`.
- `master`, tags, releases, RC.1 assets and production services were not modified.
  Databases were temporary loopback-only clusters, stopped after execution.
- Audit by inspection and local execution; this is not presented as a new independent
  audit by the historical reviewer in `docs/independent-audit.md`.

<a id="resultado-ejecutivo"></a>

## Executive results

| Executed check                                                                    | Result                                                                                                                                | Evidence                                                                                          |
| --------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| Windows x64, GCC 16.1, libpq 18.4, official PostgreSQL 17.11/TLS server           | **17/17 CTest**, zero failures/errors/skips; 226,20 s CTest                                                                           | [JUnit](stability-evidence/windows-x64.xml)                                                       |
| Windows x86, MSVC 19.51, project-patched libpq 17.11, PostgreSQL 17.11/TLS server | **19/19 CTest**, zero failures/errors/skips; approximately 205,7 s                                                                    | [JUnit](stability-evidence/windows-x86.xml), [full log](stability-evidence/windows-x86-ctest.txt) |
| Python                                                                            | **81 methods**, 75,281 s                                                                                                              | [Log](stability-evidence/python-final.txt)                                                        |
| Official open.mp 1.5.8.3079 Windows x86, component built from source              | Smoke and GMX/unload/reload, callbacks, extensions, bootstrap, sections, logging, performance and four tutorials: PASS                | [Excerpts](stability-evidence/openmp-source-excerpts.txt)                                         |
| Pawn 3.10.11                                                                      | 13 public examples; 29 Markdown snippets and 70 declarations compiled; one explicitly incomplete snippet is not treated as executable | Local compilation with warnings as errors; does not replace Pawn execution                        |
| Normal semantic fuzzing                                                           | 4.096 cases per suite and **100.000 additional cases** without sanitizers; extended seed 1592594996                                   | [Extended log](stability-evidence/extended-fuzz.txt)                                              |
| Helpers with Clang 22 ASan/UBSan Windows x64                                      | **4.096 cases**, seed 1096107081; no diagnostics, exit 0; 16,45 s                                                                     | [Log](stability-evidence/asan-ubsan-helpers.txt)                                                  |
| External supervisor, reduced execution                                            | Completed integration/load and extended fuzzing, retains SHA and statuses; fallback cleanup unnecessary                               | [Summary](stability-evidence/external-runner-summary.json)                                        |

Local ASan/UBSan instrumented `audit_fuzz.cpp`, `parameters.cpp`, `sql_sections.cpp`
and `statement.cpp`: **not** the complete Engine, libpq, OpenSSL or AMX interpreter.
There was no new local TSan execution. Engine x64 testing does not qualify an x64 server.

<a id="cobertura-histórica-contrastada"></a>

## Historical coverage cross-check

`docs/validation.md`, `docs/rc-readiness.md`, `docs/independent-audit.md`, the continuity
ledger and workflows were reviewed. Full logs for the seven jobs in
[CI 39666914034](https://github.com/itskoleban/omp-postgresql/actions/runs/39666914034)
were retrieved at exactly base `3cbf998`: Linux x86 18, Windows x86 17, Windows x64 15
and Linux x64 OFF/ASan-UBSan/TSan 18 each: **104 historical CTest cases**. Source/package
server scenarios and the distribution job are also recorded. The
[retained excerpts](stability-evidence/historical-ci-excerpts.json) distinguish that
historical remote run from new executions. No ASan/UBSan/TSan diagnostic signatures
were detected in the retrieved logs; dependencies were not fully instrumented.
RC.1 assets were not downloaded or verified again.

Tests already covered FIFO, snapshots/prepared/batches, savepoints, COMMIT rejected
by deferred constraints, COMMIT confirmation loss, immediate restart, cancellation,
streaming/backpressure, limits, native boundaries, unload and logging. New tests
complement those scenarios; existing suites were preserved.

<a id="revisión-de-arquitectura-y-límites"></a>

## Architecture review and limits

- **Engine and workers:** public registries belong to the owner thread; each worker
  owns one `PGconn` and its destruction. The queue is protected, and atomic terminal
  publication precedes result reads. `poll()` removes completions before callbacks;
  join precedes queue destruction. Analysis found D1 in interruption classification,
  not evidence of a result race.
- **Pools/FIFO and ownership:** lanes retain local order, not global order. Accepted
  jobs own snapshots; closing statements does not invalidate that work. Load checks
  exact SQL counters, capacity reuse and weak-reference expiry.
- **Transactions/savepoints:** lane and completion-capacity reservations; existing
  reconciliation of rejected COMMIT. New tests block a deferred trigger inside COMMIT,
  terminate only the PID obtained from our connection and verify zero rows, uncertain
  state, a new session and recovery. Writes are not automatically replayed.
- **Scripts/callbacks:** AMX generations, copied arguments and dispatch without queue
  locks. Reentry, closure from callbacks, sleep/error and unload tests were preserved.
  The synthetic reset during `Exec()` documented in the earlier review still lacks
  a reachable reproduction in the official server and is not a confirmed crash here.
- **Streaming/batches/prepared:** block-bounded backpressure, deadlines and cancellation;
  prepared-state invalidation on session/plan changes. Row order and completeness
  are checked. A provisional block neither confirms SQL nor justifies replay.
- **Parser/files:** UTF-8 without NUL, names/budgets, nested comments, quotes/dollar
  delimiters, exact selection and rejection of duplicate sections/multiple statements.
  Canonicalization and size limits do not protect against an administrator changing
  symlinks between checking and opening; SQL is trusted operator code.
- **Natives/memory:** arity, address/cell, index, output-preflight and flat-slot dimension
  checks were preserved. New fuzzing tests component helpers/arguments, not the AMX
  loader or interpreter.
- **Logging/shutdown:** bounded queues, counted drops and asynchronous writer. Historical
  Linux EACCES/short-write/ENOSPC tests are not new Windows tests. Blocked OS/DNS/auth/
  filesystem calls can delay join; there is no absolute OS-independent shutdown bound.
  Observed saturation drops are counted, not presented as zero loss.
- **libpq:** extended protocol, single-row retrieval and recovery without replay.
  libpq can allocate a row or message before component budgets apply; retained-data
  limits are not a strict RSS bound.

<a id="defecto-d1--medium--corregido"></a>

## D1 defect - Medium - corrected

**Problem:** cancellation observed in `Lane::wait_socket()` could be published as
`Error::connection` instead of `Error::cancelled`. The same path discarded the cause
of a deadline observed inside wait. Dispatched SQL remained uncertain.

**Reproduction:** with real Engine/libpq and disposable PostgreSQL 17.11, send
`SELECT pg_sleep(1)`, wait for `Status::running`, set `job->cancel` and dispatch the
terminal result. Initial mixed load failed; a dedicated 32-round regression failed
again on its first round:

```text
cancel round=0 status=4 error=5 message=Interrupted connection
audit_faults FAIL Observed cancellation lost its cancellation error category
```

**Expected:** cancelled cause; uncertainty retained after dispatch and no replay.
**Observed:** correct uncertain status but incorrect transport cause. This can
misdirect application error handling; corruption or false confirmation was not demonstrated.

**Root cause:** the loop checks flags/deadline before consuming input. They can change
before `wait_socket()` checks internally. Its false return was unconditionally
translated into connection failure. The correction rereads cancel/stop/deadline
before the transport fallback. It does not change signatures, ABI, reconnection
policy, uncertainty classification or replay. The timeout branch now has eight
test rounds; the retained earlier reproduction specifically demonstrates cancellation.

**Regression:** [previous failure](stability-evidence/cancel-before.txt),
[later x64 suite](stability-evidence/windows-x64.xml) and
[later x86 suite](stability-evidence/windows-x86.xml). The earlier failure used the
base runtime and new tests before the fix; that initial working tree is not presented
as a clean build of the final SHA.

No new Critical/High component defect was confirmed in this work. This does not
demonstrate absence of defects in every schedule or environment.

<a id="carga-reducida-y-medición"></a>

## Reduced load and measurement

Sustained phases of at least **120 seconds**, not 12 or 24 hours, used seed 1592594996.
The same main Engine instance retained four lanes in each phase. Each wave has
48 jobs; every 32 waves there are read/write transactions, ordered streaming,
cancellation, reconnection and creation/closure of an additional connection.
Deterministic recovery pauses precede the existing lifecycle scenarios.

Windows x86: **393 waves**, **182.365 counted load operations**, last sample at
**120,113289 s**, approximately **1.518 operations/s** for this load and host.
RSS after the first wave: **15.908 KiB**; final **16.524 KiB**; observed maximum
**16.544 KiB**. Process CPU through the last sample: **36,546875 s**. Medians of
per-wave percentiles: p50 **131.979 us**, p95 **246.549 us**, p99 **258.938 us**.
These are not global percentiles.

[x86 CSV](stability-evidence/stress-x86.csv) and [structured summary](stability-evidence/metrics-summary.json).
pending=0/errors=0 samples are checked quiescence points, not continuous sampling.
The counter omits setup/verification and some auxiliary SQL. Allocator bytes were
not measured. Finite measurements establish neither a leak bound nor performance comparisons.

On x64, CTest passed the complete phase, but the later fuzz CTest overwrote
`LastTest.log`; JUnit retains truncated stdout and only three complete initial
samples. That partial capture is explicitly retained, **without inferring final x64
metrics**. The attached x64 text is derived from JUnit stdout and labelled accordingly.

<a id="fallos-bloqueos-y-trabajo-no-ejecutado"></a>

## Failures, blockers and work not executed

- **Earlier D1 failure:** retained and corrected; not erased from result history.
- **Local PostgreSQL 18.4/MSYS2 provider:** baseline suite was 14/15. The server recorded
  a backend terminated by exception `0xC0000028` while processing `SELECT 1; SELECT 2`;
  the client timed out. That case passed with official server 17.11, also using libpq
  18.4. No standalone reproduction outside Engine was completed; this is neither a
  newly confirmed component defect nor certification of that provider. Original logs
  are in the local delivery.
- **ASan/UBSan 100.000 cases:** started but was interrupted at the approximate ten-minute
  wall budget without a complete result; not a pass. A separate 4.096-case execution
  completed and is the only new sanitized qualification claimed.
- **Sandbox:** permissions blocked initdb, sockets/Python and MSBuild workers. Failed
  attempts are not passes. Valid runs used host permissions, new folders and fixture-specific cleanup.
- **Final Linux/TSan/full ASan:** not run locally; Docker lacked a daemon and WSL returned
  access denied. Historical results belong to the base, not the fix.
- **Packages extracted from the new revision:** not run. The official x86 source
  installation passed; this is not equivalent to new distribution qualification.
- **12-24 hours, long partitions, systematic FD/allocator exhaustion, memory pressure,
  frozen physical/network storage, AMX-loader fuzzing and real playerid reuse:** not
  qualified by this work. Limit tests and SDK doubles have narrower scope.

<a id="automatización-y-criterios-pendientes"></a>

## Automation and pending criteria

[Runbook](stability-runbook.md), [scenario matrix](stability-matrix.csv) and
[manual workflow](../.github/workflows/audit-long.yml). Existing fast/release CI is
preserved. The long workflow needs a preprovisioned disposable Linux runner labelled
`pg-audit-isolated`; it was not dispatched and no paid resources were activated.
The supervisor ran with 120 s and rejects builds configured for a shorter duration.

READY requires at least exact CI for the proposed integrated change, Linux x86 and
sanitizers for the corrected revision, fresh package/server qualification on both
systems, binding new artifacts to evaluated code and assessment of remaining support
limits. The long campaign is prepared but unexecuted. The PR must remain a draft;
later green CI also does not authorize a merge or release.

Evidence files are text/CSV/JSON/XML with workspace paths redacted and a
[hash ledger](stability-evidence/ledger.json). They contain no clusters, dumps,
connection profiles, TLS keys, tokens, binaries or packages. Existing project rules
already exclude `/audit/` from source archives and public installation.
