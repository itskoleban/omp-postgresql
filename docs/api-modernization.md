# Pre-RC API modernization audit

## Inspected baseline and scope

Inspected current local/remote master `7a69a5bde95a5461e2a3e7c129907eabde674f28`,
October 18, 2026. Recent PRs #13 (transaction recovery/security corrections), #14 (RC
preparation) and #15 (documentation/tutorials) are integrated. Remote branch inventory
contained only master, and no open PR carried this work. Master
[CI 39147241370](https://github.com/itskoleban/omp-postgresql/actions/runs/39147241370)
completed all seven jobs successfully. Existing version remains 0.1.0-rc.1; this change
does not authorize a tag, merge or Release.

Recovered functionality: 69 natives, fixed completion/row callbacks, named-section lexer,
worker-side bounded UTF-8/confined file reading, per-job named capture, immutable bindings,
homogeneous/heterogeneous batches, transactions/savepoints, bounded streams, generation
ownership, explicit result release, Windows libpq correction, sanitizer/stress/runtime and
deterministic x86 packaging infrastructure. Existing documentation and qualifications remain
historical evidence for their exact commits, not automatic qualification of these changes.

Baseline run: Windows x86 Release, patched libpq 17.6, disposable TLS PostgreSQL 17.11,
seed 42: all 15 CTest cases passed without skips, 64.30 seconds. Repository inspection
covered declarations/registration/address validation, component dispatch, Engine admission
and ownership, Lane worker/capture/transactions/streaming, statements/parameters, tests,
examples, docs and both workflows. Five pre-existing document-format edits were preserved
separately from the feature.

## Classification of every public native

A = essential distinct functionality; B = useful convenience; C = genuine consolidation;
D = public distinction with internal reuse; E = potentially confusing interface requiring
an explicit contract. These are primary categories, not a recommendation to remove D/E.

| Native | Category | Decision and rationale |
| --- | --- | --- |
| `pg_connect` | A | Unique connection lifecycle/state responsibility; retain. |
| `pg_connection_close` | A | Unique connection lifecycle/state responsibility; retain. |
| `pg_connection_state` | A | Unique connection lifecycle/state responsibility; retain. |
| `pg_connection_ready` | A | Unique connection lifecycle/state responsibility; retain. |
| `pg_connection_message` | A | Unique connection lifecycle/state responsibility; retain. |
| `pg_lane_state` | A | Distinct physical-lane state, queue or cumulative metric; retain. |
| `pg_lane_pending` | A | Distinct physical-lane state, queue or cumulative metric; retain. |
| `pg_lane_active` | A | Distinct physical-lane state, queue or cumulative metric; retain. |
| `pg_lane_stat` | A | Distinct physical-lane state, queue or cumulative metric; retain. |
| `pg_query` | B | Direct one-shot SQL avoids staging; shares job submission, not preparation semantics. |
| `pg_prepare` | A | Inline logical statement creation; distinct from worker-side file loading. |
| `pg_prepare_file` | C | Whole-file and section preparation now share the recommended optional-section interface. |
| `pg_prepare_section` | C | Compatibility entry point for existing source/AMX; shares checked file creation. |
| `pg_bind_int` | B | Specialized encoding/type safety improves common calls; retain alongside generic binders. |
| `pg_bind_int64` | B | Specialized encoding/type safety improves common calls; retain alongside generic binders. |
| `pg_bind_float` | B | Specialized encoding/type safety improves common calls; retain alongside generic binders. |
| `pg_bind_bool` | B | Specialized encoding/type safety improves common calls; retain alongside generic binders. |
| `pg_bind_null` | B | Specialized encoding/type safety improves common calls; retain alongside generic binders. |
| `pg_bind_string` | B | Specialized encoding/type safety improves common calls; retain alongside generic binders. |
| `pg_bind_int_array` | B | Specialized encoding/type safety improves common calls; retain alongside generic binders. |
| `pg_bind_string_array` | B | Specialized encoding/type safety improves common calls; retain alongside generic binders. |
| `pg_bind_bytes` | B | Specialized encoding/type safety improves common calls; retain alongside generic binders. |
| `pg_bind_array` | A | Generic typed/nullable array encoding; dimensions and OID differ from specialized binders. |
| `pg_add` | A | Immutable parameter snapshot transfer; distinct from enqueue. |
| `pg_execute` | A | Homogeneous staged execution; retain batch/stream distinctions. |
| `pg_statement_close` | A | Explicit lifetime/ownership release. |
| `pg_batch_create` | A | Heterogeneous flattening/ownership/submit contract; no opaque execution mode. |
| `pg_batch_add` | A | Heterogeneous flattening/ownership/submit contract; no opaque execution mode. |
| `pg_batch_execute` | A | Heterogeneous flattening/ownership/submit contract; no opaque execution mode. |
| `pg_batch_close` | A | Explicit lifetime/ownership release. |
| `pg_stream_execute` | A | Stream producer/block selection/release/backpressure are separate responsibilities. |
| `pg_stream_next` | A | Stream producer/block selection/release/backpressure are separate responsibilities. |
| `pg_stream_release` | A | Stream producer/block selection/release/backpressure are separate responsibilities. |
| `pg_begin` | B | Useful server-default isolation convenience; does not force explicit READ COMMITTED. |
| `pg_begin_ex` | A | Explicit isolation/read-only control; semantically distinct from inherited defaults. |
| `pg_savepoint` | D | Distinct transaction command name; common validated construction/submission. |
| `pg_rollback_to` | D | Distinct transaction command name; common validated construction/submission. |
| `pg_release_savepoint` | D | Distinct transaction command name; common validated construction/submission. |
| `pg_commit` | D | Distinct transaction command name; common validated construction/submission. |
| `pg_rollback` | D | Distinct transaction command name; common validated construction/submission. |
| `pg_job_status` | A | Terminal publication/cancellation have distinct meanings and sentinels. |
| `pg_job_close` | A | Explicit lifetime/ownership release. |
| `pg_cancel` | A | Terminal publication/cancellation have distinct meanings and sentinels. |
| `pg_job_completed` | D | Distinct job metadata; share owner lookup/terminal validation where appropriate. |
| `pg_job_size` | D | Distinct job metadata; share owner lookup/terminal validation where appropriate. |
| `pg_job_item_status` | D | Distinct job metadata; share owner lookup/terminal validation where appropriate. |
| `pg_job_error_index` | D | Distinct job metadata; share owner lookup/terminal validation where appropriate. |
| `pg_job_error` | D | Distinct job metadata; share owner lookup/terminal validation where appropriate. |
| `pg_job_message` | D | Distinct job metadata; share owner lookup/terminal validation where appropriate. |
| `pg_job_duration` | D | Distinct job metadata; share owner lookup/terminal validation where appropriate. |
| `pg_job_queue_time` | D | Distinct job metadata; share owner lookup/terminal validation where appropriate. |
| `pg_job_latency` | D | Distinct job metadata; share owner lookup/terminal validation where appropriate. |
| `pg_job_connection` | D | Distinct job metadata; share owner lookup/terminal validation where appropriate. |
| `pg_result_count` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_rows` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_columns` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_column` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_column_name` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_column_type` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_is_null` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_string` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_bytes` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_int` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_float` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_bool` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_affected` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_sqlstate` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_result_message` | D | Distinct result dimensions/types/conversions; shared lookup/output/address validation. |
| `pg_last_error` | E | Local diagnostic is cleared by other natives; keep distinct from asynchronous job errors. |

## Design alternatives and decisions

| Proposal | Assessment | Decision |
| --- | --- | --- |
| Append optional section to pg_prepare_file | One extra string preserves leading positions. Include-only change would break fixed arity validation. | Accept exactly old 4 or new 5 cells; old empty/whole-file semantics preserved; section wrapper shares implementation. |
| Replace both file natives | Would break old AMX native lookup and clearer migration. | Retain pg_prepare_section indefinitely as compatibility entry point. |
| Append callback/format/varargs to existing submissions | Uniform, atomic pre-submit copy; long signatures but no new handles or duplicated native inventory. Pawn 3.10.11 verified fixed strings/defaults and by-reference variadic scalar ABI. | Accept exact legacy count or complete bounded tail; reject partial/misaligned tails before reading arguments. |
| Add callback-enabled variant of every async native | Clear separate ABI but duplicates eleven public entry points and examples; no safety advantage once old arities are explicitly accepted. | Rejected. |
| Shared persistent callback descriptor handles | Adds registry, owner limits, mutation/close/reuse rules and another handle lifetime for simple callbacks. | Rejected; one optional owned descriptor travels with each accepted job. |
| Register callback after submission | Potential completion/configuration ordering hazard and orphaned request on registration failure. | Rejected; descriptor installed before enqueue. |
| Custom stream row and final handlers together | Requires two descriptor lifetimes and new row payload/backpressure/ABI surface. | Bounded scope: custom final callback; retain OnPgRows(job,context). |
| Callback arrays/references | Need lengths, copy direction, aggregate VM heap accounting and new callback declaration semantics. | Deferred; i/f/b/s cover player/session/request data safely. Unknown formats reject. |
| Merge begin/begin_ex | Would erase inherited session isolation versus explicit options unless more sentinels/modes are exposed. | Retain both; shared job factory handles notification metadata. |
| Merge commit/rollback or savepoint actions | Distinct transaction intent matters for recovery and review. | Retain names; reuse construction/dispatch internally. |
| Merge execute/batch/stream | Different staging transfer, results and producer/consumer lifetimes. | Retain distinct functions. |
| Merge specialized/generic array binders | Specialized int/string layouts offer safety and smaller signatures; generic supports OIDs and NULL masks. | Retain all; existing encoding helpers remain shared. |
| Merge rows/affected or job_message/last_error | Result rows versus changed rows, and async versus local errors, are different observations. | Rejected. |
| Replace the SQL lexer or add a global file/public cache | Existing lexical/file-capture guarantees already qualified; cache invalidation adds lifecycle risk. | Reuse lexer and per-job capture without persistent caches. |

The source-kind enum replaces a boolean across Statement/Operation/Job, making dispatch
explicit without changing SQL strings, parser or capture ordering. One job factory installs
owner, connection/lane/transaction/context, notify and callback data for the six internal
creation paths. Callback-enabled public transaction/savepoint variants reuse those paths.
No SQL execution implementation is duplicated.

## Final API and ownership

Native inventory remains 69. Twelve declarations change: pg_prepare_file and eleven
asynchronous submissions. [The native reference](native-reference.md) contains all exact
signatures. [The callback guide](callbacks.md) defines the i/f/b/s format, 16-argument,
31-byte name, 1024-byte string and 4096-byte logical payload limits, context semantics,
missing/error policy, unload/reload and explicit ownership rules. Old Pawn binaries and
unchanged source retain OnPgComplete. New include/AMX requires the upgraded component.
There is no supported external C++ binary ABI.

Custom completion is exclusive with OnPgComplete and covers all terminal statuses.
notify=false suppresses rows/completion and discards metadata. A missing/erroring custom
public never closes SQL results automatically; retained IDs support recovery. A live BEGIN
must still be released only after its finish job dispatch. Arguments survive job closure
inside a callback and are released after invocation; unload releases metadata immediately.

Runtime C++ changes: include/pg/{types,statement,engine,component}.hpp and
src/{engine,natives,component,session}.cpp. Pawn include changes preserve names/leading
parameters. Tests extend the actual registered native boundary, dispatch mock and real
open.mp callback/section scenarios. Examples update accounts/sections/callbacks and add
an interactive concurrent account/clan/property example. Existing global completion,
batch, transfer and stream examples remain functional.

## Validation and performance evidence

See [validation](validation.md) for current run evidence and its exact source identities.
Local final CTest: 15/15, no skips, 66.81 seconds; Python: 46/46; current Pawn examples:
12/12 warnings-as-errors. Fresh source server scenarios and separately compiled baseline
AMX scenarios passed, including old four-cell file calls and callback signatures.
Tests distinguish synthetic terminal-status routing from real SQL failure, queued
cancellation and COMMIT-response loss. Callback frame tests cover partial push, runtime
error, unsupported suspension and an already suspended main continuation. No skips are
counted as passes. Interactive gameplay/provider flows require operator validation.

The registered-native benchmark in tests/callback_cases.hpp uses a Pawn SDK test double
and real PostgreSQL jobs, not the Pawn interpreter. Eight rotated repetitions (one warmup,
seven reported), 64 jobs on two lanes, compare legacy global, custom with no extras, two
integers and a 96-byte string. Submission includes native copying/validation and Engine
enqueue, with concurrent workers; tick timing includes poll, public lookup, pushes and
test-double allocation/recording. It is reproducible via natives_integration under the
disposable PostgreSQL runner. No throughput threshold or claimed superiority follows.
Actual Pawn callback and server-tick percentiles remain unmeasured. Existing SQL file and
Engine benchmarks still run as correctness checks; no additional read/cache is introduced.

Raw data: [Windows callback CSV](https://github.com/itskoleban/omp-postgresql/blob/74a298ca364dc7750ea0d79bc07618e9629a3d45/tests/benchmarks/windows-x86-callbacks.csv).

## State and remaining review

Implementation, local qualification, CI qualification, integration and publication are
separate states. Feature branch: codex/api-custom-callbacks. The associated pull request records final CI and package evidence. The historical independent audit predates this code;
this change has an internal review, not a new independent model/security certification.
Remaining limits include interactive player/provider coverage, finite concurrency/soak
evidence, fully instrumented dependencies and actual Pawn dispatch/tick measurements.
Owner review and exact integrated-source qualification precede RC publication.
