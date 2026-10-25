# Bounded detector reproduction

Use a disposable Linux x64 environment with Ubuntu Clang 19.1.1 and GDB 15.1.
No PostgreSQL component or database is needed for this detector experiment.
Do not change a host ASLR setting. The local Docker startup limitation and narrowly
permitted personality syscall are explained in the investigation report.

The original fixture comes from immutable source:

```sh
git show d0494d2bf45c03757864af0f5dfa2c7b868f4810:tests/tsan_race_control.cpp > original.cpp
clang++-19 -g -fsanitize=thread -g -O1 -std=c++20 -c original.cpp -o original.o
clang++-19 -g -fsanitize=thread original.o -o original
sha256sum original original.o
objdump -d --disassemble=__tsan_write4 original
```

The committed debugger scripts are forensic captures bound to the evaluated x86-64
runtime archive SHA-256
`bf92e52f36539006b2d53ea6f0587d7bbddc9b7119f42bb47b6a4d322af8b63f`.
Before executing either script, verify the linked `__tsan_write4` has the vector
shadow load at offset `0x46` and its next instruction at `0x4c`, with the same register
roles shown in the recorded disassembly. Stop if the runtime or prologue differs;
these offsets are not a portable debugging API. Use a disposable output directory
matching the captured `/investigation/fixture-experiments` path or make a separate
working copy of the script with only that logging path changed and record its hash.

```sh
gdb -q -batch -x shadow_collision.gdb ./original > original-debugger-console.log 2>&1
```

The negative observation requires two distinct worker callbacks for the same value,
the same old shadow snapshot, normal inferior exit 0 and no real race diagnostic.
A debugger error or timeout does not qualify. The recorded original console satisfies
those requirements; the GitHub master failure remains a separate historical result.

Build the maintained fixture normally with CMake and run:

```sh
gdb -q -batch -x handoff_snapshot.gdb /absolute/build/pg_tsan_race_control > handoff-debugger-console.log 2>&1
```

The positive observation requires phase 0 at the first callback, phase 1 and the first
worker's access in the second snapshot, an actual full TSan data-race report and an
inferior exit event of 66. The script records the inferior exit separately from GDB's
own exit. Both scripts explicitly leave GDB's randomization disabling turned off.

Ordinary bounded qualification uses the maintained CTest controls. Every race attempt
must pass independently; the startup and release/acquire controls each execute twenty
times and must complete normally. The final CI artifact additionally retains actual
fixture binaries, objects, sources, commands, runtime identities and disassembly.
