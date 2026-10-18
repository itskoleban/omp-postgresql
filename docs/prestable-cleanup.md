# Pre-stable API cleanup and diagnostics

## Baseline and intentional compatibility break

Inspected default-branch HEAD 2add8df604f71ee9336f7483ee9bb8922f921e48 on October 18, 2026,
after owner integration of PR #16. Master CI 39162713501 succeeded. No open PR or
other remote feature branch duplicated this task. Existing release 0.1.0-experimental
was already published on October 15; candidate code remains 0.1.0-rc.1, unpublished by
this task. Dedicated branch: codex/prestable-debug-cleanup. Five pre-existing document
formatting changes are excluded from task commits.

The baseline already implements unified optional file sections and owned custom final
callbacks on all eleven async natives. Those architectures are retained. The redundant
pg_prepare_section native, registration/dispatcher branch and Engine wrapper are removed: **69 -> 68 natives**, three unchanged forwards. Named SQL lexer, source enum, worker capture,
preflight, path confinement, UTF-8 and per-job file changes remain. No cache is added.

Migrate pg_prepare_section(c, file, section, count, prepared) to
pg_prepare_file(c, file, count, prepared, section). Empty/omitted section means whole-file SQL;
nonempty selects exactly one name without fallback. **Recompile affected AMX files**: previously
compiled AMX requiring the removed native will no longer load/work with this component.
Distribute include and component together. Other native names, leading positions, defaults
and old arities remain, including four-argument pg_prepare_file. Custom i/f/b/s callbacks,
OnPgComplete, OnPgConnection, OnPgRows, notify=false and explicit job ownership remain.

## Responsibility decisions

| Responsibility                                             | Decision                | Reason                                                                     |
| ---------------------------------------------------------- | ----------------------- | -------------------------------------------------------------------------- |
| Section-only native and Engine wrapper                     | Removed                 | Unified file preparation covers every supported use; no remaining callers. |
| Section lexer/source-kind/capture and tests                | Retained/migrated       | Still required by optional section selection; captures remain per job.     |
| Callback descriptors/common job factory                    | Retained                | Already implemented; bounded owned copies and generation dispatch.         |
| Transaction/savepoint, batch/stream, typed binders/results | Retained                | Distinct semantics, lifetimes and recovery intent.                         |
| Logger event formatting/queue handoff                      | Internally consolidated | One bounded typed path shared by workers and main thread.                  |
| Further compatibility/helper deletions                     | Deferred                | No additional deletion justified by actual usage analysis.                 |
| Raw diagnostics, callback arrays/custom rows/caches        | Deferred                | Outside bounded scope; privacy and lifecycle cost not justified.           |

## Complete current API classification

A = essential distinct; B = useful convenience; C = genuine duplicate responsibility
now consolidated; D = distinct public semantics with shared internal code. E requires
an unclear/obsolete contract; no additional removal is justified here.

| Native                  | Category | Decision and rationale                                                                     |
| ----------------------- | -------- | ------------------------------------------------------------------------------------------ |
| `pg_connect`            | A        | Unique connection lifecycle/state responsibility; retain.                                  |
| `pg_connection_close`   | A        | Unique connection lifecycle/state responsibility; retain.                                  |
| `pg_connection_state`   | A        | Unique connection lifecycle/state responsibility; retain.                                  |
| `pg_connection_ready`   | A        | Unique connection lifecycle/state responsibility; retain.                                  |
| `pg_connection_message` | A        | Unique connection lifecycle/state responsibility; retain.                                  |
| `pg_lane_state`         | A        | Distinct physical-lane state, queue or cumulative metric; retain.                          |
| `pg_lane_pending`       | A        | Distinct physical-lane state, queue or cumulative metric; retain.                          |
| `pg_lane_active`        | A        | Distinct physical-lane state, queue or cumulative metric; retain.                          |
| `pg_lane_stat`          | A        | Distinct physical-lane state, queue or cumulative metric; retain.                          |
| `pg_query`              | B        | Direct one-shot SQL avoids staging; shares job submission, not preparation semantics.      |
| `pg_prepare`            | A        | Inline logical statement creation; distinct from worker-side file loading.                 |
| `pg_prepare_file`       | A        | Unified whole-file and exact named-section preparation; retained worker capture semantics. |
| `pg_bind_int`           | B        | Specialized encoding/type safety improves common calls; retain alongside generic binders.  |
| `pg_bind_int64`         | B        | Specialized encoding/type safety improves common calls; retain alongside generic binders.  |
| `pg_bind_float`         | B        | Specialized encoding/type safety improves common calls; retain alongside generic binders.  |
| `pg_bind_bool`          | B        | Specialized encoding/type safety improves common calls; retain alongside generic binders.  |
| `pg_bind_null`          | B        | Specialized encoding/type safety improves common calls; retain alongside generic binders.  |
| `pg_bind_string`        | B        | Specialized encoding/type safety improves common calls; retain alongside generic binders.  |
| `pg_bind_int_array`     | B        | Specialized encoding/type safety improves common calls; retain alongside generic binders.  |
| `pg_bind_string_array`  | B        | Specialized encoding/type safety improves common calls; retain alongside generic binders.  |
| `pg_bind_bytes`         | B        | Specialized encoding/type safety improves common calls; retain alongside generic binders.  |
| `pg_bind_array`         | A        | Generic typed/nullable array encoding; dimensions and OID differ from specialized binders. |
| `pg_add`                | A        | Immutable parameter snapshot transfer; distinct from enqueue.                              |
| `pg_execute`            | A        | Homogeneous staged execution; retain batch/stream distinctions.                            |
| `pg_statement_close`    | A        | Explicit lifetime/ownership release.                                                       |
| `pg_batch_create`       | A        | Heterogeneous flattening/ownership/submit contract; no opaque execution mode.              |
| `pg_batch_add`          | A        | Heterogeneous flattening/ownership/submit contract; no opaque execution mode.              |
| `pg_batch_execute`      | A        | Heterogeneous flattening/ownership/submit contract; no opaque execution mode.              |
| `pg_batch_close`        | A        | Explicit lifetime/ownership release.                                                       |
| `pg_stream_execute`     | A        | Stream producer/block selection/release/backpressure are separate responsibilities.        |
| `pg_stream_next`        | A        | Stream producer/block selection/release/backpressure are separate responsibilities.        |
| `pg_stream_release`     | A        | Stream producer/block selection/release/backpressure are separate responsibilities.        |
| `pg_begin`              | B        | Useful server-default isolation convenience; does not force explicit READ COMMITTED.       |
| `pg_begin_ex`           | A        | Explicit isolation/read-only control; semantically distinct from inherited defaults.       |
| `pg_savepoint`          | D        | Distinct transaction command name; common validated construction/submission.               |
| `pg_rollback_to`        | D        | Distinct transaction command name; common validated construction/submission.               |
| `pg_release_savepoint`  | D        | Distinct transaction command name; common validated construction/submission.               |
| `pg_commit`             | D        | Distinct transaction command name; common validated construction/submission.               |
| `pg_rollback`           | D        | Distinct transaction command name; common validated construction/submission.               |
| `pg_job_status`         | A        | Terminal publication/cancellation have distinct meanings and sentinels.                    |
| `pg_job_close`          | A        | Explicit lifetime/ownership release.                                                       |
| `pg_cancel`             | A        | Terminal publication/cancellation have distinct meanings and sentinels.                    |
| `pg_job_completed`      | D        | Distinct job metadata; share owner lookup/terminal validation where appropriate.           |
| `pg_job_size`           | D        | Distinct job metadata; share owner lookup/terminal validation where appropriate.           |
| `pg_job_item_status`    | D        | Distinct job metadata; share owner lookup/terminal validation where appropriate.           |
| `pg_job_error_index`    | D        | Distinct job metadata; share owner lookup/terminal validation where appropriate.           |
| `pg_job_error`          | D        | Distinct job metadata; share owner lookup/terminal validation where appropriate.           |
| `pg_job_message`        | D        | Distinct job metadata; share owner lookup/terminal validation where appropriate.           |
| `pg_job_duration`       | D        | Distinct job metadata; share owner lookup/terminal validation where appropriate.           |
| `pg_job_queue_time`     | D        | Distinct job metadata; share owner lookup/terminal validation where appropriate.           |
| `pg_job_latency`        | D        | Distinct job metadata; share owner lookup/terminal validation where appropriate.           |
| `pg_job_connection`     | D        | Distinct job metadata; share owner lookup/terminal validation where appropriate.           |
| `pg_result_count`       | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_rows`        | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_columns`     | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_column`      | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_column_name` | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_column_type` | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_is_null`     | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_string`      | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_bytes`       | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_int`         | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_float`       | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_bool`        | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_affected`    | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_sqlstate`    | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_result_message`     | D        | Distinct result dimensions/types/conversions; shared lookup/output/address validation.     |
| `pg_last_error`         | E        | Local diagnostic is cleared by other natives; keep distinct from asynchronous job errors.  |

| Forward        | Category | Decision                                                       |
| -------------- | -------- | -------------------------------------------------------------- |
| OnPgConnection | A        | Main-thread observed connection lifecycle, optional.           |
| OnPgComplete   | A        | Supported global completion alternative, explicit job release. |
| OnPgRows       | A        | Stream block notification independent of final custom handler. |

## Diagnostics implementation

See [operator guide](debugging.md): strict startup level (WARN default), immutable numeric
records, fixed 256-entry ring, nonblocking try-lock/drop counters, at most 63 records and
one summary per main-thread tick, 512-byte formatting buffer. Engine owns logger before
lanes and joins them before destruction. No worker touches open.mp logging; no external
framework, disk flushing, SQL/private text or new Pawn native. Separate queue, execution
and completion-wait timings use monotonic clocks.

## Qualification evidence

Baseline: Windows x86 Release executables built from ef3b0a0 (engine/native sources byte-identical
to integrated 2add8df), MSVC 19.51, patched libpq 17.6, disposable TLS PostgreSQL 17.11:
15/15 CTest passed, 0 skipped, 65.61 seconds. This is explicitly reused binary evidence,
not a rebuild of merge identity. Current-source and final CI results are recorded below
when executed. Earlier audits/benchmarks remain revision-specific.

Local implemented-source qualification at `67cdaccf5a42350523303fff5548e386c07b006e`:

- Windows 11 10.0.26300 x64, Intel Core i5-13420H (8 cores/12 logical processors),
  MSVC 19.51.36248 Release x86, patched libpq 17.6, TLS PostgreSQL 17.11, seed 42.
- All **17/17 CTest** executed successfully, no skips, 65.99 seconds. Includes the
  real lost-COMMIT/stall/auth/TLS/lifecycle tests, migrated SQL section suite and logger
  integration/benchmark. Source hashes match this exact Git revision; the cached CMake
  package metadata still names baseline 2add8df, so this local binary is not used as a
  distributable artifact. Clean CI packages must carry their own exact source identity.
- All **47 Python regressions** passed, 12.631 seconds; all **12 Pawn examples** compile
  with warnings as errors (3.10.11). Documentation checker reports 68 natives/3 callbacks.
- Official **open.mp 1.5.8.3079 Windows x86** passed nine unattended gamemode modes,
  pending filterscript unload and four smoke GMX passes. Actual new WARN records appeared
  through the main-thread server sink. The disposable cluster stopped; no publication.
- Logging unit tests measured a 20,584-byte Diagnostics object (256 x 80-byte records plus
  state); no heap allocation in normal emit, fixed drain stack and bounded output. This
  is a storage measurement, not a measured bound on total process RSS.
- [Raw logging/callback samples](https://github.com/itskoleban/omp-postgresql/blob/8beceb53ab25b4c37092ebf184a29ebe08f2fb56/tests/benchmarks/windows-x86-logging.csv) record
  112 measurements with exact source hashes; [performance interpretation](performance.md#pre-stable-diagnostics-measurements).

The first new integration run passed 16/17: its sentinel SQL string crossed the test
double's valid heap boundary and the native correctly rejected it. Commit 67cdacc moves
the fixture strings into disjoint valid memory; the full rerun above passes. No source
coverage was deleted to pass. A first local launcher attempt lost child output; it is not
counted as qualification. Superseded/cancelled CI runs are not successful evidence.

## Independent review and corrections

A separate read-only reviewer audited d06b15f against baseline 2add8df, then re-reviewed
d1dc54e. Confirmed P2 contradictory migration guidance in callbacks/design/extension-audit
and P3 overflow emitting WARN at ERROR were corrected in d1dc54e. All-enabled-level
overflow regressions and package-guide checks were added. Sink-failure counts now produce
a bounded recovery summary even without overflow. No remaining concrete memory/lifetime,
logger race, callback or transaction defect was identified within that review's scope.
This is a new independent agent pass, not a new Astra audit or a bug-free certification.
Reviewer package tests hit sandbox Temp permissions; its two targeted tests passed and the
parent's complete 47-test run passed outside that sandbox. No reviewer DB/build run is claimed.

## Remaining release gates

Final branch CI/packages/sanitizers must be inspected with actual artifacts on the exact
preview source. A successful PR preview does not qualify a future integrated master commit.
Owner review and explicit authorization precede merge/tag/RC publication. Interactive
player/provider flows, fully instrumented dependencies, hours-long fault/soak, real Pawn
interpreter/tick comparative latency, CPU/RSS profiles and slow real output-device performance
remain unqualified. Synthetic bounded sinks establish progress/storage behavior, not an
upper bound on the time taken by an arbitrary server output sink. No production/stable
readiness claim follows from this finite qualification.
