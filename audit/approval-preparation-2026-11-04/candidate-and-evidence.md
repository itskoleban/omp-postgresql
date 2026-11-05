# Candidate and evidence correspondence

Recorded 2026-11-04, America/Bogota. This is a focused preparation record.

## Baseline verified

Initial checkout was clean on master. Local HEAD and origin/master were both
`3a5df8439881bd514727a2c5e360f9ec0bafaf43`, tree
`21cea7c3b4620c361484be7aa76ad8f73132c545`. A fresh connected GitHub
`branches/master` read confirmed the same remote SHA and tree. Sandbox Git network
lookup failed with `getaddrinfo() thread failed to start`; this did not prevent the
independent API verification. There were no open PRs at entry. `github-baseline.json`
records fresh source, issue and CI metadata. Work is isolated on
`codex/v010-approval-preparation`; master is not advanced by this preparation.

## Reused without rerunning successful suites

- Exact-source CI run 41579975396, workflow_dispatch attempt 1: seven required jobs
  completed successfully, verified through the live jobs endpoint (the combined
  status endpoint has no statuses; that is not evidence of missing CI).
- Preserved semantic fuzz: 100,000 cases, seed 1592594996. The seven helper/fuzzer
  source hashes and negative earlier attempts remain separate. The later Linux
  preflight manifest also records the completed extended-fuzz stage; the earlier
  60-second timeout remains FAIL, not a semantic counterexample or passing run.
- Target preflight attempt 5: PASS, 34/34, exact source and clean tracked inputs,
  campaign_started=false. Its seconds=86400 field is build/plan configuration;
  mode=preflight-only and stage list show that the long load was not started.
- Earlier transaction, network/TLS, sanitizer, storage, allocation and lifecycle
  qualification remains evidence for its exact recorded source/runtime/configuration.
- Prior bounded calibrations, including successful completion and deliberate abort,
  retain their actual resource caps and older supervisor identity. They do not qualify
  the proposed 2 GiB profile or supervisor v2 deployment.
- Launcher 28/28 and supervisor v2 65/65 offline tests are preserved without rerun.
  Their unchanged helper files are hash checked. Offline success is not real deployment.

The external root is
`C:\Users\esteb\.codex\visualizations\2026\11\03\01a1a183-d181-79b1-94d3-e95627612b75\omp-v010-preparation`.
Keep it in place, especially `github/`, `continuation-20261103/target-preflight-attempt5/`,
`calibration-complete1/`, failed/aborted attempts, `focused-closure/` and
`calibration600-20261104T0621Z/` under the continuation directory. Existing ledgers
are immutable references; current integrity checks are a new layer, not replacements.

## Existing artifacts retained as baseline, not republished

The prior release wrapper `github/candidate-release-3a5df843.zip` has SHA-256
`276a4a18e59e6c281a7ab34df5c796f3c4d319d7b9d414f9aa607602e5dee8bf`.
Its seven SHA256SUMS entries were previously verified. Retain the wrapper, original
sidecars, extracted manifests, release-inventory.json, source archive and verification
records. Do not substitute similarly named RC packages from older build directories.

| Exact baseline payload | SHA-256 |
| --- | --- |
| Linux x86 distribution | b337fe1f9f9eedae45d366d9defbd5ce4a9636d191e47ad055ae9d4c5ca85fa7 |
| Windows x86 distribution | 9a0debf70d192389c4ecdaa115379efb10434ba3e52119e68ca54ee6ee4de764 |
| Source archive | 0a3c5f0476a0e60aab9c7a7ff41c58af71de3868b3c0893a88907c49001f1679 |
| Linux PostgreSQL.so | 4d5620aed93dcbc446f5aa02eb93e53b005ee3fd22c41e767a38bbbd61bfbc1d |
| Windows PostgreSQL.dll | 9b1b56118feb76e0253152abaa013dc319acb45eb6789ffe4d22941fdfc4db1b |
| Identical packaged Pawn include | 00cd9369e2451b04daeb1b5b82ee0077ec21fe9a20f1760fb12e86f976280a81 |

Public signatures remain 69 natives and three callbacks. The proposed fix must keep
the include byte-identical. Official component distributions are x86; Linux x64 Engine
campaign binaries are a different target. Package metadata already says 0.1.0/stable;
that is metadata, not evidence of publication or release readiness.

The recorded CI runtime distinctions remain material: server PostgreSQL 17.11 and
Linux x64 libpq 18.6; distribution x86 libpq 17.11; SDK
473b732b2de159773b2e2908fd11aa99f07db284, Pawn
29f1a8c7fd2e01929807cd8c50353fbc93bbd651. Preserve runtime-inventory-attempt1.json,
freeze-after-preflight.json, package manifests and actual loaded-library maps/hashes.
Fresh actual loader identity on the final host remains required; a package dependency
declaration or old ldd output cannot prove what a future process loaded.

## Effect of the proposed Engine correction

Old CI, packages, preflight and calibration remain valid historical baseline records.
They cannot qualify a new Engine executable. Before a new freeze, require explicit PR
review/merge authorization, exact merged source/tree and clean checkout, affected
unit/native allocation/admission and transaction tests, cancellation/shutdown/lifecycle,
network transaction terminal/no-replay checks, resource failure tests, ASan/UBSan/TSan,
x86 open.mp source/extracted-package compatibility, and final required 7/7 exact-source CI.
Rebuild Windows/Linux packages and campaign binaries and regenerate inventories/hashes.
Repeat target preflight and bounded calibration under the new frozen runtime/profile.

Unchanged semantic parsing/type helpers can retain their 100,000-case evidence after
hash comparison; a change in compiler/runtime/flags may still require target-specific
preflight's normal extended fuzz. No request is made to repeat general audits.
Launcher/supervisor offline suites stay reusable if their own files/contracts are
unchanged. Real v2 deployment is still pending and is not replaced by Engine tests.

Review draft release notes and add the integrated identity-reservation correction
only after approval. Use existing release inventory and publication validation tooling
on the exact final commit, preserve SHA256SUMS and package manifests, and have a reviewer
compare source/artifacts/tests. Publishing workflow and stable tagging remain forbidden
until a separately authorized final campaign has passed and release approval is given.
