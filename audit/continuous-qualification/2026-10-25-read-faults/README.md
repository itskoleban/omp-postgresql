# SQL read fault qualification checkpoint

## Continuity and source identity

PR #32 was already merged when this execution began. No qualification PR was
open. The new branch `qualify/v0.1.0-sql-read-faults` starts at verified master
`6fe5a4b1a56489c12ff5aa148a7979d98ececeac` and is published as PR #33.
The source/test commit is `bccc29112a7b3b7664f1b7eb3adc5ab29b7787f4`, tree
`7f8b0fc0519963d5a676c9ac6ffd654385b384b6`. `manifest.json` binds each changed
source and retained local capture by SHA-256. A later evidence-only commit does
not replace the need to inspect the final head's hosted CI.

## Technical change and scope

No additional production defect was confirmed. The bounded read loop introduced
in PR #32 is extracted unchanged to `pg::detail::read_sql_stream`, declared in
the internal `src/sql_reader.hpp`. The file path validation, opening, filename
context, public headers, failure codes, diagnostics and SQL dispatch order are
unchanged. The extraction allows deterministic faults in the actual production
loop without replacing allocators, OS calls, or sanitizer interceptors.

The new cross-platform test buffer throws a read exception after exactly
0, 1, 4095, 4096, 4097 or 8192 bytes, with a valid INSERT prefix followed by
padding. Every case must raise `Error::file`, never return a partial prefix.
Clearing the stream and repairing its source must return exactly the full input.
Separate cases distinguish failbit, badbit and badbit plus EOF from successful
EOF, and retain empty, exact-size, block-boundary and SIZE_MAX budget coverage.
These are modeled stream/device errors, not a claim to have delivered kernel
EINTR, interrupted physical storage writes, or tested power-loss durability.

Linux OS tests independently establish actual file and directory EACCES before
checking both reader entry points and recovery. A separate normal-build process
sets its own soft descriptor limit to zero, verifies an explicit open failure,
restores its previous limit and reads successfully. Total descriptor exhaustion
is not configured for sanitizer runtimes, following the existing documented
runtime limitation; the stream-error and permission tests remain configured for
ASan/UBSan and TSan. No existing test or qualification gate was weakened.

A dedicated Linux PostgreSQL test covers both whole-file and named-section
statements under both file and directory EACCES. Four failed jobs must retain
explicit file failures, zero completed operations, empty results and unchanged
lane SQL-dispatch counts. Restoring permissions and reusing the same statements
must yield exactly four successful INSERTs. Earlier failures and a retained
successful SELECT result must remain unchanged. This test is committed for the
existing hosted workflow; it has not executed locally.

Source review confirms named-section preflight reads before section selection
or item execution, and whole-file execution reads before calling the SQL item
dispatcher. The stream unit tests alone do not prove network behavior; the
permission integration test provides the separate database-side check.

## Local validation

- Normal core and all configured test executables build successfully with GCC
  13.3, C++20, Debug and the existing libpq 17.11 installation. CMake used
  `-DPG_BUILD_COMPONENT=OFF -DPostgreSQL_ROOT=<existing-pg17-prefix>`.
  Component/open.mp builds were not performed locally in this execution.
- Ten selected non-database CTest cases PASS with zero skips:
  `core-tests.txt` and `core-tests.xml`. These include the new stream and
  descriptor cases, existing SQL sections, allocation-free terminals, socket
  outcomes, diagnostics/short writes, scalar types and semantic fuzzing.
- The complete Python suite PASS, 143/143: `python-tests.txt`.
- The new stream suite compiled with `-Wall -Wextra -Wpedantic -Werror` and
  separately with `-fsanitize=undefined -fno-omit-frame-pointer`; execution with
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` PASS (`ubsan-streams.txt`).
  This is UBSan evidence only, not an ASan or TSan pass.
- A deliberate mutation replacing the read-error throw with `return sql` is
  rejected with exit 1 (`mutation.txt`). This expected negative control proves
  the new test detects publication of partial SQL; it is not a newly discovered
  production failure. The mutant was compiled outside the repository and was
  never published as production code.
- Both local permission CTest cases are **SKIP / NOT RUN**, not PASS:
  `permissions-local.txt` and `.xml`. The standalone OS case observes effective
  UID 0; the integration case has no PostgreSQL service profile. The existing
  strict hosted CTest inventory check rejects skips.
- Documentation inventory, English maintenance, 127 original/sanitized
  real-session identities and all 26 frozen historical evidence hashes PASS.
  No historical capture, exception inventory or old manifest was edited.

## Hosted status and next action

Source-head workflow run
[40232299295](https://github.com/itskoleban/omp-postgresql/actions/runs/40232299295)
was queued at this checkpoint. It is **PENDING**, not a qualification PASS.
Inspect the final evidence commit's exact-head workflow in PR #33. Preserve
negative outcomes, inspect actual EACCES and retained-result assertions, and
complete Linux/Windows, open.mp, sanitizer and distribution jobs before making
a multiplatform verdict. An evidence-only push may supersede an earlier queued
run under the existing workflow concurrency policy; cancelled is not PASS.

Phase 2 has gained concrete error/recovery regressions, but the phase set is
not declared complete. Continue remaining storage/resource-lifecycle review and
the independent concurrency/integration scenarios after evaluating these checks.
Do not repeat the old ASan network investigation absent a recurrence or new
evidence. Issue #31 remains OPEN, historical FAIL, ROOT CAUSE UNKNOWN. The
historical Linux logging failure in PR #32 also remains preserved and causally
unresolved. Passing later runs do not explain either observation.

Independent Astra review and its consolidated handoff remain PENDING; Astra was
not invoked. The 24-hour campaign remains BLOCKED until candidate freeze,
approved isolated infrastructure and express owner authorization. Stable
publication requires final owner approval. This execution does not merge,
delete branches, change master, close #31, create tags, or use production data.
