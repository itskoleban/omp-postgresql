# Linux x86 builds, TLS-enabled libpq, and open.mp testing

The component loaded and ran in open.mp 1.5.8.3079 **x86 dynssl** on Debian Bookworm with
GCC 12.2, libpq 17.11, and OpenSSL 3 i386. Current Actions packages are qualified on
Ubuntu 24.04; that is their runtime baseline. The engine is also tested on x64 with
ASan/UBSan and a separate Clang 19 TSan configuration.
The binaries for these two architectures are not interchangeable.

## Toolchain and 32-bit client

On 64-bit Debian/Ubuntu, install the toolchain and i386 OpenSSL with administrative privileges:

```sh
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install g++-multilib libc6-dev-i386 libssl-dev:i386 cmake ninja-build make bison flex perl git
```

Use a compatible libpq 17+ i386 package if the distribution provides one. Otherwise, download
the official sources for a current maintenance release and build only the client:

```sh
bash scripts/build-libpq-linux32.sh /path/to/postgresql-source /path/to/pg-linux32
file /path/to/pg-linux32/lib/libpq.so.5
ldd /path/to/pg-linux32/lib/libpq.so.5
```

The library must be ELF32 and its dependencies must resolve to 32-bit libraries.
The helper uses gcc -m32 and OpenSSL, and installs libpq with the required client headers.
It does not build or install an i386 PostgreSQL server. Root access is unnecessary when the
installation prefix belongs to the current user.

## Component build

```sh
cmake -S . -B build-linux32 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_C_FLAGS=-m32 \
    -DPostgreSQL_LIBRARY=/path/to/pg-linux32/lib/libpq.so \
    -DPostgreSQL_INCLUDE_DIR=/path/to/pg-linux32/include
cmake --build build-linux32 --parallel 4
cmake --install build-linux32 --prefix /path/to/openmp-test
```

If the system has an x64 installation, explicit paths keep CMake from linking the wrong client.
The SDK and Pawn headers are fetched at pinned commits. Install libpq in the system loader's
search path or set LD_LIBRARY_PATH for the server process:

```sh
export LD_LIBRARY_PATH="/path/to/pg-linux32/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

Retain libpq/OpenSSL notices when redistributing their files. Component installation includes
its own notices and those of header dependencies; it does not copy arbitrary host DLLs or
shared libraries.

## Server test

Obtain the official x86 dynssl distribution and compile all .pwn examples with
Pawn 3.10.11, `-E` (warnings as errors), and 32-bit cells. Put initdb, pg_ctl, psql,
openssl, Python, and CTest on PATH.
The PostgreSQL server programs may be x64. Run as a normal user on a native Linux filesystem:

Use `scripts/compile_examples.py` as shown in the [example catalogue](examples.md), with
`--output build`. The interactive accounts mode compiles but needs its external provider
for login; it is not one of the unattended scenarios below.

```sh
python3 tests/run_postgres.py --build build-linux32 \
    --openmp-dir /path/to/openmp-test \
    --component build-linux32/omp-postgresql.so \
    --gamemode build/smoke.amx --unload-script build/unload.amx \
    --callback-gamemode build/callbacks.amx \
    --extension-gamemode build/extensions.amx --bootstrap-gamemode build/bootstrap.amx \
    --section-gamemode build/sections.amx --section-sql examples/sql/accounts.sql \
    --logging-gamemode build/logging.amx \
    --tutorial-dir build --tutorial-sql-dir examples/sql
```

The server directory must be disposable: the harness changes its configuration and gamemode.
Expect four `PG SMOKE PASS` markers, `PG UNLOAD EXIT` without a later unload callback,
`PG CALLBACK PASS`, `PG EXTENSION PASS`, `PG BOOTSTRAP PASS`, `PG SECTION PASS`,
`PG LOGGING PASS`, `PG BASIC PASS`, `PG VALUES PASS`, `PG SAVING PASS`, `PG TRANSFER PASS`, and exit code 0.
Profile and certificate paths belong to the temporary
instance, which is independent of existing databases.

The tested open.mp server encountered a ghc::filesystem error when run directly from a Docker
NTFS bind mount on Windows. Running the disposable distribution on a native Linux filesystem
resolved that issue. Sources may remain mounted for compilation. A mount failure is not a
successful component-loading test.
