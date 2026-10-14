# Experimental package procedure

The existing CI x86 jobs build once, run the core and server tests, stage the CMake
`omp-postgresql` installation component and assemble a deterministic ZIP. No x64 server
packages are advertised. `workflow_dispatch` also permits a manual build after the workflow
is integrated into the default branch. Repository permissions remain `contents: read`.

`scripts/package.py create` requires a clean tracked checkout, a Release build whose
generated metadata matches HEAD, the expected x86 binary and explicit libpq/license
paths. It inspects every included PE/ELF runtime, rejects unexpected dependency imports,
and includes only required non-system Windows DLLs. Linux distributes libpq alone;
system loader dependencies and symbol-version requirements are inventoried. Staging
starts in a new temporary directory; developer files, logs and credentials are never
copied by a recursive repository sweep.

The name is `omp-postgresql-0.1.0-experimental.COMMIT-PLATFORM-x86.zip`. It contains a
single `omp-postgresql/` directory, public include, component, docs, examples, legal
notices, selected runtime files and `manifest.json`. The manifest records full commit,
Release configuration, compiler, libpq header version, pinned SDK/Pawn revisions and
SHA-256 of each payload file. External Windows CRT and Linux system libraries are listed
explicitly. Their absence from the archive is deliberate.

ZIP entries are sorted, timestamps use the source commit time (UTC), permissions are
normalized and compression is fixed. Two assemblies with identical inputs must have
identical SHA-256; this is checked before upload. Compiler/toolchain updates can still
change input binary bytes. The procedure does not claim hermetic reproducible compilation.

`scripts/package.py verify` checks the archive's checksum, safe paths, no duplicate/symlink
entries, exact payload inventory, required files, hashes, binary architecture and version/
commit/name consistency **before** extraction into a previously nonexistent directory.
The x86 jobs compile all examples using the extracted include, unpack a fresh official
open.mp server, clear development DLL/loader search paths, and rerun all server scenarios
using the extracted component and runtime. Only successfully tested ZIPs are uploaded.
Logs are retained even when a test fails; a failed package is not a release artifact.

For a local Windows run, pass `--platform windows`, `--build build-msvc32`,
`--libpq C:/deps/pg32/bin/libpq.dll`, `--runtime-dir C:/msys64/mingw32/bin`,
`--postgres-license C:/deps/postgresql-source/COPYRIGHT`,
`--openssl-license C:/msys64/mingw32/share/licenses/openssl/LICENSE`,
`--pthread-license C:/msys64/mingw32/share/licenses/libwinpthread/COPYING`, and
`--objdump C:/msys64/mingw32/bin/objdump.exe`. Linux uses `--platform linux`, the x86
build, `--libpq /path/to/libpq.so.5` and the PostgreSQL `COPYRIGHT`; `readelf` and `ldd`
must be available. Both accept `--output DIRECTORY` and verification accepts
`--archive NAME.zip --destination NEW_DIRECTORY`.

Release publication is a later gate: no tag, merge, Release or write permission is added
by this packaging change. Version 0.1.0 remains experimental until all qualification
and independent-audit requirements are met.
