# Versioning and owner-controlled publication

This draft configures the future **v0.1.0 source**, with CMake's numeric `PROJECT_VERSION`
set to `0.1.0` and the tracked, non-cache `PG_RELEASE_CANDIDATE` set to `0`. This selects
distribution version `0.1.0` and channel `stable`; positive values retain the RC route.
Stable readiness and publication remain blocked. Package names, manifests and
the component's SDK `SemanticVersion` derive from those same source values.
Exact-commit qualification and owner authorization remain publication gates. The public
Pawn API signatures are unchanged. This source preparation authorizes no merge, tag or release.

The public [v0.1.0-rc.1 testing Pre-release](https://github.com/itskoleban/omp-postgresql/releases/tag/v0.1.0-rc.1)
retains its tag, assets and source `db2bfb9b816b0042b8ef36e847cf9fb1c24e94fc` unchanged.
Later development bundles do not replace that release. Historical experimental packages
also retain their original identities. Any new stable or RC source identity requires
matching release notes and exact-commit qualification.

## Freeze before the final campaign

Reuse the successful seven-job RC.3 baseline at
`b715a971e71047a9c73f6b85489fc342317ba86b` from
[CI 41372458054](https://github.com/itskoleban/omp-postgresql/actions/runs/41372458054)
for its verified scope. It does not qualify later corrections or this stable-source identity.
After owner-approved integration of all required corrections and release documentation,
require successful exact-head checks and all seven jobs on the exact final `master` commit.
Freeze that source SHA, both tested x86 packages, source archive, installed bytes,
dependencies, loader/runtime identities, fixtures, configuration and seed.

On the verified isolated host, complete all required preflight checks and the 100,000-case
extended semantic fuzzing stage before requesting campaign authorization. Record reviewed
historical-risk dispositions and fixed acceptance/stop criteria. The 24-hour sustained
campaign must be the final testing campaign; it is not authorized or executed by this draft.

After the final campaign, review retained evidence without running another testing campaign.
Retain the results outside the frozen source checkout so recording them does not change
the tested commit. A tracked change or changed deployed input invalidates the freeze:
review its impact, qualify the affected checks, freeze again and complete a new final
campaign before claiming qualification. Only a separately owner-authorized tag and release
may identify the exact source that completed this process.

## Read-only preparation

The existing seven-job build/test workflow compiles once per configuration, tests both
extracted x86 packages in fresh official servers, then downloads and verifies them.
Its final job calls:

```sh
python3 scripts/release.py prepare --packages build-dist-download --output build-release
```

Preparation requires both platform ZIPs and their checksum files, a clean source
checkout, matching full commits/versions/channels, successful package verification
and matching release notes. It refuses to overwrite an existing output directory.
It adds the tracked source export via `git archive`, honors `.gitattributes` export exclusions
(including internal `/audit/` records), excludes untracked/ignored local files, and produces:

- Both original platform ZIPs and SHA-256 sidecars, without rebuilding or relabelling.
- A source ZIP identified by version and source commit, including build scripts,
  tests, public include, documentation and project licenses.
- `release-inventory.json`, embedding both full dependency/build/license/payload
  manifests, archive hashes, source hash, exact commit and intended tag.
- `RELEASE-NOTES.md` and `SHA256SUMS` covering every other release asset.

Source ZIP timestamps use UTC independently of the operator's local timezone.
Identical source and package inputs with the same Git/archive toolchain yield identical
bundle bytes; this does not establish reproducible compilation. The inventory is
unsigned; integrity hashes alone are not authenticated provenance. Dependency build
records are included in package manifests, but complete dependency sources/toolchains
and signed attestations are not bundled. Compiler updates can change binary inputs.
The named artifact `omp-postgresql-release-FULL_COMMIT` is preparation, not approval.

## Manual release workflow

The procedure below records the publication controls. RC.1 publication is complete;
do not redispatch it to replace existing assets. A future release needs a separately
authorized identity and exact-source qualification.

Only the owner may authorize main integration, creation of the final tag and public
publication. No automation run in this project creates tags, merges branches or invokes
the publication workflow on the owner's behalf without that authorization.

After owner-authorized integration of the candidate, wait for a successful `build-and-test` push or
manual run on **master at the exact final commit**. Review all seven jobs, their no-skips
CTest qualification, thirteen compiled public examples and eleven unattended public-example
scenarios on each platform, the separate real Pawn measurement fixture, dependency
inventories, remaining limits and the preserved independent audit. For stable v0.1.0,
complete the freeze/preflight/final-campaign process above before the owner separately
authorizes creation of the corresponding tag on that exact tested commit.

Manually dispatch **manual release** (`.github/workflows/release.yml`) from master with:

- `ci_run`: the successful main CI run ID.
- `tag`: the existing approved new tag, such as `v0.1.0` or a separately qualified `v0.1.0-rc.3`.
- `confirmation`: that identical tag, acknowledging the selected release action.
- `action`: `draft` (default) or `publish` (explicit public stable/RC publication authorization).

The read-only job rejects branch/PR/fork/failed/incomplete CI, wrong workflows, missing
or unsuccessful jobs, unmerged commits, tags targeting another commit, source-version
mismatches and incorrect confirmation. It downloads only both named platform artifacts
from that run, rechecks all package gates and assembles the release assets anew from
the tagged source. It never uses a preview-merge package for publication.

A separate job alone has `contents: write`. It downloads the immutable artifact from
that same workflow execution, verifies `SHA256SUMS`, and creates a **draft** with all
assets using GitHub CLI's `--verify-tag`. The canonical stable tag selects
`--prerelease=false`; an RC tag selects `--prerelease=true`. `publish` then makes that
draft public with the same channel. Ordinary pushes, tag pushes and successful CI never
trigger this workflow. Both channels use `--latest=false`; publication does not select
the repository's latest release. The workflow does not modify an existing release. If upload fails,
the draft remains unpublished; inspect it and obtain authorization before cleanup or
any retry that would change existing release state. A default draft run deliberately
leaves publication to a later explicit owner action in GitHub's release UI.

The write job resolves the remote tag (including annotated tags) again immediately
before draft creation and before publication, rejecting movement/deletion since
preparation. GitHub's release API does not atomically lock a tag to an asset commit;
an operator allowed to mutate tags can still race the API call. Tag protection and
owner coordination govern that remaining privileged-mutation window. Generated
standalone notes use documentation URLs pinned to the tested commit.

The workflow adds no environment protections or repository settings; owners can impose
additional GitHub branch/tag/environment rules independently. Run metadata is checked
against GitHub's authenticated API; the content inventory is still unsigned. Neither
this procedure nor an RC label certifies production safety.
