# Focused concurrency and allocation audit

Baseline source: `d0494d2bf45c03757864af0f5dfa2c7b868f4810`.

The review covered `src/engine.cpp`, `src/session.cpp`, `src/component.cpp`,
`src/natives.cpp`, `src/diagnostics.cpp`, `src/log_file.cpp`, the corresponding
headers and existing lifecycle, stream, allocation and stress regressions. This is
a focused source review with one deterministic fault reproduction. It does not
prove the absence of all possible concurrency errors.

## Confirmed defect: terminal notification loss on main-thread allocation failure

In the baseline `Engine::poll` (`src/engine.cpp:484` onward), the completion is
popped from the intrusive queue and `Job::delivered` becomes true before
`out.jobs.push_back` at line 511. A `std::bad_alloc` at that vector growth loses
the notification permanently. Previously collected notifications in the same
local Dispatch are also destroyed if a later vector allocation throws. Connection
observation is updated before its event vector growth at lines 523-524, exposing
the same lost-event mechanism. `Component::onTick` calls poll directly at
`src/component.cpp:147` without catching that allocation exception.

This is a component allocation-recovery defect, independent of the intentional
TSan fixture's missed diagnostic. No evidence attributes the detector miss to
this defect, and the detector executable does not link the component.

An isolated scratch unit harness using the unchanged Engine implementation
reported `threw=1 delivered=1 recovered_notifications=0 retained_job=1`. The
maintained regression then reproduced the issue through public APIs with two
real, accepted PostgreSQL jobs. It waited for terminal status, disconnected and
waited for `Lane::stopped` so both queue pushes were complete. Denying one
main-thread allocation reported:

```
poll_allocation reserve=0 injection=MAIN_THREAD denied=1 threw=1 unchanged=0 recovered_jobs=1 recovered_connections=1
```

The original regression executable returned 1. Its SHA-256 is
`ae60c3b48c9714a8685ea1ec2e356198806818a79c1c24c393081fb91873f10a`.
The regression source SHA-256 is
`5bc0449648fd6787bb37d60235e1ec1817e49a053b3ed55e3c858cb05f1daf8c`.
The original Engine source SHA-256 is
`eb11cc1591b85af558fc2ae2f4bbe992b818590a594d0fef1f95e22a6fe6983f`.
The baseline compiler was MSYS2 GCC 16.1.0; libpq/server was PostgreSQL 18.4.
This Windows reproduction is separate from the requested Linux TSan runtime.

The narrow before/after comparison completed successfully in one newly created
loopback-only cluster, which was stopped in `finally`. Baseline exit was 1;
corrected exit was 0. Each corrected scenario reported `denied=1 threw=0
unchanged=1 recovered_jobs=2 recovered_connections=1`, for reserve indices 0, 1
and 2. The corrected executable SHA-256 is
`47bde7fc1ace396548c25c57ab6a50ea308f25e68010a63a5784bba1ccd8d1f3`.
The fixed Engine source SHA-256 is
`609cdfba9e3c657427369ab5598a299600e39d69d64cabacedf2cc6d333f24d5`.
Raw commands and independent stdout/stderr are retained locally in
`work/poll-runtime-qlhzjwak/results.json` and neighboring command logs. The
compact console comparison is `work/poll-public-before-after-comparison.log`.
Public evidence must replace personal workspace paths and keep its sanitized
identity separate from these originals.

The correction reserves all three Dispatch vector capacities before any pop or
delivery/connection mutation. The main-thread registries give upper bounds:
every live terminal or stream event belongs to one entry in `jobs_`, and every
connection event belongs to one entry in `connections_`. Concurrent workers can
publish additional completions during poll, but cannot add registry entries.
After successful reserve, shared-pointer and numeric Event insertion fit those
bounds and do not allocate. If reserve fails, poll returns an empty Dispatch and
the next tick can retry without consuming state or throwing into onTick.

The regression independently fails each of the three reserves and requires
unmodified delivery state, exactly one completion per job, preserved FIFO,
preserved final connection notification and no late duplicate. Normal and ASan
builds deny actual main-thread C++ allocation. TSan builds use named checkpoints
in the actual Engine translation unit and retain global sanitizer interceptors;
that narrower scope is disclosed by the test name and output.

## Reviewed ownership and synchronization

- `Engine` registries, reservation/ending maps, delivery bookkeeping, callbacks,
  Stream current blocks and owner-generation tombstones belong to the main
  thread under the explicit public API contract (`include/pg/engine.hpp:32`).
- Each Lane owns its `PGconn`, cached prepared statement, transaction state and
  libpq calls on one worker. The main thread never destroys or manipulates that
  connection. Lane destruction requests stop and joins its worker
  (`src/session.cpp:81-92`). All worker-visible fields are initialized before
  the last `thread_` member starts.
- Lane queue/diagnostic mutation is protected by `mutex_`; stop/cancel/state and
  statistics use atomics. Timed, predicate-based waits recheck stop and queue
  state, so a notification is not the sole condition for progress.
- The worker initializes result/error/timing/item fields before publishing a
  terminal atomic status and pushing the completion under the queue mutex.
  Native result readers require terminal status; queue consumption provides an
  additional synchronization boundary. The worker logging helper reads only
  immutable metadata, excluding main-thread callback/notify data.
- Accepted Job shared ownership exists in Engine, Lane/current work and the
  intrusive completion queue as required. The completion queue removes its
  next link during pop and destroys abandoned chains iteratively. Queue and
  Diagnostics lifetimes outlast Lane destruction/join.
- Stream pending ownership is mutex-protected, current ownership is main-only
  and block contents are const. Producer waits recheck cancellation, shutdown
  and deadline. `available_` is an atomic hint, not a replacement for the mutex.
- Cancellation and shutdown are cooperative; dispatched uncertain work is not
  automatically replayed. Worker recovery settles queued work with terminal
  failures before attempting a new physical session. Known SQL confirmation
  and partial item metadata are retained independently from delivery failure.
- Existing queued/running cancellation, shutdown, transaction-capacity,
  allocation checkpoints, stream pressure and owner-unload tests remain intact.

No separate component data race, duplicate worker terminal publication,
use-after-free, deadlock or lost wakeup was confirmed by this review. This
statement is bounded by source inspection and executed regressions; full CI and
TSan qualification must be reported separately at the final exact source SHA.

## Limitations and follow-up

No full system OOM, libpq/OpenSSL allocator exhaustion, arbitrary OS syscall
failure or unrestricted scheduler exploration was performed. Existing sanitizer
and total-descriptor limitations remain. A failure at an unmodeled standard
library/OS synchronization operation is not silently treated as impossible, but
no deterministic additional defect was established here. Keep such broader
fault-model work separate rather than expanding this correction without proof.

The first custom one-test run of the unchanged comprehensive PostgreSQL fixture
had a passing corrected CTest regression but an overall fixture FAIL afterward:
its required wire faults were not exercised by that deliberately narrow test
inventory. This is retained as FAIL, not counted as complete network/TLS
qualification. A narrow disposable runner and full-CI results are separate
evidence.

An earlier scratch narrow-runner attempt timed out because a spawned Windows
server inherited a captured pipe; it produced no component regression result.
Its database was independently stopped. The scratch runner was corrected to use
regular log files, as the repository fixture already does. That attempt remains
a runner FAIL and is not included in the successful before/after comparison.

The 24-hour campaign remains BLOCKED. No production infrastructure, release,
tag, merge, master mutation or prolonged campaign is part of this audit.
