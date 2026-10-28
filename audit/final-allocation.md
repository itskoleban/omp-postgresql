# R1: terminal publication and worker allocation failures

Current PR #28 follow-up: [independent scenario assertions and published descriptor
evidence](pr28-follow-up.md). The records below retain their historical source,
results and instrumentation scope; the follow-up does not reassign those results.

Status: confirmed component defect with implemented fix and executed regression.
The exact final-head CI result is recorded in [PR #25](https://github.com/itskoleban/omp-postgresql/pull/25),
including its complete SHA; the source-specific records below are not reassigned to later commits.
Severity: **High** (an allocation exception can terminate the server process).
No public Pawn signature or error/status constant changes.

## Executed negative controls

- Queue probe at `b6e2db1a46659d033160ba789b4e8e8b0c7057e1`:
  [run 40047360530](https://github.com/itskoleban/omp-postgresql/actions/runs/40047360530),
  job 120732034299, Linux x64. The actual CompletionQueue throws bad_alloc after
  127/4096 terminal publications with ordinary C++ allocation denied. Exit 1.
- Real Engine worker at `b9eb6706eb7df277bbae8e22b33062c23f5de27e`:
  [run 40047842306](https://github.com/itskoleban/omp-postgresql/actions/runs/40047842306),
  job 120733515354. Disposable loopback PostgreSQL/TLS; worker_allocation_integration
  aborts in 0.14 s with std::bad_alloc. CTest fails; the fixture shuts PostgreSQL down.
  The other 17 tests do not turn that run into a pass.
- Traced control at `baafad0a85bf88f69783a664fe6da33e0388c585`:
  [run 40048458262](https://github.com/itskoleban/omp-postgresql/actions/runs/40048458262),
  job 120735460054. Same runtime; tracing plus the native queue fixture adaptation only.
  It aborts in 0.13 s. First denied allocation: 51-byte string construction in receive().
  The next two denials are 64-byte deque maps in Lane::run(), at +0xa3a and +0xcff.
  Logs and JUnit are retained as workflow artifacts, including the negative results.

These are confirmed component exception-safety defects, not an assertion that the OS
actually ran out of physical memory. The replacement operator new is allowed to throw
bad_alloc. It denies ordinary C++ worker allocations only after an observer sees the
exact writer PID, SQL, active state and PgSleep in real PostgreSQL. Main-thread
allocations and C malloc in external dependencies remain available. The test is narrower
than system-wide OOM, libpq malloc failure, aligned/nothrow allocation, or OS overcommit.

## Cause and correction

An ID deque could allocate after terminal status publication, losing its completion.
The queue now links the already accepted shared Job objects under its mutex; push/pop
allocate nothing. Destruction releases abandoned chains iteratively. Producer-local
FIFO, ownership and four-producer publication are tested with allocations denied.

Persistent failure then exposed two temporary deques: recovery allocates a failed-job
container inside the outer try; shutdown allocates an abandoned-job container outside
it. The latter exception escapes the worker thread and invokes std::terminate.
The fix removes both temporary containers and settles accepted jobs one by one.
Terminal diagnostic copies use a nonthrowing fallback to empty text while retaining
structured error, terminal status, uncertainty, completed count and item metadata.

The worker regression keeps allocation denial through terminal delivery and shutdown.
It checks recovery and shutdown separately, one completion per accepted job, no queued
SQL replay (exact dispatch count), uncertain metadata for the dispatched SELECT, and
explicit new SQL recovery once allocation is restored. Cancellation is not presented
as proof of database rollback.

## Remaining limits

The main-thread dispatcher can allocate; universal application-wide OOM recovery is
not claimed. Mutex/kernel/library calls may block or fail independently. External
libpq/OpenSSL allocation paths are not covered by this C++ replacement. System-wide
memory/handle exhaustion, every allocation point and every SQL commit outcome still
need separate qualification. The 24-hour campaign remains **BLOCKED** pending verified,
authorized remote infrastructure. No merge, tag, release or RC.1 asset was changed.

## Executed positive record and test compatibility

Fix commit: `93da65a7e43fc244f2a32d7eb3ee6c4c3fb3e122`.
[Remote probe run 40048647471](https://github.com/itskoleban/omp-postgresql/actions/runs/40048647471)
completed both jobs successfully: real PostgreSQL fixture **18/18 CTest**, including
worker allocation recovery/shutdown, and the terminal queue probe. Later intermediate
CI also passed these allocation cases on Windows x86/x64 and Linux normal/ASan.
Those partial passes do **not** turn failed overall CI runs into successful ones.

The new global allocation replacements cannot link over Clang 19 TSan's strong
new/delete interceptors. The failed link is retained in run 40048651060. TSan now
keeps its interceptors and runs `worker_recovery_integration` and
`terminal_queue_unit` with `allocation_injection=OFF(TSan)`. Ordinary and ASan/UBSan
builds retain actual allocation denial, with distinct allocation test names. No TSan
allocation-injection pass is claimed. Its queue/worker concurrency checks remain enabled.

A later unrelated, newly added INSERT lock test initially omitted a primary key:
the INSERT completed instead of waiting for a unique-index conflict, and the phase
assertion correctly timed out. Runs 40048938444 and 40050269804 remain **FAIL**.
The fixture now creates the primary key; the phase check and deadline are unchanged.
This is a test-fixture defect, separate from the reproduced worker abort.

## Evidence preservation

Full decoded job logs are preserved in [final-evidence](final-evidence/r1-ledger.json),
with original run/job/source identities and GitHub artifact SHA-256 digests. These are
public CI fixture logs, without database directories, TLS private keys or service files.
A negative result is retained as a negative result.

The optional general bounded-soak matrix is now manual-only to respect the immediate
allocation-failure scope. Incomplete/cancelled earlier attempts are not qualification
passes. The dedicated 24-hour run was not dispatched and remains **BLOCKED**. Existing
stable/RC preparation remains on the branch; no publication recommendation follows
from this allocation fix.

## Independent descriptor/sanitizer limitation (separate from R1)

The exact-head run [40051203920](https://github.com/itskoleban/omp-postgresql/actions/runs/40051203920)
at `4a84446f0642cda34e690469e87dc819cb97267b` failed the new
resource_exhaustion_integration under ASan/UBSan: GCC 13.3 reported an invalid vptr
in std::thread cleanup while the process had no free descriptors. Worker allocation
and terminal allocation regressions passed in that same job. That CI result remains FAIL.

Two independent controls were executed without component headers/objects or a database:

- The simple thread control in [run 40051733934](https://github.com/itskoleban/omp-postgresql/actions/runs/40051733934),
  job 120745671252, at `c832833446d031a4ea73afc1708794309f7dab53`, also emitted the
  std::thread invalid-vptr warning at EMFILE and completed with exit 0. The initial
  job-summary reading missed stderr; the complete log corrects that observation.
  This control is **not counted as a sanitizer PASS**.
- The cold valid-object control in [run 40051890695](https://github.com/itskoleban/omp-postgresql/actions/runs/40051890695),
  job 120746161268, at `fbf87000381c5ee79bef0a245621286815636c59`, emitted the same invalid-vptr
  warnings for a valid external polymorphic object and std::thread under actual EMFILE.
  It completed with exit 0 despite the warnings: **not counted as a sanitizer PASS**.
  The uninstrumented control completed without that diagnostic. Full logs are preserved.

The [GCC 13.3 runtime memory-access check](https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.3.0/libsanitizer/sanitizer_common/sanitizer_posix_libcdep.cpp#L250)
returns false if it cannot create a pipe. The [vptr checker](https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.3.0/libsanitizer/ubsan/ubsan_type_hash_itanium.cpp#L181)
uses that check before accepting a vtable. Together with the independent reproduction,
this identifies a sanitizer-runtime limitation under complete descriptor exhaustion;
it does not establish that every possible vptr report is false.

Real EMFILE and shutdown tests remain enabled on Linux normal and TSan configurations.
The **ASan/UBSan plus complete-FD-exhaustion combination is NOT RUN** in the final matrix,
with an explicit CMake message. No sanitizer is suppressed or disabled on component code.
C++ worker allocation denial remains fully enabled in ASan/UBSan. The independent
diagnostic control is retained for manual investigation; future control builds enforce
nonrecovering diagnostics, but that future invocation has not been claimed as executed.
