# Candidate versioning and owner-controlled publication

The candidate being prepared is **v0.1.0-rc.1**. CMake's numeric `PROJECT_VERSION` remains
`0.1.0`; the tracked, non-cache `PG_RELEASE_CANDIDATE` value supplies `rc.1` to package
metadata and names. The component's SDK `SemanticVersion` receives those same numeric
major/minor/patch/prerelease values from CMake. No Pawn signature changes. Historical
experimental packages retain their original identity. A future candidate changes the
tracked candidate number, release notes and qualification evidence together.

## Read-only preparation

The existing seven-job build/test workflow compiles once per configuration, tests both
extracted x86 packages in fresh official servers, then downloads and verifies them.
Its final job calls:

```sh
python3 scripts/release.py prepare --packages build-dist-download --output build-release
```

Preparation requires both platform ZIPs and their checksum files, a clean source
checkout, matching full commits/candidate versions, successful package verification
and matching release notes. It refuses to overwrite an existing output directory.
It adds the exact tracked source tree via `git archive`, excludes untracked/ignored
local files, and produces:

- Both original platform ZIPs and SHA-256 sidecars, without rebuilding or relabelling.
- A source ZIP identified by version and source commit, including build scripts,
  tests, public include, documentation and project licenses.
- `release-inventory.json`, embedding both full dependency/build/license/payload
  manifests, archive hashes, source hash, exact commit and intended tag.
- `RELEASE-NOTES.md` and `SHA256SUMS` covering every other release asset.

Identical source and package inputs yield identical bundle bytes. The inventory is
unsigned; integrity hashes alone are not authenticated provenance. Dependency build
records are included in package manifests, but complete dependency sources/toolchains
and signed attestations are not bundled. Compiler updates can change binary inputs.
The named artifact `omp-postgresql-release-FULL_COMMIT` is preparation, not approval.

## Manual release workflow

Only the owner may authorize main integration, creation of the final tag and public
publication. No automation run in this project creates tags, merges branches or invokes
the publication workflow on the owner's behalf without that authorization.

After reviewing/merging the candidate, wait for a successful `build-and-test` push or
manual run on **master at the exact final commit**. Review all seven jobs, their no-skips
CTest qualification, thirteen compiled public examples and eleven unattended public-example
scenarios on each platform, the separate real Pawn measurement fixture, dependency
inventories, remaining limits and the completed independent audit. The owner then
creates the corresponding tag on that exact tested commit.

Manually dispatch `release-candidate` from master with:

- `ci_run`: the successful main CI run ID.
- `tag`: the existing approved tag, such as `v0.1.0-rc.1`.
- `confirmation`: that identical tag, acknowledging the selected release action.
- `action`: `draft` (default) or `publish` (explicit public prerelease authorization).

The read-only job rejects branch/PR/fork/failed/incomplete CI, wrong workflows, missing
or unsuccessful jobs, unmerged commits, tags targeting another commit, source-version
mismatches and incorrect confirmation. It downloads only both named platform artifacts
from that run, rechecks all package gates and assembles the release assets anew from
the tagged source. It never uses a preview-merge package for publication.

A separate job alone has `contents: write`. It downloads the immutable artifact from
that same workflow execution, verifies `SHA256SUMS`, and creates a **draft prerelease**
with all assets using GitHub CLI's `--verify-tag`. `publish` then makes that draft public;
ordinary pushes, tag pushes and successful CI never trigger this workflow. It never
marks the candidate as latest/stable or modifies an existing release. If upload fails,
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
