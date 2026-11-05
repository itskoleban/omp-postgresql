# v0.1.0 handle decision and finite operating horizon

Date: 2026-11-04 (America/Bogota).
Baseline: `3a5df8439881bd514727a2c5e360f9ec0bafaf43`, tree
`21cea7c3b4620c361484be7aa76ad8f73132c545`.

## Decision and disposition

The owner directs v0.1.0 to retain the current jobs and positive 32-bit public
handles. No identifier reuse, MANUAL/AUTO/FALLBACK mode, or 64-bit public API is
authorized. Controlled reuse and exact internal identity are a separate
[v0.2.0 proposal](handles-v020-proposal.md).

The owner accepts considering the cumulative limit subject to a verifiable
operating profile. This records that conditional residual-risk disposition; it
does not establish a safe uptime for an arbitrary application. Application-wide
accounting, enforceable reaction bounds, and maintenance remain acceptance
conditions. A rate averaged over a short calibration is insufficient.

Status: **architecture DECIDED; numerical production profile PENDING; campaign
handle guard PENDING**. This decision is neither campaign approval nor release
approval. The separate transaction finish-ID correction requires review and
affected-code qualification before a replacement candidate can be frozen.

## Exact domain and allocation routes

`include/pg/types.hpp` defines `Id = std::int32_t`. `detail::take_handle` rejects
`next <= 0` and `next >= INT32_MAX`; `Engine::next_` starts at 1. Therefore:

```text
first ID                 = 1
last ID                  = 2,147,483,646 = INT32_MAX - 1
maximum IDs per Engine   = 2,147,483,646 = 2^31 - 2
exhausted cursor         = 2,147,483,647 (never issued)
```

IDs belong to one shared cursor for connections, statements, batches and jobs,
across all VMs/owners of that Engine. Live-object limits are separate from the
lifetime issuance limit. Closing, cancellation, owner unload, GMX/component
`reset()`, PostgreSQL restart and pool reconnect do not reset the cursor. A new
Engine/component lifetime is required; the operational procedure uses a complete
open.mp process stop and restart.

| Allocating path in `src/engine.cpp` | Public callers | Baseline issuance |
| --- | --- | --- |
| `connect -> id` | `pg_connect` | One connection ID, independent of pool size. |
| `prepare -> id` | `pg_prepare`; `pg_prepare_file` delegates here, including named sections. | One statement ID. |
| `create_batch -> id` | `pg_batch_create` | One batch ID. |
| `query -> submit -> id` | `pg_query` | One job ID. |
| `execute_statement -> submit -> id` | `pg_execute`, `pg_stream_execute` | One job ID per accepted execution, independent of parameter sets, rows or stream blocks. |
| `execute_batch -> submit -> id` | `pg_batch_execute` | One job ID, independent of the number of SQL operations. |
| `begin -> submit -> id` | `pg_begin`, `pg_begin_ex` | One job ID, also the transaction ID. |
| `savepoint -> submit -> id` | `pg_savepoint`, `pg_rollback_to`, `pg_release_savepoint` | One job ID for each command. |
| `finish -> submit -> id` | `pg_commit`, `pg_rollback` | One distinct job ID. |

There are no other baseline `Engine::id()` call sites or allocator cursor writers.
Native argument parsing, callback construction and other validation can fail
before these paths reach allocation; such a rejection need not consume an ID.
Binding, `pg_add`, batch transfer, reads, polling, stream next/release,
`pg_cancel`, closes, disconnect, worker retries/reconnects and internal cleanup
SQL do not create a new public ID themselves. A business operation or an
application retry can call several allocating natives and consume several IDs.

### Failed attempts also matter

On the baseline the cursor advances before these possible failures:

- `connect`: reservation-vector/lane construction, thread creation or registry
  insertion after its ID is taken. Physical connection failure after a
  successful return also retains the issued connection ID.
- `prepare` and `create_batch`: registry insertion can fail after allocation.
- `submit`: item-status allocation, job-registry insertion, the admission
  checkpoint or lane enqueue can fail after allocation. The registry entry is
  erased on queue rejection, but the ordinary cursor is not rewound. Restoring
  staged statement/batch input does not restore that spent ID.
- Any returned job that later ends FAILED, UNCERTAIN or CANCELLED still spent
  its ID. No additional ID is needed to publish or inspect that terminal result.

`prepare_file` validates its section name before delegating. BEGIN validation,
pool readiness, job limits, lane/transaction checks and the finish ending-marker
allocation can reject before an ID is taken. Counting all failed allocator
*attempts* conservatively covers either side of those boundaries.

The proposed finish-ID correction reserves **capacity**, not a preissued token.
Successful issuance and the complete-wave model below stay unchanged; accounting
must also protect one outstanding finish credit for each live transaction that
still needs one. A finish rejection may restore its unconsumed credit without
rewinding an issued cursor. Refer to the final patch and its deterministic tests
for exact acceptance boundaries; baseline evidence does not prove that fix.

## Observability without adding an engine API

Lane statistics expose SQL commands, job outcomes and reconnects, not total ID
issuance. Job/connection diagnostic records omit statement/batch allocation and
can be filtered or dropped. SQL/s, completed jobs/s, log lines/s and live handles
cannot be substituted for aggregate IDs/s.

An isolated highest returned handle is a lower bound on current issuance: later
or failed allocations may already have advanced the cursor. A controlled
**successful** `pg_batch_create(connection)` followed immediately by
`pg_batch_close(batch)` is a useful existing-API probe. With the verified
sequential allocator, returned `h` means exactly `h` IDs have been issued at that
instant, including IDs burned by earlier failures. The probe consumes one ID,
creates no SQL, and needs a valid owned connection and an available statement/
batch slot. The proposed capacity-only reservation preserves this property.

This is a source-derived measurement method, not a claim that an application or
supervisor currently runs it. It must execute on the owning main thread. Count
the probe itself. A failed probe must trigger fail-closed maintenance/escalation;
do not treat its zero return as an unchanged cursor, or retry it indefinitely.
Never use a fabricated stale handle as a cursor probe.

For a complete application measurement:

1. Record executable/source/dependency/config hashes, a process-start identity,
   all VMs and connection pool sizes. Start with a demonstrably fresh Engine or
   an exact successful probe; do not assume GMX starts at zero.
2. Centralize every allocating native listed above across all VMs. Increment a
   durable 64-bit **attempt** counter before dispatch, including retries and zero
   returns. Retain per-route and aggregate totals, timestamps and return IDs.
   Each current path consumes at most one new ID, so this intentionally
   overcounts preallocation rejections and bounds consumption above.
3. After a successful probe returning `h`, let `K` be the number of subsequent
   allocating attempts across all owners, including subsequent probes. Then
   `A_upper = h + K` is a conservative consumed-ID bound. On a fresh Engine,
   counting from the first attempt also gives a conservative bound from zero.
   Correlate probes/counters atomically on the main thread; reconcile differences
   rather than dropping failed calls from the ledger.
4. Track a conservative live transaction-credit bound `R`. A ledger can retain
   each accepted BEGIN until finish/failed-BEGIN cleanup is observed. If that
   ledger is uncertain, use the sum of configured pool lanes as an upper bound;
   at most one lane transaction can reserve each lane.
5. Demonstrate an **enforced upper envelope** `E(t) = B + ceil(r_max * t)` for all
   allocation attempts over any relevant interval, including bursts, retries,
   probes and administrative scripts. Observed means/percentiles do not prove
   `r_max`. Freeze counting coverage, admission control and response deadlines.
   A missing VM, reset counter, unaccounted plugin producer or log gap invalidates
   the claimed horizon and stops new business admission.

This adds application/operations accounting, not an engine mode or public API
change. No such application integration was run in this preparation.

## Prospective thresholds and maintenance

For the chosen profile define `A_upper`, domain `D`, live reserved credits `R`,
maximum detection-to-stop latency `T_stop`, planned maintenance lead `T_notice`,
and a bound `Q` on additional drain/reconciliation/probe IDs beyond already
reserved finish credits. Freeze all parameters before a campaign:

```text
free ordinary capacity F = D - A_upper - R
stop reserve             = E(T_stop) + Q
warning reserve          = E(T_stop + T_notice) + Q

warning / schedule maintenance when F <= warning reserve
reject new business work and BEGIN when F <= stop reserve
complete maintenance before F can fall below the drain requirement
```

A single allocating call must also satisfy its prospective weight: BEGIN needs
its own ID and an additional finish credit. A reserved COMMIT/ROLLBACK consumes
its credit, not ordinary admission capacity. Application rejection must still
allow these finishes and nonallocating poll/result/close/cancel operations.
The engine correction is a final boundary guard; it does not replace early
operational rejection. Close all admission immediately on lost accounting,
rate-envelope violation or insufficient time to reach maintenance.

For a desired remaining horizon `H`, use a bound `R_horizon` on the maximum
outstanding finish credits anywhere in the forecast interval, rather than only
the currently observed R. The sum of configured pool lanes provides such a
bound if future connection/pool growth is capped. A conservative acceptance
inequality is `A_upper + R_horizon + E(H + T_stop + T_notice) + Q < D`. Its units
are IDs and seconds, not SQL operations. Production admission may reserve that
same R_horizon in the prospective thresholds to cover newly begun transactions.
A profile without those demonstrated inputs has **PENDING
numerical thresholds**. No production r_max, burst, response SLA or restart
window was supplied, so this document does not invent a safe production quota.

Maintenance procedure:

1. Stop new business allocations across all VMs before the rejection threshold;
   retain counter/probe evidence and stop the campaign as a failure if its
   frozen horizon cannot be completed.
2. Continue polling accepted jobs and draining streams; reconcile every terminal
   exactly once. Finish valid transactions using reserved capacity. If a finish
   is rejected, preserve the error and close the physical/logical connection as
   required; a local zero return is no accepted finish job or confirmed rollback.
3. Reconcile UNCERTAIN writes against durable application operation identities.
   Never turn disconnection into a confirmed COMMIT or replay an uncertain write.
   Release results, statements, batches and connections; verify bounded cleanup.
4. Preserve accounting/evidence, stop open.mp completely, verify the previous
   process and fixture backends are gone, then start a new process. Check a new
   process identity/Engine lifetime and rebuild handles from initialization.
   Stored handles from the old process are invalid; never restore them as data.
5. Confirm operational readiness and a fresh horizon before reopening admission.

Maintenance acceptance therefore excludes indefinite uptime, applications that
cannot reject admission or restart, uninstrumented producers, and a horizon
shorter than their recovery/drain window.

## Reused, source-specific stress accounting

The previous static model in `focused-closure/review/handle-model-result.json`
is retained. For `tests/stress.cpp::load`, N complete successful waves consume:

```text
C(N) = 4 + 161*N + 11*ceil(N/32)
```

Derivation: setup connection/CREATE/INSERT = 3; per wave 16 direct queries,
16*(statement + job), 16*(statement + batch + 4 auxiliary statements + job),
and one SELECT oracle = 161. Mixed work starts at round 0 and repeats every 32
waves: two BEGIN/query/finish triples, one statement/stream pair, and side
connection/cancel-query/recovery-query = 11. Final DROP = 1. SQL operation count
differs from this allocation count. `audit_metric round` is zero-based and
precedes mixed work; allow the pending mixed work, an in-progress wave and
observation/stop lag when deriving a guard.

The retained N=202 calibration models 32,603 IDs; this is not a runtime cursor
measurement. The maximum complete waves with **no operational reserve** is
13,309,989 (2,147,483,551 IDs, only 95 left). It is not an operating quota.
Lifecycle creates independent Engines, with at most 38 IDs per temporary Engine;
its 10-round observer uses 14. Their domains do not add to the load Engine.

The observed mean projects approximately 23,368,880 IDs over 24 hours. That
projection is not an upper bound and cannot authorize a run. The minimum 20 ms
pause every 16 waves alone permits a far higher theoretical rate.

The previous review supplies a reproducible *conditional* stop calculation:

```text
W_over(delta) = 16*(floor(delta / 0.020) + 2)
R_over       = 161*W_over + 11*ceil(W_over/32) + 172
N_stop       = max N satisfying C(N) + R_over < D
N_warn       = max N satisfying C(N) + 2*R_over < D
```

The extra 172 covers a partial ordinary/mixed wave. For the previously proposed,
**not demonstrated**, combined stop latency delta=185 seconds, W_over=148,032,
R_over=23,884,210 IDs, N_warn=13,013,923 and N_stop=13,161,956. Static arithmetic
gives C(N_stop)+R_over=2,147,483,562<D; one more wave exceeds D. Warning reserves
one additional complete response interval for maintenance preparation. These
are conditional design values, **not frozen campaign settings or a safe quota**.

Before using them, prove every timeout/observer/kill route fits delta, validate
counter origin and current Engine/phase, prohibit other producers/profile
changes, and stop on stalled/backlogged sampling. Host scheduling is not a
hard real-time guarantee. A guard crossing must abort the campaign rather than
silently restart a new Engine and count the duration as continuous success.

The existing supervisor v2 explicitly reports finite-handle enforcement absent.
Its offline PASS does not validate this guard. No engine change is necessary
to implement that separate external guard; it still needs implementation and
deterministic boundary/lag/lost-counter tests plus bounded deployment proof.

## Requirements not yet demonstrated

- A reviewed/merged finish-ID correction and affected qualification on the new
  immutable candidate; the baseline permits a confirmed BEGIN without an ID
  guaranteed for its finish.
- Complete real application all-VM accounting, successful probe integration,
  enforced allocation envelope, credited-transaction ledger and maintenance SLA.
- A frozen production workload/horizon and numeric warning/rejection thresholds.
- A campaign guard bound to the source/profile/Engine identity, including lag,
  partial waves, stopping and fail-closed loss of observation.
- Real supervisor v2 deployment, relevant final resource calibration, and an
  explicitly authorized final campaign. The 24-hour campaign remains NOT RUN.

Existing allocator boundary tests and previous static arithmetic are reused;
this document does not relabel them as application deployment or campaign PASS.
