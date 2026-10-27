# Exact-master qualification checkpoint

Selected source: `4dd188bfb1ea1c6e56f3f4a275156bd2e489953d`, tree
`b754267919c2fb6a4f0af188f671cfa772434e10`. Workflow
[40362033043](https://github.com/itskoleban/omp-postgresql/actions/runs/40362033043),
attempt 1, completed all seven jobs successfully. The independently downloaded,
size/hash-verified artifacts record six CTest reports with 200 executions,
zero failures/errors/disabled/skips, four complete 23-scenario real-session
suites, and both master-built installed distributions with verified identities.
This packet preserves those observations; it does not rerun their workloads.

`verification.json` records counts, jobs, source/artifact/member identities,
TSan instrumentation and package bindings. `candidate-final-summary.json` also
includes both complete package manifests. The selected distributions identify
master source 4dd188b. Earlier PR-source packages and evidence remain separately
preserved in the sibling `pr37-qualification` checkpoint.

## Raw evidence and offline checks

`raw-evidence.zip` preserves 97 original selected members: six JUnit reports,
the complete public TSan evidence directory with manifests, controls, objects,
sources and command outputs, four sanitized session results and their original
file ledgers, two package manifests/checksum sidecars and three original release
preparation inventory files. Each retains its exact
bytes. `raw-index.json` binds member paths/hashes/sizes to the original artifact
IDs, original paths, outer archive digests/sizes, and applicable inner archives.
No database directories, service profiles or TLS keys were selected.

Run `python verify_checkpoint.py` from this directory. It checks this packet's
ledger, raw member hashes, final job/source/artifact records, 200 successful
CTest statuses and seven allocation scenarios in every configuration, TSan
public-file/source/instrumentation records, all four session inventories and
the installed package manifest/component/runtime bindings. The coordinator
also independently checked CTest run statuses and compared all four scenario
inventories with the source's 23 explicitly named cases.

Full original outer ZIPs were independently verified against API sizes/digests;
the original aggregate verifier checked complete payload and session-ledger
files. This bounded packet omits distribution binaries and most session logs.
The retained session ledgers identify omitted members. Repeating full payload
verification requires the original GitHub artifacts identified by this index;
the offline verifier cannot recreate omitted bytes. Metadata JSON files are
serialized connector snapshots, not original HTTP wire encoding. `sha256.json`
hashes included checkpoint files except itself. Git attributes preserve packet
bytes rather than applying newline conversion.

TSan instruments project/control code, not all dependency internals. Finite
qualification and hashes do not establish prolonged stability, signed provenance,
hermetic compilation or an immutable deployed loader/runtime environment.
Issue #31 and the historical network/logging/local full-suite failures remain
unresolved; later passes do not explain their causes. The owner confirmed that
no isolated runner exists. No runner was provisioned or verified, and no 24-hour
campaign, publication or release was authorized or started by these checks.
