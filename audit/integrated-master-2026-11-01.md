# Integrated master qualification — 2026-11-01

## Source and conclusion

GitHub `branches/master`, the workflow run metadata, job logs, downloaded package
manifests and the source archive identify the same integrated commit:
`27e09b7cffa34968b78cb0025c121fce0a713e65`, tree
`095e7ef91ce50542278a40199c7eac52b6878cf4`.
The branch head was checked again on 2026-11-01. This is the merge of PR #46 and
contains merged PRs #43, #44 and #45. No open PR was present at that baseline.
Issue #31 was open.

[Workflow run 41312759129](https://github.com/itskoleban/omp-postgresql/actions/runs/41312759129)
is a **push to master**, attempt 1, and concluded **success**. It ran from
13:45:06Z to 14:02:17Z on 2026-11-01. This evidence qualifies that integrated
source; it is not inferred from successful PR checks. The passed qualification
was preserved and inspected without rerunning it.

## Jobs and CTest evidence

| Job | Job ID | Result | Main CTest tests |
| --- | --- | --- | ---: |
| Windows x86 MSVC and open.mp | 124866489685 | PASS | 31 |
| Linux x86 and open.mp | 124866489795 | PASS | 36 |
| linux (OFF) | 124866489823 | PASS | 36 |
| windows (x64 engine) | 124866489856 | PASS | 29 |
| linux (THREAD) | 124866490026 | PASS | 40 |
| linux (ON), ASan/UBSan | 124866490049 | PASS | 34 |
| Verify downloaded x86 distributions | 124870505256 | PASS | Package verification |

The six main `qualification.xml` reports contain **206 tests, zero failures,
zero disabled tests and zero skipped tests**. The Windows x64 artifact also
contains five separate fresh-cluster stalled-handshake reports with one test
each, all successful: **211 preserved CTest executions in total**. Conditional
workflow setup steps skipped on incompatible matrix legs are not skipped tests.
`network_recovery_integration` passed on every test matrix leg.

The ASan/UBSan leg uses `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. The retained diagnostic
binary's dependency record contains both `libasan.so.8` and `libubsan.so.1`.
The single instrumented network capture reports `collection.status=PASS`,
`attempts=1`, confirmed hold cleanup (`completed=1`, `error=0`), and
`historical_cause_established=false`. Passing this capture does not resolve #31.

The TSan evidence manifest reports PASS, clean tracked source, the exact commit,
`PG_THREAD_SANITIZE=ON`, `-fsanitize=thread`, retained binaries/objects and real
undefined `__tsan_*` instrumentation references. Its ordering control passed all
20 executions without a race diagnostic. The intentional-race detector control
observed a real TSan race and exit 66, as required. Those expected diagnostic
controls must not be confused with a plugin data race. The 40-test TSan suite
passed, including worker recovery, resource exhaustion and network recovery.

## Official open.mp and extracted packages

Both x86 jobs used official open.mp **1.5.8.3079**, server commit
`c6759bd8d265171ae3d86598895a23d5a8d92a3b`, and verified startup identity
`0.1.0-rc.2`. Windows used the official Windows x86 distribution; Linux used
the official Linux x86 dynamic-OpenSSL distribution. Engine x64 tests are not
evidence of a supported x64 component distribution.

The jobs passed source-build and fresh extracted-package server integrations,
VM-unload/callback regressions, warning-as-error compilation of extracted Pawn
examples, and real-player qualification with the external bcrypt provider.
Each of the four retained real-session `results.json` files contains 23 PASS
cases (92 case executions total), exact `source_commit`, and
`tracked_source_modified=false`. Its broader `source_dirty=true` records
classified CI-generated untracked inputs, not a tracked-source modification.
The extracted-package component hashes match their distribution manifests.

The final job downloaded both uploaded x86 artifacts, checked their artifact
digests, verified and extracted each distribution, checked the packaged
documentation inventory (69 natives and 3 callbacks), prepared the source and
release inventory, and passed `sha256sum --strict --check SHA256SUMS`.

Local inspection of the preserved release artifact independently verified all
seven `SHA256SUMS` entries, all 69 Linux and 72 Windows manifest-listed payload
files, and compared all 202 source-archive files byte-for-byte with
`git archive 27e09b7cffa34968b78cb0025c121fce0a713e65` (no extra files).

| Payload | SHA-256 |
| --- | --- |
| `omp-postgresql-0.1.0-rc.2.27e09b7cffa3-linux-x86.zip` | `47b142168f6567335dfef27acbe6538e5d639117cf2cb337f7326779f0fff618` |
| `omp-postgresql-0.1.0-rc.2.27e09b7cffa3-windows-x86.zip` | `15e1474d143f17756edeaf80bba24406778f7147f77112ee5927a5f7c33a5d2c` |
| `omp-postgresql-0.1.0-rc.2.27e09b7cffa3-source.zip` | `b21c0ec393d2876256d3a26d6db2698bca2ef9cfdcd0edf82d724bca4bf693e8` |
| Linux `components/PostgreSQL.so` | `8ccee132c2beb48feada450b4358176b3172768a163cf33dea23effd99505c05` |
| Windows `components/PostgreSQL.dll` | `acff100ff464382595f6b923b7810be192f2b05a2c82344d36a2f12d79314d84` |
| Both `qawno/include/omp_postgresql.inc` | `00cd9369e2451b04daeb1b5b82ee0077ec21fe9a20f1760fb12e86f976280a81` |

Both distributions are C++20 Release, x86, unsanitized component packages using
libpq 17.11, open.mp SDK `473b732b2de159773b2e2908fd11aa99f07db284` and Pawn
`29f1a8c7fd2e01929807cd8c50353fbc93bbd651`. Linux compiler: GNU 13.3.0;
Windows compiler: MSVC 19.51.36260.0. Linux bundles `lib/libpq.so.5` and requires
compatible i386 system C/C++ and OpenSSL 3 runtimes. Windows bundles libpq,
libssl-3 and libcrypto-3 DLLs and requires its documented Windows/MSVC runtimes.
The inventory includes dependency imports, Linux symbol-version requirements,
licenses and the Windows libpq mutex patch. These are not self-contained builds.

The source still selected RC.2 metadata at this commit. These exact-source
**CI artifacts are not the previously published RC.2 assets**, and no published
release was changed. RC.3 needs its own source identity and qualification after
the version change and any subsequent corrections.

## Artifact preservation identities

The following GitHub ZIPs were downloaded without modification and their local
SHA-256 values matched the GitHub artifact metadata. Extracted copies were used
for read-only inspection. Retain these archives when exporting this evidence;
GitHub's configured expiration is 2027-01-30, not indefinite archival storage.

| Artifact | ID | ZIP SHA-256 |
| --- | --- | --- |
| release inventory and three distributions | 13085519315 | `c7859e6587b69287733d3e2396533b4168beff69da4fcd0aa111e84b36dd308a` |
| Linux x64 OFF logs | 13086276537 | `0e302726e282e654e11c0e447f3ff3b1d42d6944327d5323de0057ed5f390c3b` |
| Linux x64 ASan/UBSan logs and network diagnostic | 13086167926 | `56905ed3e3e039e634a4c65bf4eae4fe1c2726a3395b190a899644712d5ac606` |
| Linux x64 TSan logs and instrumentation evidence | 13086332530 | `7f9b24e33e65afb3e2decddb16cb1d8be537d5f346cac856505da0a4393dc29c` |
| Windows x64 logs | 13086063538 | `38b7db4281c48e04e6eb51b47899c423efbdce76552e72f4f36c86969e314996` |
| Windows x86 logs and real sessions | 13086264373 | `c957120f3e5df3d332ff8c50d0831c3f745c688214e8423d7a0a8a9f40a14edc` |
| Linux x86 logs and real sessions | 13086417810 | `4b83a9a900fdff5e644c2172f87f60483ed2ee90cd62762f40344df09e38ecf1` |

The uploaded platform-wrapper artifacts additionally have these identities:
Linux ID 13086582134, SHA-256
`6d03022b58e5d2d714141765c4684033494c6e5615b0eb9c531a8a810fd1a278`;
Windows ID 13086314363, SHA-256
`49f875335e92cce285fdbb89f34f1c471c46f677f8c8b7a826b390aee09b9cc4`.
The final download-verification log confirms both wrapper digests. The identical
inner distribution ZIPs are preserved in the downloaded release artifact above.

This is bounded functional, sanitizer, fault and packaging evidence. It does not
establish leak freedom, explain the historical resource-handle delta, resolve
issue #31, qualify 24 hours, or declare production readiness.
