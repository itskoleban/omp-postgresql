# Windows x86 builds and libpq distribution

The tested open.mp 1.5.8.3079 distribution is x86. The component loaded and ran with MSVC
on that architecture. The engine also supports x64 builds for standalone testing; an x64
DLL cannot be installed into that x86 server.

## MSVC build

Install Visual Studio with the C++ tools and Windows SDK. Supply TLS-enabled libpq 17+
for x86, its headers, and an MSVC import library named `libpq.lib`:

```powershell
cmake -S . -B build-win32 -G "Visual Studio 18 2026" -A Win32 -DPostgreSQL_ROOT=C:/deps/pg32
cmake --build build-win32 --config Release --parallel 4
cmake --install build-win32 --config Release --prefix C:/openmp-test
```

For Visual Studio 2022, select its corresponding generator. If detection finds an x64
PostgreSQL installation, set `PostgreSQL_INCLUDE_DIR=C:/deps/pg32/include` and
`PostgreSQL_LIBRARY=C:/deps/pg32/lib/libpq.lib` explicitly. Resolve this ambiguity before linking.

## Building the x86 client when a suitable package is unavailable

PostgreSQL supports building x86 libpq even when standard Windows server installers are x64.
There is no need to build or install an x86 database server. Download a current maintenance
release of PostgreSQL 17+ from the [official source archive](https://www.postgresql.org/ftp/source/).
Use updated x86 OpenSSL built for the same toolchain. The helpers reproduce the locally
verified build path:

```sh
# In MSYS2, put /mingw32/bin before /usr/bin. Requires i686 GCC and OpenSSL,
# make, bison, flex, perl, and git. Extract the official PostgreSQL sources first.
export PATH=/mingw32/bin:/usr/bin:$PATH
bash scripts/build-libpq-mingw32.sh /c/deps/postgresql-source /c/deps/pg32
```

Then generate the import library with `lib.exe` from the MSVC x86 toolchain:

```powershell
./scripts/create-msvc-import.ps1 `
    -PostgreSQLRoot C:/deps/pg32 `
    -LibTool "C:/path/to/MSVC/bin/Hostx64/x86/lib.exe"
```

The helper applies the checked-in Windows libpq mutex correction with exact-context
validation before compiling. Unknown source layouts fail instead of silently omitting the
fix. The rationale is in `scripts/patches/README.md` in the source checkout; installed
packages include it under `share/omp-postgresql/patches/`.
For package assembly, record the dependency immediately after building it:

```powershell
python scripts/package.py record-libpq --libpq C:/deps/pg32/bin/libpq.dll --source C:/deps/postgresql-source
```

This checks the applied source patch and writes `libpq-build-info.json` beside the DLL.
Packaging rejects a missing record or mismatched DLL, patch or source/header version.
Configure with `-DPG_LIBPQ_SOURCE_DIR=C:/deps/postgresql-source` on Windows to compile the
deterministic mutex regression against that source. CI enables this for its x86 DLL.

The component uses only libpq's C API. libpq releases all of its opaque objects.
The import library allows MSVC to link that C DLL; no MinGW C++ objects are mixed with MSVC.
Build the Windows component for the server's ABI and verify actual loading before publishing.

## Server package

The package consists of `components/omp-postgresql.dll`, the Pawn include, and documentation.
Place `libpq.dll`, `libssl-3.dll`, `libcrypto-3.dll`, and any other dependencies of the chosen
build where the server's Windows loader can find them. Inspect dependencies with dumpbin
or objdump; do not substitute a DLL of another architecture. Install the supported MSVC x86
Redistributable separately; retain all redistributed libraries' notices. See the actual
[package dependency policy](../DEPENDENCIES.md). The project links libpq dynamically and
does not automatically package arbitrary DLLs from the development machine.

## Reproducible server test

Download a disposable open.mp distribution and compile all six examples with `-E`
(warnings as errors) and 32-bit Pawn cells. The runner starts disposable PostgreSQL
with TLS, runs CTest, and launches open.mp on loopback only. It repeats GMX three times,
unloads a filterscript with a query in flight, and verifies callback state restoration:

```powershell
python tests/run_postgres.py `
    --build build-win32 `
    --postgres-bin C:/deps/server-pg/bin `
    --openmp-dir C:/openmp-test `
    --component build-win32/Release/omp-postgresql.dll `
    --gamemode build/smoke.amx `
    --unload-script build/unload.amx `
    --callback-gamemode build/callbacks.amx `
    --extension-gamemode build/extensions.amx `
    --bootstrap-gamemode build/bootstrap.amx `
    --section-gamemode build/sections.amx --section-sql examples/sql/accounts.sql `
    --runtime C:/deps/pg32/bin/libpq.dll `
    --runtime C:/deps/openssl32/libssl-3.dll `
    --runtime C:/deps/openssl32/libcrypto-3.dll
```

`--openmp-dir` must be a disposable copy: the harness changes its network, gamemode, and
script configuration. Never point it at a production server directory. A successful run
includes four `PG SMOKE PASS` markers, `PG UNLOAD EXIT` without `PG UNLOAD CALLBACK`,
`PG CALLBACK PASS`, `PG EXTENSION PASS`, `PG BOOTSTRAP PASS`, `PG SECTION PASS`, and clean shutdowns.
