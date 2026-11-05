# v0.2.0 proposal: controlled handles with exact internal identity

Date: 2026-11-04. Status: **PROPOSAL ONLY; implementation NOT AUTHORIZED**.

v0.1.0 retains jobs, positive 32-bit public handles and no reuse. Its finite
domain and maintenance contract are documented in [the v0.1.0 decision](handles-v010.md).
The minimal transaction finish-capacity correction is independent of this
proposal. No API migration, allocator redesign or reuse is implemented here.

## Problem and constraints

The v0.1.0 shared monotonic cursor prevents a stale handle from selecting a later
object within one Engine lifetime, but limits cumulative issuance to
2,147,483,646. A slot pool could bound allocator storage by concurrent objects
and reduce issuance pressure. It must not weaken transaction, owner, callback,
terminal publication, result-retention or uncertainty semantics accidentally.

**Exact internal identity alone does not make a reused public int32 token safe.**
An old Pawn value numerically equal to a new token cannot reveal which object
the caller intended. With a finite token space and unbounded retention of stale
values, indefinite reuse and perpetual stale-handle rejection cannot both hold.
Splitting 31 bits into slot/generation fields merely postpones generation wrap.
The public guarantees must be revised explicitly before choosing an allocator.

## Proposed investigation

Retain the asynchronous job model and existing terminal categories. Represent an
internal operation by an exact, strongly typed identity containing the Engine
epoch, owner generation and monotonic nonwrapping serial, plus object kind as
appropriate. Use checked advancement; on serial exhaustion fail closed. A wider
internal domain is finite too and must not wrap silently.

Store public-token-to-object mappings separately. Worker queues, completion
publication, cancellation, streaming blocks, callbacks, transaction ownership and
retirement must carry/reference exact identity, not re-resolve a recycled token.
Owner generation checks must use the same exact identity at dispatch and after
callback reentry. Token generation/slot fields are lookup aids, not substitutes
for that identity.

Evaluate these alternatives against the public contract before selecting one:

| Alternative | Requirement/tradeoff |
| --- | --- |
| 32-bit slot + generation, retire slot before generation wrap | Bounded storage and useful reuse; finite total safe reuse. Quantify slot capacity, generation budget and exhaustion behavior. Preserve stale rejection within the declared epoch. |
| 32-bit token with explicitly limited stale-value validity | Requires an enforceable new public lifetime/epoch contract. A bare promise that applications discard old integers is insufficient. Explain how callers distinguish epochs and what errors are guaranteed. |
| Revised public capability/epoch or wider public representation | Separate owner-approved API migration, Pawn representation and compatibility work. Do not assume two cells or an int64 can replace one-cell signatures transparently. |
| Continue monotonic handles with maintenance | Simplest reference behavior. Retain if measured workloads and maintainability make added allocator complexity unjustified. |

No MANUAL/AUTO/FALLBACK execution modes are part of this investigation. No
automatic token reuse policy may silently change between application calls.

## Reclamation and admission invariants

- Accepted work owns immutable operation identity until terminal publication.
  Exactly one terminal is preserved, even after queue/worker/allocation failure.
- A public job remains valid until explicit permitted close; callback return
  cannot silently recycle it. Result/stream owners remain valid independently
  of registry removal until their final references are released.
- A slot is not eligible for public reuse until its old job is terminal,
  publication is consumed, the callback dispatch/reentry boundary is complete,
  transaction bookkeeping is retired and explicit close/unload semantics allow
  removal. Concurrent consumers retain exact identity, not a borrowed slot.
- Owner unload, disconnect, interrupted BEGIN/finish, destructor paths and late
  completions cannot free or modify a new occupant of an old slot.
- BEGIN reserves both terminal job capacity and finish identity capacity before
  admission. COMMIT/ROLLBACK consumes its own reservation once. A rejected
  attempt restores only unconsumed reservation state; accepted identity is never
  rewound or reassigned. Every cleanup path is idempotent for its exact identity.
- Preserve CONFIRMED, FAILED, UNCERTAIN and CANCELLED classifications. Token
  availability is local admission behavior, not evidence about SQL commit.
- Main-thread registry ownership and PGconn-exclusive lane workers remain.
  Bounded allocation failure must not require allocating a free-list node to
  publish a terminal or reclaim an abandoned object.

## Required evidence before implementation approval

First write a public compatibility decision and proof-oriented state machine for
allocate/reserve/admit/execute/publish/dispatch/close/retire/reuse. Quantify maximum
live slots, retained results, generation budget, per-object memory, allocation
costs and failure boundaries under measured intended workloads. Give recovery
and migration procedures for existing scripts and persisted application data.

Then propose deterministic tests using a deliberately tiny slot/generation
domain: stale token after reuse, generation exhaustion, crossed owner/Engine
epochs, out-of-order/late completion, callback-triggered unload and recursive
lookup/close, pending stream ownership, failed BEGIN and rejected finish,
shutdown before publication and double-retirement attempts. Check full terminal
multisets and transaction outcome invariants, not only returned numeric values.
Use allocation-failure injection at every state transition and concurrency
validation appropriate to worker publication. Follow with affected runtime,
sanitizer, network/transaction and packaging qualification on immutable builds.

Release/API review must choose a documented finite public guarantee and its
limitations before integration. Neither this proposal nor exact internal
identity authorizes claiming unlimited uptime or reclassifying historical #31.
