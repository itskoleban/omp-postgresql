# Contributing

Write all maintained repository prose in English: documentation, comments, diagnostic
messages, example explanations, test descriptions, reports, commit messages and pull
request titles/descriptions. Review new identifiers for consistent English terminology.
Use UTF-8; English technical writing may contain Unicode symbols, proper names and
international test data. ASCII-only rules and probabilistic language classifiers are
not the repository policy.

Keep production corrections, test improvements and language-only edits in separate,
clearly named commits within the same pull request. Report technical verification and
language-only verification separately, using exact commit SHAs. Run focused tests,
affected regressions and the complete CI pipeline without weakening its gates.

Before changing a runtime string, diagnostic, identifier or literal, search tests,
scripts, documented protocols and downstream consumers for exact-content dependencies.
Preserve public API/ABI, numeric codes, SQLSTATEs, configuration keys, serialization,
wire strings and external compatibility. A language-only commit must not change code,
test expectations, machine-readable formats, executable examples or protocol contracts.
If a compatible internal reference needs an update, place that change in a separate
technical commit and prove the compatibility result before translating prose.

Never translate or silently rewrite historical captures, hash-bound reports, original
manifests or third-party license text. Preserve historical FAIL, BLOCKED and NOT RUN
results and their source identities. Add a new English explanation with provenance
instead. Exact legacy heading IDs are compatibility literals, not prose to translate.
Record exceptions and their English rationale in `.github/english-policy.json`;
exceptions name exact files and SHA-256 identities, never entire future directories.

Run these checks before submitting:

```sh
python scripts/check_english.py
python scripts/check_docs.py
python scripts/real_session_evidence.py verify audit/real-session-evidence
python scripts/verify_audit_provenance.py
python -m unittest discover -s tests -p 'test_*.py' -v
```

The deterministic residue check detects the explicitly reviewed legacy vocabulary and
checks frozen exceptions. It is deliberately not a general language detector. Reviewers
must read all changed prose for English, accuracy and compatibility, including content
outside that vocabulary. Review any change to the rule list or exception identities.
See [language maintenance and exceptions](docs/language-policy.md) for the migration record.

Keep the 24-hour campaign blocked until isolated infrastructure is verified, the
candidate is formally frozen and the owner explicitly authorizes execution. A passing
pull request does not authorize a merge, tag, release or production access.
