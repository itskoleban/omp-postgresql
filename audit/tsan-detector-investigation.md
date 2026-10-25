# TSan detector-control investigation

## Scope and status

The detector fixture has a **WORKAROUND VALIDATED** for a demonstrated LLVM 19
shadow-update limitation. The exact internal cause of the historical failed execution
remains **ROOT CAUSE UNKNOWN**: its process streams establish nondetection, but no
runtime instruction trace was retained. A passing repetition does not resolve that
historical uncertainty.

This report concerns the correction branch based on remote master
`d0494d2bf45c03757864af0f5dfa2c7b868f4810`. The seven-job CI verdict for the final PR
head is recorded in the new PR's check suite and description before a merge recommendation. No merge,
release, tag change or extended campaign is authorized by these results. The
**24-hour campaign remains BLOCKED**.

## Preserved merged-master failure

[Master workflow 40142964957](https://github.com/itskoleban/omp-postgresql/actions/runs/40142964957)
at `d0494d2bf45c03757864af0f5dfa2c7b868f4810` remains **FAIL**, with six successful
jobs and failed [linux (THREAD), job 121018361798](https://github.com/itskoleban/omp-postgresql/actions/runs/40142964957/job/121018361798).
That job passed 28/29 CTest cases, including `network_recovery_integration`.

The independent detector's race iteration **3/20** exited **0** with **zero stdout
bytes and zero stderr bytes**. The other nineteen race executions reported a genuine
TSan race and exit 66. All twenty startup-only executions passed. The existing oracle
correctly classified the missing diagnostic as FAIL. It must never be relabeled PASS.
This evidence does not establish a PostgreSQL component data race.

| Historical executable | SHA-256 |
| --- | --- |
| Race fixture | `1f4eb1807bdeed12d46b88e499414353d5bc85414d089574e585d96100e5a2ef` |
| Startup fixture | `00439497091f9cbbe60bbadfbb42c0b070046c5db4e3e0f300df52a15d23e66c` |

These same executable hashes occur in the successful PR #29 qualification at
[workflow 40140847210](https://github.com/itskoleban/omp-postgresql/actions/runs/40140847210),
source `f18ff579aea4e6f369a6abbf6d8021cba6071635`. The merge has the same source tree.
The observed transition therefore does not demonstrate changed fixture compilation.
The earlier success is evidence of an intermittently passing control, not evidence
that the later failure was harmless.

The failed artifact `linux-x64-test-logs-THREAD`, ID **12579208601**, was downloaded
and inspected, including complete LastTest/JUnit and integration command logs. The
downloaded original ZIP SHA-256 is
`c956351dcf518c2d0c17c4827f864500525b43d55fabe799f4f91bd218fac718`.
The original artifact contains logs, not the executables or a complete verbose linker
command. Historical binary identities are retained from the logged hashes; unavailable
binary bytes and omitted commands are not invented.

## Independent hypotheses

| Hypothesis | Evidence and conclusion |
| --- | --- |
| 1. A genuine race occurs | Both workers write the same non-atomic `value` after startup. The startup RMW/load operations do not order those later writes. GDB reached the actual instrumented access in both workers. |
| 2. Optimization changes accesses | Confirmed in Clang 19.1.1 IR and disassembly: the original arithmetic loop retains only one instrumented value store per worker. Source loop count does not imply 10,000 detector opportunities. |
| 3. Both workers reach the race point | Confirmed in the debugger reproductions. The historical empty-stream execution lacks per-worker observability, so its exact progress cannot be independently reconstructed. The corrected fixture publishes that both workers started before releasing them. |
| 4. Startup synchronization eliminates the race | Rejected by happens-before analysis. Each conflicting write follows the final startup release/RMW. Neither write can be carried by an earlier release. Joining workers later orders main's read, not one worker's write before the other's. |
| 5. Initialization/instrumentation is incomplete | The historical exit 0 is not a startup-error signature, and the same binary detected races nineteen times. This does not prove internal startup correctness for iteration 3. A separate local Docker startup denial was captured and remains a failed environment control. Actual callbacks and the runtime archive were inspected. |
| 6. Compiler/linker/library/runtime behavior explains the failure | Compiler store sinking and the linked runtime's concurrent shadow-snapshot mechanism are demonstrated. The same historical binary hashes exclude a changed binary as the observed differentiator. No component/library defect is inferred from that fact. |
| 7. The oracle misclassifies the failed child | Rejected for the historical outcome: exit 0 and empty stderr fail. Separate regressions strengthen report validation, timeout handling and forged-message rejection without accepting that outcome. |
| 8. Scheduling affects detection | Confirmed: constraining only the original workers' callback schedule reproduced exit 0 without a report. The original and volatile-only fixtures both admitted this schedule. |
| 9. The evaluated runtime has a relevant limitation | Confirmed in actual LLVM 19 execution and supported by upstream source. Simultaneous shadow snapshots can each omit the conflicting access. This demonstrates a sufficient mechanism; it does not recover the historical schedule. |

## Actual-runtime reproduction

The isolated toolchain was Ubuntu Clang **19.1.1**
(`1:19.1.1-1ubuntu1~24.04.2`), matching the relevant failed-CI compiler version.
Compilation used `-g -fsanitize=thread -g -O1 -std=c++20`; linking used
`-g -fsanitize=thread`. Every run used
`TSAN_OPTIONS=halt_on_error=1:exitcode=66`. Full commands, driver expansion, linked
libraries, package versions, environment metadata, IR, disassembly and per-attempt
streams are preserved in the investigation manifests.

The linked runtime archive SHA-256 was
`bf92e52f36539006b2d53ea6f0587d7bbddc9b7119f42bb47b6a4d322af8b63f`.
The rebuilt original fixture SHA-256 was
`b83df09d1cd835f8884fc865c448c7bed5d1a45cfc5114cd7c6a558cbfcf8596`.
This is a recorded local rebuilt binary, not a claim to possess the historical CI
executable bytes.

GDB 15.1 stopped both original worker callbacks at `__tsan_write4 + 0x4c`, immediately
after the vector shadow load at `+0x46`. The linked disassembly identifies these
instructions. Each callback had the same snapshot:
`c00202f0 800102f0 800101f0 c00201f0`, containing scheduling-atomic accesses and neither
worker's value access. Continuing the original program produced exit **0** and no
TSan diagnostic. No instruction, source, register or data patch was used; the debugger
only constrained scheduling and explicitly set `disable-randomization off`.

The [LLVM 19 access implementation](https://github.com/llvm/llvm-project/blob/llvmorg-19.1.1/compiler-rt/lib/tsan/rtl/tsan_rtl_access.cpp)
loads a shadow snapshot, checks it and later stores new access metadata. Those steps
are not one locked transaction. The [upstream algorithm description](https://github.com/google/sanitizers/wiki/ThreadSanitizerAlgorithm)
also documents a small probability of missed detection during concurrent shadow
updates. The experiment demonstrates that limitation in the evaluated runtime.

A volatile-only loop retained all 10,000 instrumented stores in IR and passed its
ordinary bounded series, but the adversarial debugger schedule still produced exit
0 without a race report. It is rejected as a demonstrated correction. A later passing
execution cannot compensate for either negative result.

## Minimal fixture correction

The maintained fixture uses two independently executing worker threads, a startup
gate, an aligned non-atomic volatile value, and a separate aligned atomic phase flag.
Worker 1 performs its value write before publishing phase with relaxed ordering.
Worker 2 waits using relaxed loads, then performs its conflicting value write. This
ensures the first detector callback has completed before the second starts.

Relaxed phase operations create no synchronizes-with edge. The two non-atomic writes
remain unordered by happens-before and genuinely race. Volatile preserves the value
accesses without making them atomic. The startup release/acquire gate precedes both
conflicting accesses; it establishes worker readiness without making the race safe.
Separate alignment prevents scheduling-atomic traffic from occupying the value's
shadow cell. No TSan interceptor, suppression or instrumentation setting is weakened.

The [LLVM 19 atomic implementation](https://github.com/llvm/llvm-project/blob/llvmorg-19.1.1/compiler-rt/lib/tsan/rtl/tsan_interface_atomic.cpp)
does not acquire/release clocks on its relaxed load/store paths. A separate variant
changes phase to release/acquire and must run normally without a diagnostic. That
negative control demonstrates the distinction between the racy scheduling protocol
and a correctly synchronized protocol.

The exact maintained control built in the local CMake configuration had SHA-256
`e4a509c94d0e41a99f47f6b3dd1081959d7c2b7c37cd256bba65496c6b6cadcd`.
Under GDB, stopping the first callback showed phase **0**, preventing the second value
access. After the first callback completed, the second snapshot showed phase **1**
and included the first worker's value access. Continuing emitted a full genuine race
report with actual process exit **66**. This verifies the maintained fixture against
the demonstrated failure mechanism.

## Bounded controls and environment limitations

| Variant | Default Docker seccomp, 20 attempts | Startup-permitted container, 20 attempts |
| --- | --- | --- |
| Original | 20 startup failures, zero reports | 20 genuine reports, exit 66 |
| Volatile loop | 16 startup failures, 4 genuine reports | 20 genuine reports, exit 66 |
| Relaxed aligned handoff | 18 startup failures, 2 genuine reports | 20 genuine reports, exit 66 |
| Release/acquire handoff | 17 startup failures, 3 normal exits | 20 normal exits, zero reports |

All 71 default-profile startup failures remain **FAIL**. Their diagnostics identify
the denied `personality` call at `tsan_platform_linux.cpp:290`. They are not missed
race detections or component defects. The second disposable container used the same
installed toolchain, a profile permitting only that additional syscall, all capabilities
dropped and `no-new-privileges`. No host ASLR/entropy setting was changed, and no
non-PIE workaround was used. LLVM may perform its own startup re-exec when needed.
This documented local startup adaptation is not proposed as a CI change.

The original and volatile adversarial nondetections are additional independent negative
results, not members of the ordinary twenty-attempt series. A first handoff debugger
script attempt failed in Python bookkeeping after a worker exited; its script/log are
retained and never counted as a successful control. The corrected debugger experiment
and exact maintained-binary verification completed separately.

The oracle continues to require every positive execution to contain an actual race
diagnostic and exit 66. Startup-only, correctly synchronized, absent/empty diagnostics,
incorrect exits, fake fixture markers, crashes, timeouts and initialization failures
must remain distinguishable. No percentage or majority threshold is acceptable.

## Evidence identities and publication

The [compact evidence index](tsan-detector-evidence/manifest.json) retains
original/sanitized identities separately. Complete CI artifacts remain authoritative;
large historical logs are not duplicated in Git. Important local
original transcript identities are:

| Transcript | SHA-256 |
| --- | --- |
| Original adversarial nondetection | `f9a4fc809f95bbca840d4f8c8da8a21e27283e5837e066898f268fb6e86ed321` |
| Volatile-only adversarial nondetection | `309a81b570f041ea9361c6835f1d1ad5bfe8b837ca27b6cf2a9d8a1725179606` |
| Experimental handoff diagnostic | `31ccd64c9d1f3963f9adb49989f91ca951751cea759932d670b2a48e899517cc` |
| Maintained handoff diagnostic | `4a0fd2c761cd2b3b7580187d21e6b9993e04aa561ee96a3513e8716593817634` |

The original default-profile manifest SHA-256 is
`73832a006ec19232afb2c00cb1c938d9904b77ed146a466c4ad748c6fb6fa30f`;
the startup-permitted experimental manifest SHA-256 is
`08b990d024272dc5e33d46b76d0f1a880256a64c919138dff6525810e11b5226`.
Both record every attempt independently, including stream hashes and lengths. Build
hashes depend on debug paths; an independently rebuilt binary is never assigned the
historical CI binary identity.

## Focused component audit and final qualification

The [focused concurrency audit](tsan-focused-concurrency.md) confirms a separate
allocation-recovery defect in `Engine::poll`: vector growth could throw after consuming
a completion and setting delivery state, permanently losing terminal notification.
A real public-API regression failed before correction (exit 1; one of two accepted
completions lost) and passed afterward (exit 0; all three reserve failures preserve
both completions, FIFO and the final connection event). Preflight capacity reservation
now occurs before any consumption or state mutation; allocation failure returns an
empty Dispatch for the next tick. TSan uses named reserve checkpoints and preserves
its global allocator interceptors. This component correction is **RESOLVED** within
the demonstrated fault model and is unrelated to the detector executable.

No additional component data race, unsafe lifetime, deadlock, lost wakeup or duplicate
terminal publication was confirmed. The audit records ownership reasoning and limits.
Reserving Dispatch storage increases bounded per-tick allocation work; broad OOM,
third-party allocator failures and unrestricted scheduling remain separate follow-up.

Local verification used the recorded modified source files based on d0494d2, rather
than claiming an earlier Git SHA contained uncommitted corrections. Clang 19 TSan's
configured core suite passed **27/27**, including every worker site control, the new
poll recovery regression, both detector controls and network recovery. The normal
core configuration passed **22/22**, and ASan/UBSan passed **21/21**. These local builds used PostgreSQL 17.11,
`PG_BUILD_COMPONENT=OFF`, stress rounds 5 and sustained stress seconds 0; they omit
the CI-only ENOSPC mount and component/server packaging. The complete Python suite
passed **138 tests**. Full seven-job qualification remains separate at the PR head.

The first local TSan fixture attempt could not start CTest because the root-built
temporary directory was not writable by the PostgreSQL test user. Its setup FAIL is
retained separately; correcting only disposable build-directory permissions enabled
the reported suite. The build-evidence recorder's dirty-source dry run also deliberately
remains FAIL; all instrumentation commands succeeded, and the final clean-commit
recorder is required to pass independently.

Final PR-head results are recorded in the new PR's description and immutable GitHub
check suite. The following table describes this report's pre-CI checkpoint, not the
later result. No earlier PASS is inherited by that head:

| Required qualification | Status |
| --- | --- |
| Detector oracle and positive/negative controls | Pending final recorded source |
| Worker recovery/shutdown/allocation regressions under TSan | Pending final recorded source |
| Network/TLS/PostgreSQL recovery | Pending final recorded source |
| Complete Python and English validation | Pending final recorded source |
| Linux normal; ASan/UBSan; TSan | NOT RUN on final PR head |
| Linux x86/open.mp; Windows x86/open.mp; Windows x64 | NOT RUN on final PR head |
| Downloaded package verification | NOT RUN on final PR head |

The nineteen original and two PR #29 real-player scenarios on each x86 platform must
remain enabled. The final CI must identify the exact proposed PR head. Until those
results and any confirmed component regression are complete, this report's workaround
verification does not authorize merging the PR. The 24-hour campaign remains **BLOCKED**.
