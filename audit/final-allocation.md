# R1: terminal publication and worker allocation failures

Status at this source: fixes implemented; exact-head qualification must complete.
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
