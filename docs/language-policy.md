# English maintenance policy

All maintained repository prose and contribution metadata use English. The policy
covers comments, diagnostics, examples, reports and test descriptions as well as
public documentation. Maintainers review new prose; automated checks are a repeatable
supplement to that review, not a claim to recognize every human language.

## Migration and compatibility

The repository inventory at `c8528bf89068dfd33fd96bf7350ee59e599d278f` covered every
tracked file. Four maintained narrative reports were translated: A1/A2 resolution,
stability report, real-session qualification and the previous qualification's evidence
index. `audit/language-migration.json` records original commit/hash identities,
translated identities and retained heading aliases. Their original Git history remains
available. Statements about past results keep their original dates and scope.

Runtime strings, public headers, Pawn declarations, examples, tests and scripts were
already English. No diagnostic or runtime string required translation, so there is no
consumer-string migration. The language-only commit changes four Markdown narratives
and adds its provenance record; executable fences, literal commands, external URLs,
test expectations, protocol strings, machine-readable evidence and runtime files are
unchanged. Original numeric spelling is retained where historical reports use it.

## Preserved exceptions

- Raw historical logs, JUnit, CSV and JSON retain their exact bytes, including localized
  Windows tool output, filenames, original observations and FAIL/BLOCKED/NOT RUN values.
- `audit/v0.1.0-pre-24h-qualification.md` and its CSV are bound by the existing manifest's
  `document_hashes`; that report and the manifest's original text values remain unchanged.
- Licenses retain their legal wording, copyright attribution and original identity.
- Legacy heading IDs, named files, SQL/protocol tokens and other exact compatibility
  literals retain their spelling. English text surrounding them explains their meaning.
- Language-check rule data and encoded negative fixtures necessarily describe the
  vocabulary being checked. They are technical test data, not maintained foreign prose.

The exact-file exception inventory is `.github/english-policy.json`. It freezes hashes
and gives English reasons. It does not exempt arbitrary new files placed beside an old
capture. New captures need their own provenance, English explanation and reviewed
exception if they contain localized external output. Never edit a digest to conceal
an altered historical result.

## Deterministic enforcement

`scripts/check_english.py` reads the tracked-file inventory, verifies frozen identities
and checks a small explicit list of legacy residue patterns. It uses no network,
language model, inferred language scores, nondeterministic package or blanket ban on
Unicode. Technical terms, Unicode examples and English names are allowed unless an
explicit residue rule matches; a legitimate exact-content match needs a reviewed,
narrow exception rather than a weaker global rule.

Regression cases check rejected residue, unchanged known exceptions, modified-exception
rejection, new files within historical directories and legitimate Unicode/technical
text. CI executes this check and its tests with the existing regression suites.
The checker cannot prove that arbitrary newly introduced prose is English. Reviewers
must assess that requirement and must inspect policy/exception changes.
