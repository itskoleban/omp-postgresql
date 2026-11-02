# Windows resource-handle attribution — November 1, 2026

## Disposition

The historical 109-to-173 residual increase was reproduced as **exactly +64**
in three new stress processes, despite different absolute startup counts.
Three further processes reproduced the same +64 and the same complete type
vector with **direct libpq TLS on a joined worker, without creating an Engine**.
This establishes a dependency/Windows initialization and retention path that
does not require plugin-owned Engine resources. It is not evidence of a
per-Engine leak. An isolated Windows shell call accounts for a substantial
subset of that initialization.

The retained objects remain process-owned until shutdown. In all 15 final
fresh-process controls, the parent waited for exit and successfully queried
zero remaining process handles through its retained process handle. No
unbounded growth was observed in the bounded cycles. These observations do
**not** prove general leak freedom, establish an RSS ceiling, or attribute
each unnamed retained object to a particular allocating DLL/call stack.

No plugin lifecycle defect was established by this increase; changing Engine
cleanup or calling process-global OpenSSL/Winsock cleanup from the component
would therefore be unjustified. This observation does not block an evaluation
RC.3 with the evidence and limits disclosed. Stable-release and long-duration
qualification gates remain separate.

## Controlled experiments

Host: Windows 11 build 26300, MSVC 19.51.36248.0, native Win32 processes,
PostgreSQL/libpq 17.11 and the same local libpq/OpenSSL DLL bytes used by the
historical bounded campaign. SQL ran only in a newly initialized loopback
PostgreSQL 17.11 TLS cluster, stopped after the controls. Production databases
were not used.

The Engine implementation is unchanged from integrated master
`27e09b7cffa34968b78cb0025c121fce0a713e65`; `src/`, `include/` and `pawn/`
have no diff. Experimental stress instrumentation adds native handle snapshots
at the existing resource checkpoints and one steady-state checkpoint. It
does not change workload assertions, queues, SQL or lifetimes. The repository
stress source was restored after the experiment. Release metadata was being
prepared separately; these local binaries are diagnostic evidence, not an
exact-clean-source RC distribution.

The reusable `pg_windows_resource_probe` test diagnostic enumerates
`ProcessHandleInformation`, queries object types/names, records loaded modules
and independently compares `GetProcessHandleCount`. Four initial observer
snapshots warm the instrumentation before the baseline. Module snapshot handles
are closed before the next measurement. Each control uses the same linked
binary, DLLs and regular-file stdout/stderr redirection, a fresh process, one
operation followed by ten complete repeated cycles, and a settled checkpoint.
Native API errors fail the diagnostic rather than silently accepting an
incomplete snapshot. Unsupported Windows native APIs are a diagnostic limitation.

| Experiment | Warm baseline | First active | Destroyed | After ten more cycles | Fresh processes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Stress load, then lifecycle checks | 113 / 105 / 105 | 193 / 185 / 185 | 177 / 169 / 169 | unchanged after lifecycle | 3 |
| Observer only | 113 | 113 | 113 | 113 | 1 |
| Empty Engine | 105 | 105 | 105 | 105 | 1 |
| Four C++ threads | 105 | 109 | 105 | 105 | 1 |
| Direct libpq, plaintext on main thread | 105 | 164 | 163 | 163 | 3 repeated controls |
| Direct libpq, TLS on main thread | 105 | 172 | 171 | 171 | 3 repeated controls |
| Four-lane Engine TLS | 105 | 185 | 169 | 169 | 1 |
| Direct libpq TLS, joined worker | 105 | 173 | 169 | 169 | 3 |
| Windows shell folder lookup only | 105 | 153 | 153 | 153 | 3 |
| Winsock startup / cleanup only | 105 | 110 | 106 | 106 | 3 |

Each of the three stress runs passed 7,263 existing semantic/lifecycle checks.
Their original +80 active delta reduces by 16 at Engine destruction: four File,
four Event and eight Thread handles. All worker Thread handles disappear from
the stable post-destruction snapshot. Native snapshot counts agree with the
independent process-count API. Absolute counts differ between launches, so
unrelated process totals are never subtracted to assign ownership.

The direct TLS worker reproduces this exact residual vector:

| Native object type | Residual increase |
| --- | ---: |
| EtwRegistration | 41 |
| Event | 11 |
| Key | 9 |
| File | 1 |
| IoCompletion | 1 |
| Section | 1 |
| **Total** | **64** |

The checked-in diagnostic with its strengthened native-size/string validation
also passed 12 fresh-process checks (three each: observer, shell, direct TLS
worker and Engine). The delivered Python supervisor then passed all four modes
once against the CMake-built executable. Both batches confirmed the same deltas,
stable repeated cycles and successful zero post-exit handle queries. Five
supervisor regressions cover child timeout/termination, failed or denied
observation, supervisor exceptions and unreadable log hashing.

The shell-only delta is +48: 32 EtwRegistration, eight Event, seven Key and one
Section. Retained named objects include Winsock catalog/OLE/Explorer/locale
registry keys and `windows_shell_global_counters`. Newly loaded Windows modules
include shell/storage/COM/crypto/socket support. This is stronger than an
aggregate count, but it does not justify assigning every unnamed object by
subtracting experiments. Main-thread direct libpq retains two additional handles
relative to the joined-worker control; thread lifetime is therefore part of a
comparable measurement boundary.

## Source-supported mechanism and attribution limit

The inspected PostgreSQL 17.11 `src/interfaces/libpq/fe-connect.c` calls
`pqGetHomeDirectory` for default password-file lookup and implements its Windows
branch using `SHGetFolderPath(..., CSIDL_APPDATA, ...)`. The TLS setup in
`fe-secure-openssl.c` also requests that directory when relevant certificate
parameters default. The shell-only control calls this Windows API without libpq
connection/TLS or Engine operations (all modes share a version lookup) and
demonstrates its initialization cost. In
`pqMakeEmptyPGconn`, libpq explicitly initializes process-wide Winsock and
deliberately omits matching `WSACleanup`; connection destruction is not process
teardown. These source paths and matched controls explain why comparing cold
startup to post-Engine destruction is not a plugin-only ownership test.

Attempted own-process allocation-stack tracing could enable tracing, but its
query returned `0xc0000022 (STATUS_ACCESS_DENIED)` in both x86 native and x64
Python controls. No allocation stacks were returned. No global tracing, privilege
change or external-process trace was attempted. Thus the exact allocator stack
of each unnamed object remains unknown; this limit is preserved rather than
claiming complete DLL-level attribution.

The prior standalone OpenSSL-load control (+28) remains a different process and
experiment. It is not subtracted from +64 and was never sufficient to explain
the historical result. Nothing here diagnoses the unrelated network timeout
in issue #31.

## Reproduction and preservation

Build the Windows test diagnostic with the normal CMake test build, then run
it only against an explicitly disposable service selected with
`OMP_PG_TEST_SERVICE` and `PGSERVICEFILE`:

```powershell
cmake --build build --config Release --target pg_windows_resource_probe
build/Release/pg_windows_resource_probe.exe observer
build/Release/pg_windows_resource_probe.exe empty-engine
build/Release/pg_windows_resource_probe.exe thread
build/Release/pg_windows_resource_probe.exe shell
build/Release/pg_windows_resource_probe.exe winsock
build/Release/pg_windows_resource_probe.exe libpq-plain
build/Release/pg_windows_resource_probe.exe libpq-tls
build/Release/pg_windows_resource_probe.exe libpq-thread
build/Release/pg_windows_resource_probe.exe engine
```

For fresh processes, bounded child supervision, per-log hashes, retained failures
and parent-side post-exit handle queries, use the delivered supervisor instead:

```powershell
python scripts/probe_windows_resources.py --binary build/Release/pg_windows_resource_probe.exe --output build-resource-evidence
```

It runs observer/shell/direct-TLS-worker/Engine three times each by default;
`--mode` selects explicit alternatives and `--repetitions` accepts 1..10. It
requires a new or empty evidence directory. It does not create, modify or stop
the caller's database fixture; fixture ownership and cleanup remain explicit.

Use identical launch/redirection boundaries, retain every failure, compare
within-process warmed baselines, and wait for complete child exit. Counts are
recorded as observations; no universal count threshold or automatic leak-free
verdict is imposed. The tool validates its SELECT result and requested TLS mode.
It neither replays uncertain SQL nor changes the component's public API.

Original stress logs, both control batches, their binary/dependency/source hash
manifests, shutdown results, experimental sources and the tracing-denial capture
were preserved locally in the task's user-facing evidence output. The initial
six-mode batch and final 15-process batch are distinct observations, not 21
independent confirmations of every mode. A future long-duration campaign must
monitor warmed descriptor/handle trends alongside ownership counters, semantic
invariants and timely cleanup; these finite observations do not replace it.
