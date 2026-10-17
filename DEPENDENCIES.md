# Runtime dependencies

The experimental x86 Actions packages link libpq dynamically. They are not standalone
server distributions. `manifest.json` records the actual binary imports, architecture,
compiler, libpq headers, source commit and every payload checksum. Only x86 open.mp
1.5.8.3079 is currently qualified; x64 engine test builds are not distributed components.

## Windows x86

The package places `libpq.dll` and the recursively required non-system DLLs beside the
server executable. CI builds libpq 17.11 from its checksum-verified official source with
MSYS2 i686 OpenSSL 3. It copies only required OpenSSL/MinGW runtime DLLs from the explicitly
selected directory, checks PE machine type and includes their licenses. The component
itself is built with MSVC; libpq's opaque C objects remain owned by libpq.

The Windows source build applies a downstream mutex-initialization correction. Its patch
and rationale are included under `share/omp-postgresql/patches/`; the package manifest
binds the patch SHA-256 and source version to the actual libpq DLL SHA-256. This is a
project-maintained correction, not an upstream-released patch. The race and remaining
qualification limits are recorded in [validation](docs/validation.md). Externally supplied
Windows libpq builds need equivalent verification; a newer version number alone is not proof.

Install Microsoft's supported **Visual C++ v14 Redistributable (x86)** separately, including
on 64-bit Windows. The Universal CRT and Windows system DLLs are operating-system
dependencies and are not copied from the build machine. Inspect the manifest for the exact
external imports. Do not substitute x64 DLLs or copy DLLs into Windows system directories.

## Linux x86

The package includes the TLS-enabled libpq 17.11 client as `lib/libpq.so.5`, with the
PostgreSQL license. Set `LD_LIBRARY_PATH` to this package's absolute `lib` directory before
starting open.mp. OpenSSL 3, glibc, libstdc++ and libgcc remain system dependencies and
must be installed for **i386**, even on an x64 host. CI inspects DT_NEEDED, ELF machine
types, version requirements and loader resolution. Its Ubuntu 24.04 environment is the
tested runtime baseline, not a claim of compatibility with every older distribution.

On Ubuntu 24.04, the runtime packages include `libssl3t64:i386`, `libstdc++6:i386`,
`libgcc-s1:i386` and `libc6:i386`. Enable i386 with `dpkg --add-architecture i386` and use
your distribution's maintained packages. See the exact GLIBC/GLIBCXX requirements in the
manifest; a matching library name alone does not establish ABI compatibility.

## Notices and maintenance

Project code is MIT. Header dependencies retain their licenses under
`share/omp-postgresql/licenses/`; redistributed runtime licenses are under `licenses/`.
OpenSSL is Apache-2.0; libpq uses the PostgreSQL License. If a MinGW runtime is required,
its corresponding notice is included. System runtimes installed separately retain their
providers' terms. See [NOTICE.md](NOTICE.md).

Update and requalify libpq/OpenSSL regularly. The runner's build tools and MSYS2 packages
can change: deterministic archive assembly from identical inputs does **not** establish
bit-for-bit reproducible compilation across toolchain updates. A future RC must freeze
and review its final dependency inventory and independent audit.
