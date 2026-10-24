# Original identities and sanitized representations

The evidence observed at PR #27 commit
`101f540520657273f5bc987b0ae834a5bc45000f` had 127 registered files.
The raw originals were verified against that commit's ledger before transformation.
This directory now contains **58 explicitly identified sanitized copies** and
**69 byte-identical originals**. No PASS/FAIL/BLOCKED/NOT RUN result, assertion,
session/job token, timestamp, SQL control result (apart from an environment path),
or executed-binary hash was changed.

- `original-sha256.json` is the exact original ledger. Its SHA-256 is
  `25d05c1d1bd0b6faaf6c119d62b797bb693d21e5ec01a5a4edc2437d9b81059b`.
- `sha256.json` verifies the files currently present. Every entry retains
  `original_sha256` / `original_bytes`, the new `sha256` / `bytes`, its explicit
  `representation`, transformation counts, and an observation fingerprint.
- `sanitization.json` identifies the original Git snapshot and transformation profile.

The `local-paths-v1` transformation replaces unnecessary personal workspace,
dependency, client-build, cluster, temporary and hosted-runner prefixes with named
locators such as `<WORKSPACE>`, `<DEPENDENCIES>`, `<CLIENT_BUILD>`, `<FIXTURES>`,
`<PROVIDER>`, `<POSTGRES_CLUSTER>`, `<EVIDENCE>` and `<TMP>`. Unknown private paths
get an opaque stable locator. Technical suffixes are retained where their role is
known. Local machine/container hostnames in JUnit become `SANITIZED-HOST`.
XML placeholders are escaped; line endings are preserved. JSON is parsed before
replacing path values, so backslash escapes cannot defeat the transformation.

These locators describe roles, not literal runnable filenames. Supply the actual
build paths using `scripts/run_real_session_ci.py`; the hashes identify the precise
inputs independently of local directory names. The names of fictional test accounts
and non-sensitive generation/job tokens are intentional evidence, not credentials.

Verify the current copies and their retained original identities with:

```sh
python scripts/real_session_evidence.py verify audit/real-session-evidence
```

For a private byte-for-byte extraction of the original snapshot, regenerate into a
**different new directory** using `publish --source ... --destination ...
--original-ledger ... --original-commit 101f540520657273f5bc987b0ae834a5bc45000f`.
The tool rejects originals that do not match their ledger and never changes them.
It compares observed results, QA events and JUnit outcomes before publishing a copy.

New CI session captures retain raw files on the ephemeral runner and upload only
`sanitized-evidence/`, including separate original/new identities, even when the
test command fails. Service profiles, clusters, private keys and binaries are not
included. There are no changes to the network/client/provider assertions.

This is a forward correction, not a history rewrite: earlier Git objects and
previous Actions artifacts are not deleted or relabelled. Original hashes remain
identifiers of those original bytes; they must never be used to verify a sanitized
copy. See [the review](../real-session-review.md) for the Windows and provenance limits.
