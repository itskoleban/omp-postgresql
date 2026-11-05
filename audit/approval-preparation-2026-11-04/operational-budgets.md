# Operational budget and deployment disposition

November 4, 2026, America/Bogota. **BLOCKED.** This is a focused closure of the
remaining operational requirements. No Docker container, SQL, calibration, long
campaign, energy change or publication was started for this review. The final
24-hour campaign is **NOT RUN** and has no start authorization.

The retained candidate is `3a5df8439881bd514727a2c5e360f9ec0bafaf43`, tree
`21cea7c3b4620c361484be7aa76ad8f73132c545`. A proposed Engine correction must be
reviewed separately: the old binaries and their results cannot qualify a changed
Engine. Existing observations remain historical measurements and sizing inputs.

## Evidence retained without repeating successful tests

The previous operational evidence root, abbreviated **E**, is:

`C:\Users\esteb\.codex\visualizations\2026\11\03\01a1a183-d181-79b1-94d3-e95627612b75\omp-v010-preparation\continuation-20261103`

| Retained evidence under E | Established result and scope |
| --- | --- |
| `target-preflight-attempt5/qualification-summary.json`, recovered manifest and monitor | 34/34 CTests, zero failures/skips; extended fuzz exit 0; preflight-only, `campaign_started=false`. A manifest field `seconds=86400` describes configured future load, not executed load. |
| `calibration-complete1/verification-summary.json`, recovered calibration manifest | PASS: one unchanged Engine for 120.497938 measured load seconds, 202 waves, 93,735 counted load SQL operations; ten lifecycle rounds, cleanup and retention. Wrapper elapsed 141.979709 s is a different measurement. |
| `calibration-interrupt1/drill-timing.json`, monitor and recovered archive | Deliberate ABORTED drill, not load PASS: request to guard completion 4.146679 s; guard signal to completion 1.546856 s; PostgreSQL cleanup/retention passed. |
| `focused-closure/launcher/unit-tests.review.txt` | 28/28 offline PASS. No real launch proved by these fixtures. |
| `focused-closure/supervisor-v2/unit-tests-final.txt` | 65/65 offline PASS, final helper bytes. The earlier 64-test result remains historical. |
| `focused-closure/budgets/prior120-additional-evidence.json` and `budget-closure.proposal.json` | Analysis of existing captures; no new SQL or runtime test. |
| `calibration600-20261104T0621Z/{gate,result}.json` | Last earlier attempt BLOCKED: available RAM 794,714,112 bytes; calibration and v2 deployment NOT RUN. AC and Docker were passing then, not proven current. |

`operational-evidence-index.json` records current file hashes, validates the
retained launcher/supervisor/latest-gate ledgers and verifies selected archive
checksums. This integrity check is not a rerun of the successful tests. Keep the
original evidence root: this compact index does not replace its raw logs or files.

## Fresh physical gate and bounded calibration

[The fresh gate](physical-capacity-gate.json) was captured November 4 at 22:34:10
America/Bogota (`2026-11-05T03:34:10Z`):

- RAM available: **4,144,308,224 bytes**, exceeding the strict 2,894,368,768-byte
  start threshold by 1,249,939,456 bytes: memory gate PASS.
- **AC disconnected**, battery 48%: physical gate **BLOCKED**.
- Windows C: available: 206,643,580,928 bytes. This snapshot alone does not
  establish a complete storage budget or available Linux filesystem capacity.
- Docker/Linux/storage/supervision checks were not continued after the failed
  physical gate. No workload was started and no unchanged-condition retry made.

The laptop is **not currently an eligible campaign host**. This does not prove
permanent incapacity. A future AC-connected window must also satisfy every other
gate freshly; memory once passing does not reserve it for later.

The predeclared 600-second observation policy is
`E/focused-closure/budgets/resource600-policy.pre-run.json`, SHA-256
`e247786351e02d66f83b5e12c0fde1d72c5412b22d8215f73970fa42c1538cc2`.
It permits only one 600-second continuous Engine observation plus existing ten
lifecycle rounds, with a 900-second external guard. It specifies 2 GiB memory,
2 CPU, pids 256, memory-swap equal to memory (no additional swap), empirical host
reserve 746,885,120 bytes and the strict start threshold above. These are bounded
observation/containment settings, not accepted final campaign budgets.

The policy retains all 0–600 s diagnostic data in fixed 60-second windows. Its
additional 120–600 s analysis requires eight complete windows and at least 40
occupied one-second bins per window. The 120-second warm-up candidate does not
authorize discarding unfavorable measurements or establish final campaign warm-up.

**600-second calibration: NOT RUN, blocked by AC.** If the Engine correction is
approved, first bind a new freeze and affected checks to that source. Do not run
the old binary merely to create an apparently current calibration result.

## Budget disposition before any prolonged campaign

The following acceptance invariants are fixed now and must not be relaxed after a
run: **zero semantic defects, lost or duplicate terminals, unexpected uncertainty,
transaction violations, sanitizer findings, failed fixture cleanup or incomplete
evidence**. Quiescent pending/errors must be zero. Expected deliberate fault
outcomes remain distinguished from unexpected uncertainty. Any unexplained
recurrence of historical issue #31 suspends qualification and requires disposition.

| Quantity | Retained evidence / prospective decision | Final campaign status |
| --- | --- | --- |
| Warm-up, RSS window/growth/slope | All initial measurements retained. Late median growth was +1,564 KiB; 120 s supplies only two short late windows. No allowed drift or slope has been justified. | **PENDING** 600 s observation and prospective engineering margins. Do not derive a passing drift allowance from the campaign. |
| Process RSS / HWM | Lifecycle HWM 906,696 KiB. Historical cgroup peak 1,419,481,088 bytes. Approximately aligned sequential cgroup-minus-process snapshots reached 517,165,056 bytes; this is not guaranteed overhead. | **PENDING** process ceiling, growth tolerance and total-memory envelope. |
| Cgroup / host margins | Proposed 2 GiB cap leaves 728,002,560 bytes above the historical total peak. Host reserve 746,885,120 bytes derives from prior baseline variation. | Containment/start gates fixed for the bounded observation; **PENDING** demonstrated sufficiency for final campaign. No global Docker memory change. |
| FD / threads in load | Every retained quiescent wave: 7 FD / 5 threads; transients 8 / 6; complete teardown returned 3 / 1. | **PENDING** absolute ceilings. Do not use 7 / 5 as global maxima. |
| FD / threads at matched quiescence | Adopt prospective required growth **0** for equivalent phase/Engine/lane configurations; compare to measured matched baseline and complete teardown, excluding explicitly scheduled side-lane phases. | Criterion fixed; must be mapped to the final supervisor and verified on the final deployment. An unaccounted persistent increase fails. |
| CPU | Observed maximum interval Engine 48.990%, cgroup 168.922%; scale is one CPU = 100%, so 2 CPU = 200%. Includes setup/PostgreSQL/lifecycle in cgroup. | Quota 2 CPU is containment. **PENDING** sustained window/ceiling and peak tolerance. A progress-gap guard must not be described as proof of busy-spin. |
| Wave latency / duration | Observed maxima p50 1,092,482 us, p95 2,478,118 us, p99 2,659,842 us. These are per-wave delivery percentiles, not global/Pawn API percentiles. | **PENDING** target tolerance and maximum wave duration; preserve every wave. |
| Throughput / progress | 93,735 / 120.497938 = approximately 778 counted SQL operations/s on the retained profile. Internal maximum progress gap 2.677147 s; external maximum sample gap 4.312810 s. | **PENDING** minimum throughput, fixed progress windows, cadence/gap tolerances and stall cutoff. Counted SQL is not total wire traffic or ID consumption. |
| Reconnection / retries | Required primary reconnections during normal load **0**; scheduled cancellation/recovery on the separate side connection is a distinct expected event. No automatic replay of uncertain writes. | Criterion fixed; final event accounting and monitoring mapping **PENDING**. Classify every attempt; no unexplained retries accepted. |
| Cancellation / close assertions | Existing `until()` and lifecycle teardown checks configure cooperative **20 s** deadlines; seven retained mixed brackets conservatively included cancellation/recovery within at most 0.174911 s. | Preserve existing 20 s test assertions. They are not an independently measured API SLO or an OS-independent hard deadline. |
| External shutdown / cleanup / retention | Historical deliberate stop timings above; previous guard grace 45 s is configured, not a measured worst-case bound. Blocking OS calls retain documented limitations. | **PENDING** agreed final grace and enforced mapping/abort drill. Cleanup, no descendants, PID removal and retention are mandatory. |
| Storage | Identified dynamic streams project 1,140,946,472 bytes over 24 h before fixture/WAL/static files/archives/recovery copies; details below. | **PENDING** full sizes, per-filesystem reserve, growth ceiling and complete destination plan. |

The fixed zero-growth/reconnection criteria are deliberately narrower than a
promise of no leak in every schedule. They do not fill the still-null fields of
the old campaign contract or authorize its execution. After bounded measurement,
write tolerances, units, normalizations, windows, phase rules and stop actions to a
new immutable contract **before** any prolonged run. Any still-unjustified field
continues to block READY FOR APPROVAL.

## Supervisor v2 deployment

**PREPARED / OFFLINE VERIFIED; real deployment NOT RUN.** The v2 package is under
`E/focused-closure/supervisor-v2`. Its fixes include Linux `/evidence` statvfs,
chunked final-copy sanitizer scanning, late lifecycle HWM and streaming final
log/XML review. Existing 65 tests support those code paths only.

The old successful live preflight/calibration used earlier frozen helper snapshots.
They establish antecedents for identity/heartbeat/stop/cleanup, not deployment of
the current v2 bytes. In particular, the resource600 launcher currently copies
`resource600/supervisor`; selecting it alone does not prove v2 deployment.

Once physical gates pass, prepare one bounded, uniquely named v2 deployment with
read-only source/build/helpers and their hashes, frozen image, scoped labels,
non-root execution, private disposable loopback PostgreSQL and no production data.
Verify full container ID/label, process command/executable/start identity, host
boot, heartbeats, monotonic progress, CPU/RSS/FD/threads, cgroup and independent
Linux/Windows space readings. Check late copied logs larger than 4 MiB, signal
forwarding/deadman/owned-container abort paths, bounded cleanup and retained final
manifests. Never act on a bare reusable PID or a container-name glob.

The authorized 600 s observation can exercise normal v2 capture; affected abort
paths still need a genuinely bounded real check when possible. No 24-hour load is
needed or allowed for deployment verification. Until observed, each live capability
remains NOT RUN. Offline fixture PASS must not be entered as a live result.

Final Linux-to-Windows recovery is manual in this package. A new automated drain
is not a release prerequisite: the documented manual procedure can satisfy it.
Operational `COMPLETE` and `PASS_CAPTURE_REVIEW_ONLY` are not campaign PASS.

## Storage and recovery plan

The raw 120-second stress log was 91,451 bytes, projecting 65,572,627 bytes per
copy. The canonical pipeline can retain five representations (tee, LastTest,
integration copy, JUnit system-out and its copy): 327,863,135 bytes projected.
CSV projects 13,670,076 bytes and monitor JSONL 799,413,261 bytes. Sum:
**1,140,946,472 bytes**, an estimate rather than a hard bound.

Missing sizing includes Linux fixture/database growth, WAL, PostgreSQL logs,
maps/static inventory, preflight captures, final archive and simultaneous recovery
copies. `max_wal_size` is soft and cannot serve as a disk cap. C: backs both
Windows evidence and the Docker VHD: a large logical Linux free-space reading
does not create additional physical host capacity.

Before authorization, measure each component on the final deployment. For each
filesystem, reserve the next full archive, next full recovery copy, expected
remaining growth plus explicitly chosen margin, fixture/static footprint, and
the working space needed to stop and close evidence safely. Require independent
Linux statvfs and Windows free-space guards; also budget their combined physical
C: backing. Floors and maximum allowed growth remain **PENDING**, rather than
choosing an arbitrary GiB quota from partial projections.

Responsibilities and destinations proposed for final approval:

- Project owner `itskoleban`: accountable for window/host and permanent retention.
  A named on-call operator and independent final reviewer are **PENDING**.
- Live raw source: the uniquely owned Linux `/evidence` volume. Host monitor,
  control, launch record and config are additional evidence; the Windows mirror
  can be incomplete during execution.
- Recovery: a new run-specific sibling directory outside the frozen checkout,
  under the owner's selected evidence root; preserve full Linux volume and host
  monitor/control/config together. Exact final run path remains **PENDING**.
- Independent second copy: owner-selected external disk or independent host;
  destination, capacity and write verification **PENDING**. A second directory on
  C: provides a copy, not protection from loss of that physical disk. Existing
  `.codex/visualizations` artifacts must not be the only permanent copy.

On completion/abort: stop only owned writers, verify exact process/backend
cleanup and removed postmaster.pid, record container inspect/exit/OOM/restart
state, archive the complete Linux evidence, copy to the new host destination,
compare archive byte size and SHA-256 across both sides, list/extract and verify
every manifest member, review all raw XML/logs/resources including final HWM,
then hash the closure report and copied inventory. Preserve exceptions and failed
attempts. Archive manifests that say `ARCHIVE_PENDING` are followed by external
verified retention records; do not rewrite history to imply an in-archive PASS.
Do not remove a volume or disposable environment before both recovered copies
and the final integrity check pass. Restore power configuration even after failure.

## Energy and alternate host

No energy change is authorized by this request. Preserve the previously tested
design in `E/energy/Set-TemporaryCampaignEnergy.ps1`: inspect and save a **fresh**
backup of the active plan, change only AC standby timeout during a specifically
approved window, and restore/verify exactly that backup on finish or abort.
Reject plan/third-party changes instead of overwriting them. Keep AC, ventilation
and lid open. Do not change battery policy, lid action, updates, global Docker
memory, services or autostart. Do not assume a `finally` block survives power loss;
store backup/restoration instructions durably for manual recovery.

Retained screen-off continuity subchecks remain useful; their enclosing trial FAIL
remains FAIL. Screen on is not a campaign requirement. Detached supervision can
outlive the launching chat, but does not establish continuity through logout,
Windows/Docker exit, sleep or reboot. Any such interruption aborts; do not resume
and sum durations to manufacture continuous elapsed load.

If a continuous resource/AC window cannot be supplied, use an **already available
isolated physical Linux x86-64 host** with non-root C++20/CMake/Ninja/PostgreSQL/
OpenSSL tooling, no production credentials/data, private disposable loopback
fixtures, sufficient measured RAM/storage, verified final runtime hashes and an
independent supervisor/evidence destination. Recalibrate host-dependent budgets
and preflight affected deployment paths there. A VM on this laptop does not remove
its RAM/energy constraints. No host was contracted or provisioned.

## Remaining order of work

1. Review the minimal transactional-ID PR, obtain merge authorization, then record
   the new source/tree, rebuild and repeat only affected qualification/freeze work.
2. Obtain a physically eligible host/window; complete the single pending 600 s
   observation and bounded real v2 deployment checks, preserving failed attempts.
3. Fill the still-unjustified performance/memory/storage/monitoring budgets and
   bind all enforced values to a reviewed immutable contract. Complete ID horizon,
   candidate-specific residual risks, retention destinations and named operators.
4. Obtain separate permission for the exact temporary energy window if needed.
   Only when all other prerequisites are evidenced is the plan **READY FOR
   APPROVAL**. Explicit later authorization makes it **APPROVED TO START**.
5. The final campaign remains NOT RUN in this work. Only actual successful
   execution and comprehensive review could establish **CAMPAIGN PASS**; a further
   release decision and artifact match are needed for **READY TO RELEASE**.
