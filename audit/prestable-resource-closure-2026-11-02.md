# Pre-soak storage, pressure and cleanup disposition

Scope: finite practical pre-stable checklist on RC.3 source
`b715a971e71047a9c73f6b85489fc342317ba86b`, followed by the cleanup correction in
this branch. This is not a new comprehensive audit or a stable-readiness claim.
The 24-hour campaign was not started.

## Reused executed evidence

[CI 41372458054](https://github.com/itskoleban/omp-postgresql/actions/runs/41372458054)
completed all seven jobs successfully on that exact RC.3 source. The
[Linux normal job 125045688794](https://github.com/itskoleban/omp-postgresql/actions/runs/41372458054/job/125045688794)
was inspected directly: its CTest summary is **36/36 passed, zero failed**. In
particular, the named cases below passed, rather than being inferred from a green
workflow icon. None of these successful suites was repeated for this checklist.

| Gap | Disposition | Evidence and precise limit |
| --- | --- | --- |
| Log ENOSPC | PASS | `diagnostics_enospc`: actual ENOSPC on an isolated tmpfs of at most 16 MiB; one failed sink, accounted drops and explicit recovery. The same Engine preserves SQL error/message/SQLSTATE/index/results and confirms subsequent SQL while its own sink is failed. |
| Log EACCES | PASS | `diagnostics_permission`: verifies an actual `open` failure with `errno=EACCES`, preserves the existing file, accounts drops, disables the sink once and explicitly recovers append. |
| Positive short write | PASS | `diagnostics_short_write`: per-process `RLIMIT_FSIZE=1`, real one-byte write of a longer record, disabled sink/drop accounting, restored limit and explicit recovery. |
| SQL file permission/read failures | PASS | `sql_file_permission`, `sql_file_permission_integration`, `sql_file_stream_faults`: file/directory EACCES in both whole-file and section readers; six interrupted-read offsets, stream-state/EOF boundaries, no returned partial SQL and recovery. |
| SQL file descriptor exhaustion | PASS | `sql_file_descriptor_exhaustion`: process-local `RLIMIT_NOFILE`, rejected readers and restored-limit recovery. |
| Transport descriptor exhaustion | PASS | `resource_exhaustion_integration`: actual process-local EMFILE; new transport fails without SQL dispatch, an already open TLS transport succeeds, workers stop at exhaustion, explicit new connection/query recovers after restoring descriptors. |
| Bounded application pressure | PASS | `stress_integration` verifies retained-job saturation, reserved transaction-finish capacity, blocked streams, terminal delivery and release. `confirmed_result_allocation_integration`, `worker_allocation_integration`, `poll_allocation_integration`, `terminal_allocation_unit`, `owner_unload_allocation_unit`, `owner_unload_allocation_integration` and `callback_allocation_integration` passed their distinct structured allocation-failure contracts. This models selected C++ allocation failures; it does not claim system-wide OOM survival. |
| Evidence-storage failure during cleanup | PASS | RC.3 `test_network_cleanup.py` already covers actual destination-directory collision and injected wire-copy/diagnostic-finish failures, with successful and failed workloads. It attempts later fixture cleanup and retains the original failure. |
| Other finalizer failures | FAIL on baseline; PASS after correction | The new regression demonstrates skipped later finalizers, masked primary exceptions and an ignored live monitor after timed join. The correction and execution below close this specific defect. |

The existing ENOSPC same-Engine case and other file-fault cases cover their stated
contracts. Running identical SQL beside each standalone file fault would not establish
a new transport outcome. A separate logger-EMFILE case was not added merely to repeat
the existing open-failure/disable path with another errno.

## Bounded new cleanup regression

`python -m unittest discover -s tests -p test_network_cleanup.py -v` executes the
runner's actual `finally` AST with only external resources replaced by recorders.
It uses disposable temporary directories and starts no database, proxy or service.

The twelve new fault/workload combinations cover monitor-join exception, a still-live
monitor after the existing 15-second join, COMMIT proxy close, ROLLBACK proxy close,
PostgreSQL stop and final evidence retention. Each is tested after a successful
workload and while an original workload exception is active. They require the complete
ordered sequence through both proxies, wire/stall fixtures, cluster-stop attempt and
evidence-retention attempt. The failed stop remains a failure; no successful shutdown
is fabricated and there is no retry-until-success.

Negative control on uncorrected `b715a97`: the new test run produced nine errors and
three assertion failures, while the four existing unittest methods passed. The
retention-with-primary-error case already preserved the workload exception; its
assertion failure was the older diagnostic wording, not an additional lost-cleanup
defect. The other failures exposed early exits or ignored monitor liveness.
After correction, all six methods passed locally on Windows/Python
3.13. This is fixture exception-control evidence, not a real OS failure of `pg_ctl` or
a new component/runtime qualification. `git diff --check` passed. Black was unavailable
in the local Python environment and is not claimed as run successfully.

The correction catches and records finalizer failures, attempts later cleanup and
retention, preserves an active workload exception, and fails a previously successful
workload if any finalizer failed. A monitor that remains alive after its bounded join
also prevents qualification. Existing wire/stall/diagnostic failure checks remain.

## Explicit exclusions and retained risks

| Remaining limit | Disposition | Reason |
| --- | --- | --- |
| Global physical-memory exhaustion, OS OOM kill and arbitrary dependency malloc failures | OUT OF SCOPE | Destructive global pressure adds no bounded component guarantee. Existing per-process/structured fault contracts above are the practical supported qualification boundary. libpq can allocate a row before component size checks. |
| System-wide descriptor/Windows kernel handle exhaustion | OUT OF SCOPE | The process-local Linux EMFILE path is qualified. Exhausting shared OS tables or undocumented Windows limits risks unrelated processes and is not substituted by the Windows finite handle-count measurements. |
| Complete EMFILE plus ASan/UBSan | OUT OF SCOPE | Independent controls identify a sanitizer-runtime pipe-descriptor limitation; CMake explicitly excludes this combination. Normal/TSan descriptor checks and ASan allocation checks remain enabled. See [preserved diagnosis](final-allocation.md#independent-descriptorsanitizer-limitation-separate-from-r1). No sanitizer suppression is introduced. |
| Physical devices, Windows kernel ENOSPC, network filesystems, privileged reparse races | OUT OF SCOPE | Disposable Linux real-kernel failures and existing Windows file checks qualify their explicit scopes. No destructive host-volume or privilege-race experiment is required by this finite checklist. |
| Uninterruptible filesystem/provider calls | OUT OF SCOPE | No hard bound on an OS-blocked writer join, DNS/authentication provider or dependency call is claimed. Bounded tests cannot prove such a bound. |
| Historical Windows stalls, logging anomaly and issue #31 | OUT OF SCOPE for this correction | Their original failed outcomes and unknown causes remain preserved; candidate-specific owner risk disposition is still a launch/release prerequisite. A passing cleanup regression does not resolve the historical ASan SQL cleanup timeout. |

The practical storage/resource checklist is closed within these explicit scopes.
Final integrated source, binaries, dependency identities and affected checks must still
be frozen and qualified before an owner-authorized 24-hour campaign. The prolonged
campaign remains the final testing campaign; no merge, tag or publication is authorized.
