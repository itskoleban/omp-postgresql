# Completed qualification and independent Astra review

The owner integrated PR #37 as master
`4dd188bfb1ea1c6e56f3f4a275156bd2e489953d`. No qualification PR was open at
the start of this execution. Work continued from that verified source on
`qualify/v0.1.0-independent-audit`. Its complete tree
`b754267919c2fb6a4f0af188f671cfa772434e10` equals qualified PR source
`0a65609d115b8fb8eed82022dbc030d5f594b79e`; their commit identities remain distinct.

## Completed technical work

The pending TSan outcome from the previous checkpoint is now verified, rather
than inferred from a green status. Run
[40331053180](https://github.com/itskoleban/omp-postgresql/actions/runs/40331053180),
attempt 1, completed seven successful jobs. Directly downloaded artifacts were
verified by API-reported size and SHA-256. Six original JUnit reports contain
200 CTest executions, zero failures/errors/disabled/skips: Linux OFF 35,
ASan/UBSan ON 33, TSan THREAD 39, Windows x64 28, Linux x86 35 and Windows x86 30.
These are executions across configurations, not 200 unique test definitions.
Each configuration includes the seven confirmed-result allocation scenarios.

Four separate full real-session suites passed 23 scenarios each: source-built
and installed-package components on Linux and Windows. Package archive sidecars,
complete payload inventories, manifests, component/runtime identities and the
original sanitized session ledgers were checked against downloaded bytes.
The coordinator additionally applied the repository CTest report verifier to
all 200 preserved cases and matched all four session inventories against the
source's 23 named scenarios. These checks inspect actual prior executions;
they are not new network or C++ test runs.

The separate exact-master run
[40362033043](https://github.com/itskoleban/omp-postgresql/actions/runs/40362033043),
attempt 1, subsequently completed successfully. Its newly downloaded artifacts
were independently verified: seven jobs, another 200 CTest executions with the
same six-configuration inventory and no failures/errors/disabled/skips, four
23-case real-session suites and both installed-package identities. The candidate
packet now selects these master-built packages. PR-source observations and
hashes remain separate; neither tree equality nor a later pass rewrites them.

The independent Astra reviewer evaluated exact integrated commit `4dd188b` and
the identical qualified tree. [The full report](astra-review.md) records its
scope, source reasoning and directly executed package regressions (5/5 PASS),
and attributes hosted evidence to its separate inspector. It found no new
confirmed blocking implementation defect. No runtime patch was justified or
invented in this execution. The candidate may proceed to infrastructure
preparation within the recorded limits; prolonged stability is not certified.

## Preserved evidence and candidate

- [Original PR qualification packet](pr37-qualification/README.md): 94
  original selected members, six JUnit reports, public TSan build/control evidence,
  four sanitized result inventories and ledgers, two manifests and sidecars,
  final metadata, source/artifact/member hashes and a standalone verifier.
- [Exact-master qualification packet](master-qualification/README.md): separately
  downloaded final evidence for the integrated source and selected packages,
  with its own raw index and offline verifier.
- [Candidate dependencies](candidate-dependencies.json): exact selected package
  archives, build/dependency manifests and installed component/runtime identities.
  Selected packages identify master `4dd188b`; the earlier `0a65609` package
  identities remain in the original PR packet.
- [Preparation state](stability-preparation.json): integrated correction and audit,
  resource defaults, result/uncertainty invariants and remaining campaign gates.

Run `python pr37-qualification/verify_checkpoint.py` and
`python master-qualification/verify_checkpoint.py` from this directory to verify
both packets offline. Each intentionally omits complete distribution binaries
and most session logs. Repeating complete payload/ledger verification requires
the original GitHub artifacts identified in its index. Hashes are not signed
attestations or hermetic-build proof. The scoped Git attributes preserve captured
packet bytes across checkouts.

The first offline-packet verifier attempt failed on a `windows` versus `win32`
name comparison. The explicit mapping was corrected; its failed observation is
preserved in the packet. Original captured test/evidence bytes were not changed.
Earlier cancelled workflow attempts retain their actual statuses.

## Phase continuity and remaining work

| Phase                      | Current finite evidence                                                                                                                 | Remaining boundary                                                                                                                 |
| -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| 1: memory/resources        | Integrated owner retirement, transaction-marker retirement, identifier/token boundaries and seven result-retention allocation scenarios | Named sites and accounting limits do not prove arbitrary OOM or hard RSS bounds                                                    |
| 2: files/storage           | Integrated bounded SQL reader and read/permission/descriptor faults; logger short-write, permission and actual ENOSPC cases             | Filesystem/OS interruptions retain documented limits; historical logging observation remains unresolved                            |
| 3: connections/concurrency | TLS, timeouts, cancellation, transactions, network recovery and instrumented project-code TSan                                          | Issue #31 remains open; dependency internals and all schedules are not qualified                                                   |
| 4: open.mp                 | Linux/Windows x86 source and installed full sessions, player-slot reuse, tokens, late SQL/provider callbacks and AMX lifecycle          | The future deployed installation needs its own immutable freeze and applicable preflight                                           |
| 5: automated tests/fuzzing | Parameter/format and batch semantic regressions, resource lifecycle, fault controls and hosted sanitizer configurations                 | Finite seeds/configurations are not exhaustive fuzzing                                                                             |
| 6: platform qualification  | Final PR-source and exact-master matrices independently verified; candidate selected from master-built packages                         | Evidence-checkpoint PR checks have their own source identity; target-host qualification remains pending                            |
| First Astra review         | Completed for integrated `4dd188b`; no new confirmed blocker                                                                            | Preparation only; second review follows an actually authorized campaign                                                            |
| Prolonged stability        | Candidate/dependency packet prepared                                                                                                    | Isolated runner absent; deployment freeze, target preflight and acceptance thresholds pending; 24 hours not authorized/not started |

The historical ASan cleanup run `40164306733` remains FAIL / ROOT CAUSE UNKNOWN
under issue #31. The older Linux logging observation and prior local
MinGW/PostgreSQL full-suite failures also remain unresolved. Later green runs
and this review do not explain or correct them. Preserve the earlier negative
archives and the original rejected-COMMIT regression.

Next concrete action is to provision/identify a disposable isolated non-root
runner, freeze the actual campaign binaries/dependencies/fixtures/TLS/seeds,
execute a bounded target preflight, and record measured resource/queue/result,
latency, database, shutdown, capture and stop criteria. The owner has confirmed
that no isolated runner exists yet. No runner was provisioned here. Express
owner authorization is required before any 24-hour execution; its complete
results then need a second independent Astra review. Stable publication still
requires final owner approval and a justified disposition of issue #31.

No merge, branch deletion, tag, release or long-run workflow dispatch was
performed. This checkpoint does not change the published RC or its provenance.
