# Finite v0.1.0 pre-soak closure and final-source freeze

November 2, 2026. This closes the practical checklist within the scopes below.
It does not qualify a future integrated source, authorize a campaign, or declare
stable readiness. The final 24-hour campaign has **NOT RUN**.

## Finite dispositions

RC.3 source `b715a971e71047a9c73f6b85489fc342317ba86b` passed all seven jobs in
[CI 41372458054](https://github.com/itskoleban/omp-postgresql/actions/runs/41372458054).
Its core, sanitizer, TLS, x86 package and real-session results are reused with their
original source/dependency scope. They are not repeated to create another audit.

| Item | Disposition | Evidence and boundary |
| --- | --- | --- |
| Standalone release-note links | PASS after correction | [PR #49](https://github.com/itskoleban/omp-postgresql/pull/49): absolute links and local anchors preserved, relative documentation targets pinned to the tested commit, malformed/nested constructs preserved whole. Twelve release and three stable-release tests passed locally. |
| ENOSPC, EACCES, positive short writes and descriptors | PASS, reused | [PR #50](https://github.com/itskoleban/omp-postgresql/pull/50) records the inspected RC.3 Linux normal job's 36/36 executed CTests: real isolated tmpfs ENOSPC, EACCES, one-byte RLIMIT_FSIZE write, SQL reader and Engine EMFILE with recovery. |
| Bounded pressure and cleanup | PASS after correction | Existing retained-job/stream saturation and structured allocation-failure tests are reused. Twelve new injected finalizer/workload combinations require all later cleanup and evidence retention, preserve the primary exception and fail on a live monitor after bounded join. Failed OS stop is reported, never converted to successful cleanup. |
| Transaction interruption, response loss and reconnection | PASS | Seven added Engine/PostgreSQL boundaries: incomplete BEGIN/COMMIT/ROLLBACK requests, each response held beyond the command deadline, and opaque TLS outage through both recovery handshake deadlines. Independent SQL/backend/sequence oracles prove outcomes and no automatic replay. Existing thirteen wire cases, including shutdown, are reused. |
| Extended semantic fuzz | PASS | 100,000 cases, seed `1592594996`, on unchanged RC.3 input helpers. The retained manifest binds the source-file, executable and capture hashes. No genuine failing seed was observed; no reproduction is invented. This is local Windows x64 helper evidence, not qualification of the future frozen binaries. |
| Real open.mp lifecycle | PASS, reused | Both RC.3 x86 jobs and installed-package sessions cover GMX, pending filterscript unload, shutdown, callbacks, player disconnect/ID reuse, stale SQL/provider callback rejection, job closure and recovery. No bridge/runtime/provider change here justifies repeating those sessions. |
| Component input boundaries | PASS, reused | Registered-native seeded address/arity/string/array/NULL-mask/VM-ownership/output-atomicity cases plus semantic helpers cover the component's input boundary. No demonstrated uncovered component path requires a new AMX corpus. |
| AMX loader/bytecode, physical partitions, global OOM/handle-table exhaustion, physical storage faults and unsupported protocols/platforms | OUT OF SCOPE | Upstream loader/interpreter and destructive shared-host experiments are not component guarantees. COPY/receiving LISTEN, arbitrary providers, network filesystems, Windows kernel ENOSPC and privileged reparse races retain their documented limits. Bounded outage tests do not establish hours-long partition behavior. |

Historical issue [#31](https://github.com/itskoleban/omp-postgresql/issues/31) remains
**ROOT CAUSE UNKNOWN**. The Windows dependency handle attribution, historical stalls
and logging anomaly retain their original evidence and limitations. A passing later
test does not explain or erase a historical failure. Candidate-specific risk acceptance
is a decision gate below, not an instruction to begin another comprehensive audit.

## Exact prerequisites before the final campaign

1. Obtain owner approval to integrate the focused corrections and future stable-source
   preparation. Resolve their dependency order and include all required metadata,
   English notes, procedures and acceptance criteria **before** freezing the source.
   No merge, tag or publication is authorized by this checklist.
2. Record the final integrated `master` SHA/tree and require successful seven-job
   `build-and-test` CI on exactly that SHA: core/TLS/network/sanitizer checks, actual
   Linux storage faults, both x86 installed official-server/session checks, downloaded
   package verification and matching notes/inventory/checksums. Prior RC.3 or PR runs
   retain their scope and cannot substitute for this final-source result.
3. Record candidate-specific disposition of issue #31, historical failures, dependency
   ownership findings and the explicit exclusions above. A newly reproduced unexplained
   defect or cleanup/evidence failure blocks launch. No automatic replay of UNCERTAIN SQL.
4. Verify an existing disposable Linux x64 host: non-root execution, no production
   credentials/data, loopback-only PostgreSQL, sufficient free evidence storage and
   process/backend cleanup. The `pg-audit-isolated` label alone proves none of these.
   No usable isolated runner is established by this task; do not provision paid services.
5. Freeze a read-only deployment and an external identity ledger: clean source SHA/tree,
   compiler/fixture/provider revisions, component/include and all test executable hashes,
   complete dependencies and actual loaded runtime hashes, TLS/configuration, build
   options, positive uint32 seed and installed packages. Match binaries to source and
   compare them before launch; a version string alone is insufficient.
6. Complete the affected target-host preflight on those same inputs. Require real
   ENOSPC/EACCES/short-write evidence, applicable descriptor/allocation checks, TLS and
   adversarial transaction coverage, retained zero-skip reports and all external
   official-server/session prerequisites. Reuse matching valid evidence; execute only
   affected or target-dependent checks. Run the 100,000-case extended semantic stage
   before any prolonged load, retaining seed, logs and any genuine failing corpus.
7. Freeze 86400 measured load seconds, warm-up/sampling schedule, SQL/result/terminal
   counters and host-specific RSS/resource/latency/shutdown budgets from bounded
   measurements. Stop on sanitizer findings, crash/deadlock, missing/duplicate terminal
   outcomes, semantic violations, unexpected uncertainty, quiescent pending/errors,
   recurrence of issue #31, fixture-cleanup or evidence-collection failure. Do not choose
   acceptance budgets after observing the campaign.
8. Preserve the command list, timestamps, manifests, raw CTest XML/logs, activity and
   resource/latency samples and SHA-256 ledger outside the disposable cluster. Confirm
   retention and review ownership, then obtain explicit owner authorization to start
   the final 24-hour campaign. Preflight PASS provides no campaign authorization.

## Enforced campaign ordering

`scripts/audit_long.py` now runs tool/inventory discovery, **extended-fuzz (100,000)**,
**bounded-preflight**, then **integration-soak** last. A failed earlier stage launches
no later stage. The bounded preflight runs all configured CTests except the configured
long stress case, then runs the same stress executable with zero timed-load seconds;
all bounded load/lifecycle assertions still execute. Skip rejection remains enabled.
The last stage selects only `stress_integration`, retaining its complete load,
quiescence, lifecycle and teardown assertions. No later fuzz or test suite runs.
Metrics validation, cleanup and evidence preservation are campaign acceptance work.

`--preflight-only` completes the first stages without launching the load campaign;
its manifest explicitly reports that mode and `campaign_started=false`. It is not a
24-hour result. The dedicated workflow defaults to this mode, configures the tiny
disposable ENOSPC fixture, retains the fuzz corpus directory and requires a deliberate
mode change for an owner-authorized load. The supervisor rejects a dirty load checkout
and changed source/executable identities between preflight and load. External loader,
dependency, deployment-isolation and risk gates still require the frozen ledger.

Use a new evidence directory for each invocation. Keep failed attempts, original
exceptions and failed cleanup/retention results; never overwrite them with a later PASS.
When fuzz finds a genuine failure, retain its printed seed/iteration and original
`extended-fuzz/audit-fuzz-failure.bin`, minimize a copy while preserving the failing
condition, and add the reproduction before restarting qualification.

After the final campaign, only inspect retained evidence and perform separately
authorized tag/release operations. Keep the campaign report outside the frozen source
checkout and release payload; adding a documentation/version/source commit afterward
would change the qualified source. Any source, dependency, runtime, configuration or
acceptance-criteria change requires a new freeze, its affected preflight, then a new
final campaign. The Engine campaign is not a 24-hour real Pawn/server session and
must never be described as one.
