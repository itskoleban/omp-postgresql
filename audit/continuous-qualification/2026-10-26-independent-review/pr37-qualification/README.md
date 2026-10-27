# Completed qualification checkpoint

The selected source is `0a65609d115b8fb8eed82022dbc030d5f594b79e`, workflow
[40331053180](https://github.com/itskoleban/omp-postgresql/actions/runs/40331053180),
attempt 1. The completed observation is seven successful jobs, six CTest reports
containing 200 executions without failures, errors, disabled cases or skips, and
four complete 23-scenario real-session suites. This packet preserves that finite
qualification; it does not conduct a new execution.

`verification.json` records the reviewed identities, counts, package hashes,
source/master tree equivalence and limitations. Source and master
`4dd188bfb1ea1c6e56f3f4a275156bd2e489953d` both have tree
`b754267919c2fb6a4f0af188f671cfa772434e10`. The packages continue to identify the
source commit from which they were built. `master-run-observed.json` is an earlier
in-progress observation of the separate master run, not a claim of completion.

## Preserved evidence

`raw-evidence.zip` contains the six original JUnit files, the complete public TSan
build-evidence directory (manifest, detector/control executables, objects, source
and command outputs), four already-sanitized original session results and their
original file ledgers, and the two original package manifests and checksum
sidecars. Every selected member retains its exact bytes. No decoding, newline
normalization or redaction was applied while copying it. `raw-index.json` binds
each member to the original artifact ID, name, archive digest/size and member path;
nested package member records also identify their inner archive. No database
directories, service files or TLS keys were selected.

The JSON run/jobs/artifact metadata files are connector responses serialized as
local JSON snapshots. Their fields were retained; these are not a claim to preserve
the original HTTP wire encoding. `linux-python-pawn-observed.json` and
`superseded-final-statuses.json` retain the previous explicit observations used by
the aggregate verifier. `sha256.json` hashes the checkpoint files except itself.

The full original outer ZIPs were downloaded and verified against the API-reported
sizes and SHA-256 digests. The aggregate verifier also checked both complete
package payload inventories and all files referenced by the sanitized session
ledgers. This bounded checkpoint omits the full distribution binaries and most
session logs. The preserved session ledgers identify omitted original members;
their absence here is intentional. Repeating complete payload/session-ledger
verification requires the original GitHub artifacts, whose immutable identities
are recorded. The standalone verifier checks the included evidence and binding;
it cannot recreate omitted bytes.

## Offline verification

Run `python verify_checkpoint.py` from this directory. It validates the checkpoint
ledger, raw archive/member hashes, source/job/artifact identities, all 200 CTest
statuses and allocation coverage, TSan public-file hashes and source/instrumentation
records, four real-session inventories and package-manifest identities.

TSan records project/control instrumentation; it does not establish full libpq or
OpenSSL instrumentation or all possible thread schedules. The recorded PR event
`GITHUB_SHA` is a merge reference; the actual clean tracked-source checkout is bound
by its recorded `git rev-parse HEAD` and status output. Generated untracked CI
inputs in the session records do not modify tracked source.

The original historical ASan cleanup failure remains FAIL with ROOT CAUSE UNKNOWN;
issue #31 is open. Passing this checkpoint does not resolve that historical risk.
The owner confirmed no isolated long-run runner exists yet. No runner, 24-hour
campaign, stable release or publication was created or authorized by this packet.
