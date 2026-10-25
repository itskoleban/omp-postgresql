# Preserved Linux x86 logging observation and evidence-capture correction

## Original negative evidence

Source `201ef37c5691ff8f095a9f7b8ff401cbe35e2cbd`,
[run 40228419561](https://github.com/itskoleban/omp-postgresql/actions/runs/40228419561),
[job 121274971695](https://github.com/itskoleban/omp-postgresql/actions/runs/40228419561/job/121274971695).
The core CTest inventory completed 27/27 with no skips. The later open.mp logging
smoke printed `PG LOGGING PASS`, but its independent post-exit file assertion
failed: `Component-owned log did not contain timestamped numeric events`.
The process exited normally; the console contains a job_terminal record and
`log_dropped count=1 sink_failures=0`. The required component-file record was
not established by the verifier. This job remains **FAIL**.

The uploaded [original artifact 12611907432](https://github.com/itskoleban/omp-postgresql/actions/runs/40228419561/artifacts/12611907432)
is retained byte-for-byte in `original-linux-x86-artifact.zip`, SHA-256
`8ff583e53da7f7727c34f2edef6c04a0cfa5f4d8f8b0e977af8b1d7802b86536`.
The index binds every member's identity. The standalone console copy retains its
original bytes. No historical FAIL or artifact content is replaced.

## What is and is not established

The archive contains the console but omits `logs/postgresql.log`, which the
verifier inspected. Its contents cannot be recovered from that archive. The
original verifier combines timestamp presence and job_terminal presence into a
single rejection, so neither an empty file nor one missing record is independently
proven. Source review confirms the writer deliberately uses a nonblocking try-lock
handoff that can report dropped diagnostic events. A count of one alone does not
prove which event was dropped or establish the particular failure's cause.

This is a new unresolved qualification observation, not evidence that the SQL
reader returned incorrect results, and not a correction/explanation for issue #31.
No SQL, memory, writer, fixture or scheduler root cause is claimed.

## Concrete workflow changes

Retain component-owned `postgresql.log*` files from both source and packaged Linux
and Windows server directories, in addition to the existing console captures.
Before checking the logging file's content, print only its byte count, terminal
record count and SHA-256; do not print private payloads. The exact existing
terminal-event, timestamp and privacy assertions remain unchanged.

The real-player step now depends explicitly on successful component, Pawn and
server preparation rather than success of the unrelated preceding smoke step.
It runs after a smoke failure when all three prerequisites succeeded, and never
runs after cancellation. Neither job failure status nor package admission is
relaxed: no continue-on-error is added, and later package steps retain their
normal success precondition. This permits independent player-token evidence
without hiding the original failed smoke.

## Continuation and release gates

The changed workflow and evidence capture require their own exact-head hosted
execution. A later PASS would show that execution's success only; it would not
retroactively explain this FAIL. If the failure recurs, inspect the now-retained
component file and loss counters before changing runtime code or test contracts.
Do not remove the terminal-event/privacy assertions or disable diagnostics to
obtain a passing build. Record final head, run, job and token outcomes in PR #32.

Source 201ef37 finished with five successful jobs (Linux OFF, ASan/UBSan, TSan,
generic Windows and Windows x86), one failed Linux x86 job and one skipped
distribution-verification job. Windows x86 completed both new real-network token
cases successfully, along with its existing real-session and package checks.
Linux's new token cases were not reached because of the preceding failed smoke.
The overall workflow remains FAIL; this is not a full multiplatform PASS.

Dedicated SQL permission and interrupted-read scenarios and independent Astra
review remain pending. Issue #31 remains OPEN, ROOT CAUSE UNKNOWN, with its
original FAIL preserved. The 24-hour campaign remains BLOCKED pending express
owner approval and approved infrastructure. No merge, master update, branch
deletion, tag, stable publication or production operation is performed.
