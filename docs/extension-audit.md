# Extension audit and implementation plan

This internal implementation review continues the existing 0.1 implementation. It does not replace the worker,
transaction, ownership, or callback architecture. The baseline was checked against commit
`ce4e70d` and the working tree, including the PostgreSQL fixture corrections. That baseline
and the 42-native count below are historical. The current API has 69 natives; see the
[native reference](native-reference.md) and [current readiness](rc-readiness.md). No section
of this document records an independent GPT-6 Astra audit.

## Verified baseline

The engine already provides service-profile connections, exclusive session workers,
FIFO lanes, immutable parameter snapshots, homogeneous batches, explicit transactions,
bounded retained results, cancellation, reconnection without replay, and VM-generation
ownership. The existing 42 natives, examples, and real open.mp loading tests remain the
compatibility baseline. There is no legacy SA-MP plugin entry point.

The latest baseline passed 26 unit and 146 integration checks on Windows x86, Windows x64,
Linux x86, and Linux x64. The Linux x64 run included ASan, UBSan, and leak detection. These
counts describe the baseline, not the extension tests added afterward.

## Stabilization on October 13, 2026

The current code includes the query shortcut, heterogeneous batches, exact scalar transport,
nullable typed arrays, result OIDs, lane metrics, explicit BEGIN options, savepoints, and
bounded streaming. The API and security documents now describe those contracts. The original
baseline above is historical; the [validation record](validation.md) distinguishes each
expanded suite from it.

Reproducible regressions were corrected without changing the execution architecture:

- The BOOLEAN array test expected native `t` from an explicit `::text` cast, which actually
  returns `true`. Diagnostics now identify the scalar/array OIDs and comparison, and all
  sixteen array types are checked in prepared, direct, and heterogeneous execution.
- The extension gamemode used an indexed string slot without an explicit capacity. Pawn's
  default `sizeof` did not supply the slot width, leaving an invalid empty BIGINT element.
  Explicit 24-cell capacities restore the real server test. Bootstrap prototypes also use
  explicit forwards, and CI treats Pawn warnings as errors.
- The Linux console can replace its single pending command when unload and GMX are sent
  together. The server harness waits for unload acknowledgement before testing reloads;
  it still fails if the script remains loaded or receives a late callback.

COPY transfer and receiving LISTEN/NOTIFY remain excluded from the 0.1 candidate scope.
They require independently bounded protocol state machines and lifecycle tests; exposing
them through ordinary query execution would leave transfers or notification queues unmanaged.
The existing streaming consumer addresses large row retrieval while this scope is qualified.
No pending capability is advertised as implemented. Pipeline and automatic lane selection
remain optional measurements-driven work, not prerequisites for the sequential engine.

## Findings by category

| Category              | Finding and decision                                                                                                                                                                                                                                                                     |
| --------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Infrastructure defect | The first GitHub run failed before integration tests because Linux selected a protected Unix socket directory and Windows initdb dropped privileges against Python 3.13's temporary-directory ACL. Use loopback TCP and grant only the current user access to the fresh Windows fixture. |
| Infrastructure defect | A failed startup wait could leave the disposable postmaster running. Clean up its PID file without masking the original startup error.                                                                                                                                                   |
| Toolchain defect      | GCC 12 ASan PIE startup failed before main on the local high-entropy-ASLR Linux host, including a minimal program. A non-PIE sanitizer test executable passed the comparison; the component remains position independent.                                                                |
| Bridge robustness     | Recording a diagnostic inside a noexcept native wrapper can itself allocate. Make this best effort so allocation failure cannot escape the wrapper.                                                                                                                                      |
| Fundamental API gap   | A job could repeat only one SQL statement. Add bounded heterogeneous snapshots, retaining FIFO ordering, the same submission path, and explicit partial outcomes.                                                                                                                        |
| API ergonomics        | Add a zero-parameter query shortcut with no intermediate public statement handle. Values still require bind functions.                                                                                                                                                                   |
| Result inspection     | Preserve PostgreSQL column type OIDs, expose column names and item outcomes, and keep checked conversions.                                                                                                                                                                               |
| Type coverage         | Reuse typed text transport for exact numerics and temporal values; add byte arrays and nullable typed arrays with real PostgreSQL validation.                                                                                                                                            |
| Observability         | Expose nonblocking lane gauges and cumulative counters, plus separate queue, execution, and delivery timings.                                                                                                                                                                            |
| Advanced transactions | Add explicit isolation/read-only options and controlled savepoint recovery on the reserved session.                                                                                                                                                                                      |
| Large results         | Add an opt-in bounded row-block channel to the existing execution path. Published rows remain provisional until terminal success.                                                                                                                                                        |
| Optional optimization | Keep explicit lane selection and the single unnamed plan until comparative measurements justify additional policies.                                                                                                                                                                     |

## Dependency order

1. Preserve the verified baseline and correct fixture/toolchain failures.
2. Extend job metadata and heterogeneous submission without changing pg_add semantics.
3. Extend the bridge, exact types, and result inspection; validate ownership and rejection.
4. Add transaction options and observable session statistics.
5. Introduce bounded streaming with slow-consumer, cancellation, and unloading tests.
6. Run the expanded PostgreSQL and open.mp suites on declared platforms, then update the
   validation record and compilable examples.

Each extension must keep rejected input recoverable, prevent worker access to Pawn memory,
and publish results before terminal status. No automatic replay is introduced. Existing
native signatures remain valid; additions do not constitute a stable-API compatibility promise.

## Advanced protocol decisions

Streaming, COPY, and notifications are distinct protocols, not aliases for batch execution.
[libpq row mode](https://www.postgresql.org/docs/17/libpq-single-row-mode.html) supports
incremental delivery, but previously delivered rows must be discarded or reconciled if
the statement ultimately fails. Streaming therefore needs bounded publication and an
explicit consumer lifecycle before it can be exposed to Pawn.

[COPY](https://www.postgresql.org/docs/17/libpq-copy.html) requires directional protocol
states, bounded input/output buffers, and draining the final command result. It remains
excluded from ordinary SQL execution. No COPY native is advertised until those states and
failure paths are implemented and tested.

[LISTEN/NOTIFY](https://www.postgresql.org/docs/17/libpq-notify.html) needs an independently
polled listener, bounded notification storage, subscription ownership, and resubscription.
A dedicated connection is preferable to delaying events behind long queries. It is not a
persistent message queue; notifications during disconnection cannot be recovered. The
current extension rejects LISTEN/UNLISTEN through the ordinary query path, which cannot
drain notifications. This also prevents unbounded notification retention inside libpq.
Sending a notification remains possible with a normal parameterized
`SELECT pg_notify($1, $2)` statement; a receiving API is not advertised.

Pipeline, named-plan caches, and automatic lane selection remain optional optimizations.
Their evaluation must include error correlation, session resets, memory, and ordering,
not only transport throughput. Existing sequential execution and explicit lanes remain
usable without these optimizations.

## Named SQL section extension

`pg_prepare_section` follows the existing logical statement/worker path; it adds no connection
or synchronous query API. Its lexer and job-local file/selection index are bounded by existing
SQL/batch byte settings and a fixed 1,024-section cap. The bridge exposes 69 natives.
Unit/adversarial, real-engine/database and extracted-package server checks are described in
[validation](validation.md); see [the full contract](sql-sections.md). These implementation
notes are not an independent GPT-6 Astra audit or a Release Candidate qualification.
