# Finite pre-soak network and component-boundary closure

November 2, 2026. Baseline `b715a971e71047a9c73f6b85489fc342317ba86b`
passed all seven jobs in [CI 41372458054](https://github.com/itskoleban/omp-postgresql/actions/runs/41372458054).
Its successful suites and real sessions are reused within their original scope.
No stable readiness, physical partition, or 24-hour result is claimed.

| Remaining question | Disposition and evidence |
| --- | --- |
| Incomplete BEGIN/COMMIT/ROLLBACK request | **PASS**: three new real Engine/PostgreSQL cases forward an incomplete Parse frame and close. Client outcomes remain conservatively UNCERTAIN. The old backend disappears; interrupted COMMIT cannot persist its transaction; explicit fresh transactions work. |
| Transaction response withheld past deadline | **PASS**: three new cases hold BEGIN/COMMIT/ROLLBACK CommandComplete until the 1500 ms query deadline. Each publishes exactly one UNCERTAIN terminal result with timeout cause. The independent SQL oracle sees one committed row only for COMMIT, zero for BEGIN/ROLLBACK, and one identity allocation for each attempted INSERT. No automatic replay occurs. |
| Outage through recovery exhaustion | **PASS**: the seventh new case verifies actual TLS and observes the exact active query/PID in PostgreSQL, withholds opaque response bytes and holds both subsequent connection handshakes. The query deadline and two 2000 ms recovery handshake deadlines expire. Initial plus recovery connection attempts equal three, queues settle, the failed pool rejects new SQL, and an independent connection remains usable. Explicit pool recreation after fault release succeeds; the original backend is gone and the queued INSERT identity sequence remains unused. |
| Lost responses, partial writes, shutdown | **PASS, reused**: existing thirteen network scenarios retain their RC.3/CI result, including lost transaction responses, confirmed/uncertain/unsent batch vectors, seven-byte framing, opaque TLS timeout, bounded destruction and queued-write suppression. Application-level fragmentation is not an OS short-write injection. |
| Real open.mp reload/unload/player lifecycle | **PASS, reused**: both RC.3 x86 platform jobs and installed-package sessions include three GMX reloads, pending filterscript unload, pending SQL shutdown, real player disconnect/reuse, stale SQL/provider callback rejection, job closure and PostgreSQL recovery with explicit pool recreation. Runtime/bridge/provider code is unchanged here; another real session run is not justified by these test-only additions. |
| Malformed component inputs | **PASS, reused**: registered-native tests already include 4096 seeded addresses (`0x414d5801`), 16384 adversarial native cases (`4051312678`), arity, packed/unpacked strings, invalid UTF-8, array dimensions, NULL masks, foreign/unloaded VM rejection and output atomicity. Extended semantic fuzz executes separately before the final prolonged campaign. No additional native corpus is justified by a demonstrated uncovered component path. |
| Malformed AMX loader/bytecode | **OUT OF SCOPE**: open.mp/Pawn loads and interprets AMX; this component receives registered native arguments and SDK memory access. These tests do not fuzz the upstream loader/interpreter or certify arbitrary bytecode execution. |
| Physical or hours-long partitions; unsupported protocols/platforms | **OUT OF SCOPE** for this finite pre-soak checklist. The bounded outage qualifies timeout/recovery state transitions, not physical equipment or elapsed-hours behavior. COPY/receiving LISTEN and alternate providers remain outside the candidate scope. |

The new `network_boundaries_integration` CTest has its own 90-second outer bound.
`--network-boundaries-only` selects only its seven cases in a fresh disposable cluster;
the existing `--network-only` still selects its thirteen original cases. Normal CI
executes both tests and requires every configured case, without weakening assertions,
timeouts, sanitizer settings or skip rejection.

The [capture manifest](prestable-network-boundaries-evidence/manifest.json) retains
source scope, compiler/client/server identities, executable hashes, original/public
capture hashes and negative-attempt disposition. Local Windows x64 GCC 16.1.0,
libpq 18.4 and PostgreSQL 17.11 passed the seven new cases in 13.38 seconds
(1110 dynamic checks), one CTest with zero failures/skips, and eight proxy unit methods.
Only a temporary loopback cluster and disposable tables were used. All proxy workers
joined and PostgreSQL stopped. These local client bytes are an additional supported
development observation, not qualification of either distributed x86 package.

The initial sandbox denied loopback connect with WinError 10013 before component
execution; the bounded run outside that restriction passed. No component defect or
failing semantic seed was discovered. No minimized reproduction is therefore created.
Historical issue #31 remains ROOT CAUSE UNKNOWN; successful new cases do not resolve it.
The 24-hour campaign was not started. Exact final-source CI and frozen-host preflight
remain separate prerequisites under the release freeze procedure.

## Diagnostic-build configuration correction

[Exact-head CI 41473863113](https://github.com/itskoleban/omp-postgresql/actions/runs/41473863113)
at `704a4581fb8648d6689b58a7c6756996222e2edf` is **FAIL**. Its Linux ASan job
[125374786445](https://github.com/itskoleban/omp-postgresql/actions/runs/41473863113/job/125374786445)
passed the original network test and the other 33 cases, but the new boundaries
test stopped before any scenario with `Network diagnostic capture initialization failed`.
The instrumented executable requires `OMP_PG_NETWORK_TRACE`; CMake had configured
that environment only for its original invocation. This is a new test-configuration
defect, not a reproduced SQL/Engine failure or an explanation of historical issue #31.
The failed run and its
[ASan artifact](https://github.com/itskoleban/omp-postgresql/actions/runs/41473863113/artifacts/13156761819)
retain that negative result.

Both diagnostic test invocations now receive distinct exclusive-create trace paths.
The boundaries trace is `build/network-boundaries-client.jsonl`, whose parent already
exists even for the standalone boundaries selector. The original collector retains
exclusive creation of `build/network-diagnostic`; no directory is precreated for it
and neither capture overwrites the other. The existing sensitive-value log restriction,
Capture write/overflow checks and always-upload evidence behavior remain enforced.

The new regression configures the actual CMake diagnostic build, reads its actual
CTest JSON inventory, and creates both reserved trace files with exclusive-create
semantics. It **FAILs before** the correction because the boundaries environment is
absent and **PASSes after** it. The local Windows check is configuration-only with a
Linux target; it does not claim execution of Linux ASan code. Its sanitized
[before](prestable-network-boundaries-evidence/trace-config-before.txt) and
[after](prestable-network-boundaries-evidence/trace-config-after.txt) outputs retain
the outcome. Corrected exact-head CI is **PENDING** until its own run completes;
the failed run is not relabelled as successful and no successful runtime suite was
repeated locally for this correction.
