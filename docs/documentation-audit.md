# Documentation and tutorial audit

Source baseline: `77527fd031f8bf6064ce0c9d487da943d8d3b879`, recovered October 18, 2026.
Remote default branch was master with no other remote branches and no open pull requests.
Owner-integrated PR #14 and subsequent tag-validation changes were already present.
[Baseline CI 39138377025](https://github.com/itskoleban/omp-postgresql/actions/runs/39138377025)
completed successfully in all seven jobs. This baseline evidence is distinct from tests
of the documentation changes in this branch. Historical audit/benchmark/qualification
records retain their original source identities and limitations.

## Scope and evidence reviewed

Reviewed all 69 public native declarations and three forwards against native dispatch,
Engine acceptance/ownership/polling, Statement snapshots, session execution/transactions,
file/section capture, parameter encoding, stream ownership and their existing tests.
Reviewed CMake installation rules, both workflows, package/release checks, all existing
guides and six original Pawn examples. No runtime source or public API change is intended.
This is an internal documentation audit, not another independent model/security audit.

## Findings and corrections

| User impact   | Finding                                                                                                                                                                                     | Resolution                                                                                                                                                                                                     |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| High          | A technically complete API lacked a progressive working introduction and an account-session example. Reused player IDs could be mishandled by readers inventing their own callback pattern. | Add a first-query path, focused CRUD/types/saves/transfer demos, and explicit job/token/session tracking in an interactive account tutorial. Hashing remains external and default authentication fails closed. |
| Medium        | Installation described the independent Astra audit as future work although its report/fixes were integrated.                                                                                | Link the completed report; preserve exact-candidate and owner approval requirements.                                                                                                                           |
| Medium        | The native reference's section-name pattern had been corrupted by Markdown formatting, allowing a literal `*` and omitting a leading underscore.                                            | Restore `[A-Za-z_][A-Za-z0-9_]*` from the lexer contract; correct the nearby file-native name and array/OID formatting.                                                                                        |
| Medium        | The reference discouraged another example because six regression modes covered the lifecycle, leaving realistic account/transfer learning gaps.                                             | Keep original tests and add focused tutorials with prerequisites and explicit runtime limits.                                                                                                                  |
| Medium        | CI compiled a fixed list of six scripts, with no checked-in signature/link validator. New scripts could bypass validation.                                                                  | Discover all Pawn examples, compare full declarations/defaults/tags/callbacks, check relative targets/headings and enumerate SQL fixtures in the catalogue. Repeat checks on extracted packages.               |
| Medium        | Installation only shipped one SQL fixture and omitted administrative example assets.                                                                                                        | Explicitly install the required tutorial SQL and credential-free operator examples; retain narrow package allowlists and ignored/private-file rejection.                                                       |
| Clarification | Acceptance, SQL success, COMMIT durability, dispatch timing, polling and result indexing are distinct.                                                                                      | Add practical troubleshooting/saving/transaction guidance without altering the implementation-backed reference.                                                                                                |

## Validation record

The final commit/PR records the completed test results for this change. The new runners
exercise basic/values/saving/transfer in the same disposable database/server infrastructure
as the original scenarios. All eleven examples must compile with Pawn 3.10.11 `-E`, and
source/extracted documentation must pass signature/link/fixture checks. Package/release
regressions remain mandatory. Do not infer interactive authentication coverage from those
unattended tests.

The account example's provider-disabled path and optional provider-enabled build are
separate validation targets. A compiled optional provider interface does not qualify a
provider binary, real-player login, disconnect/slot-reuse races or provider timeout behavior.
The pinned provider's actual dispatcher was inspected to resolve an inaccurate hash-callback
comment in its include: no `hashid` is supplied in that revision. The tutorial adapter follows
the implementation and keeps tokens in a server variable across GMX. Its persistent schema
fixture is also applied by psql inside the disposable test cluster, without authenticating players.
No comparative performance measurement or production-readiness claim is made here.
