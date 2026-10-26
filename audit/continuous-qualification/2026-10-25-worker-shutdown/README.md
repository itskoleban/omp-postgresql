# Worker allocation shutdown qualification follow-up

## Exact baseline and negative evidence

Continue PR #33 on `qualify/v0.1.0-sql-read-faults`, baseline
`0e777e968d21a6d90862f62d2f03f24f8e8d05e0`.
Master remains `6fe5a4b1a56489c12ff5aa148a7979d98ececeac`.

[Run 40232497444](https://github.com/itskoleban/omp-postgresql/actions/runs/40232497444)
remains FAIL: six jobs passed; Windows x64
[job 121287135950](https://github.com/itskoleban/omp-postgresql/actions/runs/40232497444/job/121287135950)
failed `worker_allocation_integration`, 23/24 CTest cases passed.
The recovery scenario printed PASS, followed by
`worker_allocation FAIL Invalid connection handle or owner`.
Retained artifact 12613991049, ZIP SHA-256
`1458aa469375726299e729f215102e4b0339b6ddd927010d51ba3f66f8dd9f9b`.
The baseline Linux OFF, ASan/UBSan, TSan, Linux x86/open.mp,
Windows x86/open.mp and downloaded-distribution jobs all passed. This does
not erase the Windows x64 failure or qualify the modified source.

## Test defect and correction

Static control-flow evidence identifies a stale-handle lookup in the shutdown
test: `Engine::poll` delivers the final disconnected event with the handle
still valid, and removes that handle on the next poll. Terminal publications
and connection-state observation occur independently during polling. A close
event can be observed before both terminal jobs have been consumed. The next
poll then retires the connection, and the old test unconditionally looks up
its handle to inspect lane statistics or wait for shutdown.

The test now captures the SQL-command count while consuming the final close
event. It waits for both terminal deliveries and the close event, explicitly
polls again to force retirement before checking retained jobs, and verifies
that the retired handle raises Error::invalid. It counts both terminal
callbacks and the close event exactly once, including the final drain.
The recovery scenario still verifies a fresh explicit SELECT after allocation
restoration. Uncertain active SQL, zero completed operations, item metadata,
queued cancellation/failure causes, no replay and allocation-site coverage
remain asserted. No production code or Engine lifecycle contract changes.

The forced retirement is a deterministic regression for the invalid lifetime
assumption; the original scheduling interleaving was observed on Windows.
A successful rerun alone is not the causal evidence: the invalid lookup and
the documented reap rule are directly visible in the source.

## Validation and continuation

- Local `git diff --check`, English maintenance and documentation checks PASS.
- Local Python discovery ran 143 tests: eight errors because CTest is absent,
  one skip. Overall FAIL / incomplete environment; no full-suite PASS claimed.
- Local C++ compilation, PostgreSQL integration and mutation execution NOT RUN:
  CMake/CTest and PostgreSQL development/server dependencies are absent.
  Additional package installation was rejected by automatic approval review
  because third-party package services are outside the authorized scope.
- Publish this test correction to the existing PR and inspect its exact-head
  hosted workflow, including Windows x64 and both allocator-injection modes.
  A queued or running workflow is PENDING, never PASS.
- If this change passes, finish remaining storage/resource and concurrency
  qualification. Independent Astra audit remains PENDING, not performed.
- Issue #31 remains OPEN, ROOT CAUSE UNKNOWN. Its original ASan network FAIL
  and the prior Linux logging FAIL remain unchanged and causally unresolved.
- The 24-hour campaign remains BLOCKED without express owner authorization;
  no master changes, merge, branch deletion, release, tag or production use.
