# PR #30 bounded hosted network diagnostic

Status before execution: ROOT CAUSE UNKNOWN; merge qualification BLOCKED.
Historical workflow 40164306733 remains FAIL at 7ca0d1df1e970e622fad09240bd099b939a563c8.
Earlier local controls and passing observations retain their original classifications.

## One execution budget

This commit adds optional observation for exactly one ASan/UBSan network case in
the complete seven-job pull-request qualification. It runs all thirteen original
network scenarios once on a disposable GitHub runner, with the original 1500 ms
query timeout, 90-second outer limit, SQL assertions, uncertainty and no-replay
semantics. No previous local experiment is repeated. No correction is claimed.
The option is enabled only for the PR #30 ASan pull-request job. Duplicate push
workflows do not enable it. Normal, TSan, Windows and x86 qualification remain enabled.

The client recorder uses bounded fixed storage with no per-event allocation or
file write on query paths. It is activated only by the network executable and is
compiled out of normal builds. It records admission/worker start, actual deadline,
dispatch, flush, consume, busy/readiness, result and null drain, confirmation,
terminal status, queue-publication brackets, poll delivery and connection lifecycle.
SQL identity is a hash; parameters, service strings and credentials are not recorded.
Terminal insertion is bracketed, not assigned an invented exact instruction timestamp.
Client UTC timestamps are correlation anchors; steady-clock values govern deadlines.
Overflow, missing footer or required phases invalidate diagnostic collection.

Only this network process receives PostgreSQL logging options. Statement duration,
statement identity and lock waits are enabled without changing deadlock or statement
timeouts. Parameter logging is disabled. Fixture bootstrap credentials precede this
process and are not logged by these options. Independent activity/lock samples use
a 100 ms observer statement timeout and 50 ms requested interval. Thread scheduling,
CPU/memory/I/O pressure and cgroup observations request 20 ms intervals. Actual gaps
must be reported. Observers have finite process-start and runtime bounds and stop
when the network process exits. They never replay SQL or change workload results.

The artifact retains the actual ASan executable, source/runtime hashes, compiler and
linker commands, exact source commit, raw client/activity/resource/server captures,
collection status and file hashes. Missing/crashed/incomplete captures remain failures.
Recording adds overhead; a passing instrumented run cannot describe an unobserved
historical schedule or establish performance equivalence.

## Shared cleanup-deadline hypothesis

`Test::limits()` configures query_ms=1500 for the Engine containing both observer
and writer. Thus unrelated observer cleanup also has a 1500 ms command budget.
Each `Lane::command` creates a new deadline; cleanup does not inherit the elapsed
INSERT deadline. Queue time is separately accounted for. These are confirmed
configuration/code facts, not proof of a fixture defect or historical cause.

To demonstrate an inappropriate cleanup budget, the capture must identify where
the actual cleanup interval was spent and show that the oracle conflates an
unrelated fixture condition with the intended fault contract. A normal cleanup
passing under 1500 ms cannot establish that. Neither an increased budget nor a
passing rerun is an acceptable correction. The original negative control must remain.

## Decision rule

If an equivalent failure identifies a reproducible component or fixture defect,
make only a minimal correction with a deterministic before/after regression and
qualify the final new head with all seven jobs. If the failure does not recur,
retain ROOT CAUSE UNKNOWN and assess residual risk from the missing original
server/client/scheduler observations. Do not begin indefinite repetitions.
No merge, master change, release or 24-hour campaign is authorized.
