# SQL file boundary qualification checkpoint

## Scope and source identity

Continuation of PR #32 from `a3435d0c8b24d5289dbf2508570dac33c3d8179f`.
Master remains `7a7ff79bb5e1dacaf2bcb33cf3ad44e5225fed1b`. The previous
PR-head [CI run 40226099070](https://github.com/itskoleban/omp-postgresql/actions/runs/40226099070)
completed all seven jobs successfully. That is evidence for the previous head,
not for this patch. The exact published head and subsequent CI disposition must
be recorded in the PR description. `manifest.json` binds these local observations
to the two changed source files by SHA-256; adding evidence does not change them.

## Confirmed defect and correction

`read_sql_contents_raw()` allocated `max + 1` bytes before reading. At
`SIZE_MAX`, unsigned addition wrapped to zero and a nonempty SQL file was returned
as empty content. The new regression failed against the old implementation
(`storage-before.txt`, exit 1). This is a confirmed C++ reader boundary defect;
normal Pawn configuration does not expose an unbounded SIZE_MAX setting. No data
corruption, uncertain-operation replay, or default-setting SQL loss was established.
For ordinary small files the old implementation also allocated the entire budget.

Read at most 4096 bytes per step, with a one-byte oversize probe calculated only
when the remaining budget is smaller than the block. Reject oversized or failed
reads before returning any content. EOF, binary bytes and existing file diagnostics
remain explicit. A small file no longer requires allocation of its whole byte budget.

Unit cases cover SIZE_MAX and SIZE_MAX-1 budgets, empty content, budgets equal to
file length and one byte on either side, sizes 0/1/4095/4096/4097/8192/8193, all
byte values, nonregular paths, and a successful read after an oversized file.
They pass with the corrected reader (`storage-after-unit.txt`).

New PostgreSQL integration assertions exercise both whole-file and named-section
modes. A 4097-byte file containing a valid INSERT prefix must fail with zero SQL
commands dispatched. Repair to exactly 4096 bytes must permit the same statement
to execute once, retain the earlier failure, and produce exactly two table rows
across the two modes. These assertions are committed for CI; they have **not**
been executed locally and are not claimed as passing at this checkpoint.

## Local results and limitations

- Debug component and test compilation: PASS, GCC 13.3, C++20, libpq 17.11.
  SDK `473b732b2de159773b2e2908fd11aa99f07db284` and compiler headers
  `29f1a8c7fd2e01929807cd8c50353fbc93bbd651` match the pinned CMake inputs.
- Nine selected non-database CTest cases: PASS, zero skips, with JUnit retained.
  They include native wrappers, type conversions, terminal queue allocation,
  socket outcomes, diagnostics/short writes, handles, and SQL section boundaries.
- Existing semantic fuzzer: PASS, 4096 cases, seed 1096107081, included above.
- Python regression suite: first attempt FAIL (eight errors because CTest was
  absent from PATH); after supplying CTest, all 143 cases PASS. Both captures remain.
- English maintenance and documentation inventory checks: PASS. Historical frozen
  exception identities remain unchanged. The English check predates these new
  evidence files and must also be rerun after staging them.
- ASan/UBSan build: PASS. The local instrumented CTest run remains **FAIL**:
  eight processes terminate when LeakSanitizer cannot inspect `/proc/<pid>/task`;
  one types executable was not started because its generated file mode was 0644.
  A separate empty-main control with the same sanitizer flags and leak detection
  also exits 1 with the same `/proc` fatal diagnostic. No sanitizer options were
  relaxed and no failing run was relabelled PASS. The diagnostic's ptrace hint
  does not prove that ptrace is the underlying cause.
- Local PostgreSQL fixture: **NOT RUN**. The container maps only UID/GID 0;
  changing ownership to an unprivileged account failed and `runuser` could not
  set groups. No root-start guard was disabled and no modified server was used.
  Hosted workflows provide the required normal PostgreSQL and platform evidence.
- Git CLI push could not authenticate; publication uses the connected GitHub
  Git-data tools with an expected-head check instead. No credentials were exposed.

## Remaining work and continuation

Phase 1 is advanced by the already-qualified handle boundaries and the reader's
allocation correction; it is not declared complete. Add controlled Pawn token
boundary scenarios to the actual session fixtures, and complete resource-lifecycle
review. Phase 2 gains file-size and retained-result regressions; dedicated SQL read
permission/interruption scenarios remain to be qualified. Existing logger ENOSPC
coverage does not alone qualify SQL reader errors.

Next, inspect the new exact-head hosted CI, including PostgreSQL INSERT guards,
ASan/UBSan, TSan, Linux/Windows x86 open.mp, real-session player-ID reuse and
package verification. Keep every new negative outcome. Then continue controlled
SQL read errors and token exhaustion before declaring phases complete.

[Issue #31](https://github.com/itskoleban/omp-postgresql/issues/31) remains OPEN.
Historical run 40164306733, job 121082357048, source
`7ca0d1df1e970e622fad09240bd099b939a563c8` remains **FAIL; ROOT CAUSE UNKNOWN**.
This reader fix does not establish a cause or correction for that cleanup timeout.
No repeat investigation of the historical incident was launched.

No independent Astra audit has been started or claimed. The phases and audit
handoff are not complete. The 24-hour campaign remains **BLOCKED**, pending the
owner's express authorization and approved isolated infrastructure. No master
change, merge, branch deletion, tag, release or production operation was performed.
