# Network recovery investigation and fixture cleanup correction

Baseline: `27e09b7cffa34968b78cb0025c121fce0a713e65`, inspected on November 1,
2026 UTC. Issue [#31](https://github.com/itskoleban/omp-postgresql/issues/31)
remains open and **ROOT CAUSE UNKNOWN**. The correction below addresses a
separately reproduced fixture cleanup failure, not the historical SQL timeout.

## Historical negative evidence

The original [run 40164306733](https://github.com/itskoleban/omp-postgresql/actions/runs/40164306733)
at `7ca0d1df1e970e622fad09240bd099b939a563c8` remains **FAIL**. Its ASan log
artifact `12586984982` was downloaded independently; SHA-256 matched GitHub:
`eaeaacdfb138b2fe0d2398cd7d4add92c165f56353f1c279f3523777f8993462`.

`LastTest.log` confirms the intended withheld INSERT response produced
`uncertain/error6/completed0`. The replacement backend, explicit SELECT, original
backend disappearance, row count one, sequence value one, and fresh BEGIN/ROLLBACK
all passed. The unrelated observer's `DROP TABLE phase2_hold` then produced the
same uncertain timeout and failed the SQL-confirmation oracle. The original
backend was 5254, the replacement 5257, and the direct TLS observer 5253.
`wire-hold.log` confirms the withheld response and closure of the original relay;
the historical `server.log` confirms those connection identities and disconnections.

That server log has no statement-duration/activity/lock-wait observations for
the DROP, and there is no contemporaneous client deadline or worker scheduling
capture. Thus it cannot distinguish server execution, socket receive delay,
scheduling, instrumentation, or other causes. Original backend disappearance and
successful fresh transaction do not establish why observer cleanup timed out.
Both lanes have independent 1500 ms command deadlines; the DROP does not inherit
the elapsed INSERT deadline. This code fact does not establish historical cause.

## Existing integrated diagnostic capture

The passed [integrated run 41312759129](https://github.com/itskoleban/omp-postgresql/actions/runs/41312759129)
was inspected without repeating the qualification. Its ASan artifact
`13086167926` SHA-256 is
`56905ed3e3e039e634a4c65bf4eae4fe1c2726a3395b190a899644712d5ac606`.
The PR #45 collection passed once with no collection errors or client overflow:

| Observation | Result |
| --- | --- |
| Client records | 13,208 |
| Hold cleanup backend | 5338 |
| Cleanup deadline | Original 1500 ms |
| Cleanup outcome | CONFIRMED, error 0, completed 1 |
| API entry to worker start | 0.089681 ms |
| API entry to dispatch return | 0.160647 ms |
| Command start to terminal status | 1.021314 ms |
| API entry to terminal status | 1.130584 ms |
| API entry to poll delivery | 2.135585 ms |
| Activity samples / largest actual gap | 187 / 53.683458 ms |
| Resource samples / largest actual gap | 469 / 21.233698 ms |

All 187 activity rows pass `validate_activity`. Three activity snapshots within
100 ms of this cleanup interval reported no blocking PID. Their sampling interval
cannot exclude a shorter wait. The command trace includes a complete result,
null drain, SQL confirmation and terminal publication, with no wait expiration or
receive interruption. The whole instrumented network test passed all thirteen
scenarios. These observations characterize this execution only; they do not
explain the historical failure or establish timing equivalence without observation.

Relevant retained capture hashes:

- `client.jsonl`: `135f3d8be22b96e0f317163e5a1043cdcb69d659b4c40434e5d4b94bd570374f`
- `activity.jsonl`: `e69a677ff80f73113e1e20441b1d017073d6a961c64f04bd7a20d0d912e30631`
- `resources.jsonl`: `fb19fb77740cb1dd256ebf2da33c61e79c1d532e17205f0a1197cf3efb785199`
- `server.log`: `ec7cabb2f27d90c327d0a360616acf3509ed073437987f9133a8591990bfd880`
- `provenance.json`: `491d3f590281d31b60e384661cb62e9f900f7024d31972a295c6438539d0a167`

## Reproduced evidence-storage cleanup defect

Hypothesis: the runner can abandon remaining fixture cleanup when retaining a
wire proxy journal fails. This follows a concrete path in `run_postgres.py`:
wire shutdown is guarded, but the subsequent destination directory creation and
`shutil.copyfile` inside `finally` were unguarded. Either exception skipped later
wire peers, stalled peers and the disposable PostgreSQL shutdown. It also replaced
an existing workload failure with the storage exception.

Two added regression methods execute the runner's actual cleanup AST, replacing
external resources with recorders. Each covers both a real filesystem path
collision at the build destination and an injected `copyfile` OSError, after
both successful and failed workloads. No PostgreSQL process is needed to
deterministically exercise this supervisor behavior.

Before the correction, all four new subcases failed with FileExistsError/OSError
at evidence retention. After the correction, the runner records the retention
error, closes both wire peers and the stalled peer, requests disposable PostgreSQL
shutdown, and attempts remaining evidence retention. An existing workload error
remains the primary failure; an otherwise successful run fails qualification for
the lost evidence after cleanup. Missing evidence is never accepted as a pass.

Local Windows/Python 3.13 validation:

```text
python -m unittest discover -s tests -p test_network_cleanup.py -v
  Before: 4 methods, 4 errors in the new subcases; original 2 methods passed.
  After: 4 methods PASS, including all 4 new subcases.
python -m unittest discover -s tests -p test_network_diagnostic.py -v
  7 methods PASS.
git diff --check
  PASS.
```

To reproduce the negative control, retain the new tests, restore only
`tests/run_postgres.py` from the baseline SHA in a disposable checkout, and run
the cleanup command above. Restore the correction before qualification.

This changes only the test supervisor. It preserves the public Pawn API,
component scheduling, SQL deadlines, sanitizer settings, SQL confirmation,
UNCERTAIN outcomes and prohibition on replaying uncertain writes. It is not a
demonstrated cause of the historical SQL timeout.

## Pre-release risk disposition

No reproducible component network defect was established from these observations.
Do not increase the control SQL deadline or repeat the broad network campaign to
turn the historical failure green. The existing diagnostic capture is adequate
to continue RC preparation with the historical availability uncertainty disclosed.
It does not constitute stable-release risk acceptance. Issue #31 stays open;
any recurrence with captured worker/socket/server evidence is an actionable
release blocker requiring a focused reproduction and correction. The candidate
must receive qualification at its own exact source before approval for a tag.

The 24-hour campaign still requires a frozen candidate, verified isolated Linux
runner, disposable non-root PostgreSQL environment, explicit stop criteria and
preserved provenance. No long campaign was initiated by this investigation.
