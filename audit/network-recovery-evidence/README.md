# Phase 2 capture identities

`execution.json` binds each local run to its source, toolchain, command, result and
limitations. `sha256.json` identifies every selected public file and its original
input digest. The ledger does not hash itself or this explanation.

`stdout.txt` files are explicitly labelled excerpts from CTest's full LastTest logs.
They retain the test output, SQL, numeric status/item metadata and negative assertions.
Localized external CTest date headers/footers are omitted; original complete log hashes
remain in the manifest. Personal environment paths are sanitized by the existing
publisher's path rules. No original raw log is edited. The Python negative log retains
the failed packaged-link regression. Wire files retain their exact bytes, UTC clocks,
monotonic order, backend PIDs and fault/cleanup observations without SQL or TLS payloads.

The first two development negatives were uncommitted iterations, with incomplete
immutable input provenance. They demonstrate corrected test-oracle assumptions,
not a reproduced defect at the baseline commit. The exact-source packaged-link failure
at 699e84a and subsequent pass at a96efdf are separate. No FAIL becomes PASS through
publication, hash verification or a later run.

See the [technical report](../network-recovery-qualification.md) and PR #29 for the
final source's seven-job CI and x86 package/session evidence. Those results are not
inferred from these local captures. Historical evidence and English-policy exceptions
remain unchanged. The 24-hour campaign remains BLOCKED.
