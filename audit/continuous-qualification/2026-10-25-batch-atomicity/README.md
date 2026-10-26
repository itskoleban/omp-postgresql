# Batch admission atomicity qualification

Base: afb918c811ac241544a93fa7989debc0c1f1df19, merged PR #34.
No qualification PR was open at start. Branch:
qualify/v0.1.0-batch-atomicity.

## Scope
Extend the existing hosted semantic fuzz target with randomized two-column
batches and exact byte budgets. No production defect is claimed.
Check that a missing trailing parameter leaves the partially copied row
uncommitted, with bindings and type inventory unchanged. Establish explicit
NULL and text types, reject a type change without consuming bindings, and
reject a row one byte over the remaining budget without hitting row count.
Repair the binding, admit the exact remaining budget, and verify both retained
snapshots, explicit NULLs, byte accounting and consumed bindings.
An oversized rebind must preserve its earlier binding and retained rows.
These are local Statement admission guarantees, not PostgreSQL transaction
atomicity or database durability claims.

## Validation
GCC 13.3.0 C++20, -Wall -Wextra -Wpedantic -Werror:
100000 full semantic iterations PASS, seed 1096107081, normal -O2.
100000 full iterations PASS, seed 9876543, -O1 -g with
-fsanitize=undefined -fno-sanitize-recover=all.
Compile tests/audit_fuzz.cpp with src/parameters.cpp, src/statement.cpp and
src/sql_sections.cpp using -Iinclude; no external dependency installation.
The existing numeric, Unicode, SQL section and BYTEA checks remain active.

Negative control: a disposable copy of statement.cpp changed the admission
comparison from remaining budget to total budget. The new suite rejected it
at iteration 0 with exit 1, Invalid input accepted, seed 1096107081.
The mutant and its generated corpus are not production defects or published
runtime code. Seed and iteration reproduce this expected negative control.

Diff whitespace, English maintenance and documentation checks PASS.
Hosted exact-head qualification PENDING. No local PostgreSQL, open.mp,
Windows, ASan or TSan execution claimed.

## Continuity and release gates
PR #34 head 00fb1657c89705cb55f608c02d8481df90a44379 passed hosted
run 40250018869. Earlier master c849ab3 passed run 40249743442.
Current master post-merge run 40251830291 was still in progress at checkpoint.
Inspect this PR's exact-head workflow and that master run before qualification.
Continue resource, storage and concurrency review, then consolidate the
independent Astra handoff; Astra remains PENDING and was not invoked.
Issue #31 stays OPEN / ROOT CAUSE UNKNOWN. Preserve the original network and
logging negative evidence; these tests do not explain either failure.
The 24-hour campaign remains BLOCKED without separate express authorization.
No master mutation, merge, branch deletion, tag, release or production use.
