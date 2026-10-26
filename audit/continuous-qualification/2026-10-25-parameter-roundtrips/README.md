# Parameter representation qualification

Base: c849ab3dd8a4254b40e9d3d6d60964a7a996a8d4 (merged PR #33).
No qualification PR was open at start. Continue on
`qualify/v0.1.0-parameter-roundtrips`.

## Scope and independent assertions

Extend the existing semantic fuzz executable, already configured in CTest and
hosted sanitizer jobs. No production defect is claimed and no runtime source
is changed.

- FLOAT4: uniformly sampled 32-bit representations; finite values must retain
  their exact bits after encoding and independent standard-library parsing.
  Nonfinite values must raise Error::invalid. Fixed zero, negative zero,
  subnormal, minimum normal and maximum finite boundaries supplement sampling.
- BIGINT: sampled signed 64-bit representations retain exact textual digits
  and OID 20; appended junk is rejected. Fixed minimum/maximum, signed zero,
  overflow, whitespace, empty and malformed sign cases complement sampling.
- BYTEA: an even-length string with an invalid hex nibble must be rejected.
  Existing random byte roundtrips and odd-length rejection remain configured.

## Validation

GCC 13.3.0, C++20, -Wall -Wextra -Wpedantic -Werror. Compile the existing
fuzz executable directly with src/parameters.cpp, src/statement.cpp and
src/sql_sections.cpp; no CMake, libpq or external installation is needed.

Normal -O2: 100000 cases, seed 1096107081, PASS.
UBSan -O1 -g -fsanitize=undefined -fno-sanitize-recover=all:
100000 cases, seed 1234567, PASS.
These are 200000 complete semantic iterations plus the fixed corpus.
They test local parameter encoding, not PostgreSQL interpretation or Pawn.

Negative control: a disposable /tmp copy of parameters.cpp replaced FLOAT4
max_digits10 with 6. The expanded suite rejected it with exit 1 at the fixed
boundary corpus: FLOAT4 boundary representation changed. The mutant is never
published as production source. Its generated failure corpus belongs only to
the deliberately incorrect control, not a discovered component failure.

English maintenance, documentation inventory and diff whitespace checks PASS.
Exact-head hosted CI remains PENDING until inspected. Windows, ASan, TSan,
PostgreSQL and open.mp execution are not claimed from this local run.

## Continuation and release gates

Inspect the new PR exact-head CI and the post-merge master run 40249743442.
Continue resource/storage and concurrency qualification, then consolidate the
candidate and independent Astra handoff. Astra was not invoked; audit PENDING.
Issue #31 remains OPEN / ROOT CAUSE UNKNOWN. Preserve original network and
logging negative evidence; these parameter tests do not explain either.
The 24-hour campaign remains BLOCKED without separate express authorization.
No master change, merge, branch deletion, tag or release is performed.
