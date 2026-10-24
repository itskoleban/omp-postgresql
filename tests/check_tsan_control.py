"""Require real race detection; preserve exit/stream evidence for every control."""

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys


def accepted(result, race):
    if race:
        return result.returncode == 66 and "WARNING: ThreadSanitizer: data race" in result.stderr
    return result.returncode == 0 and "TSan startup control entered main" in result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--startup-probe", type=Path)
    parser.add_argument("--runs", type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.runs <= 20:
        parser.error("runs must be 1..20")
    entropy = Path("/proc/sys/vm/mmap_rnd_bits")
    if entropy.is_file():
        print("TSan environment mmap_rnd_bits=" + entropy.read_text().strip(), flush=True)
    failures = 0
    for binary, race in ((args.startup_probe, False), (args.binary, True)):
        if binary is None:
            continue
        print(
            f"TSan input race={int(race)} sha256={hashlib.sha256(binary.read_bytes()).hexdigest()}",
            flush=True,
        )
        for iteration in range(args.runs):
            result = subprocess.run(
                [str(binary)],
                capture_output=True,
                text=True,
                env=dict(os.environ, TSAN_OPTIONS="halt_on_error=1:exitcode=66"),
                timeout=15,
            )
            ok = accepted(result, race)
            print(
                f"TSan control race={int(race)} iteration={iteration+1}/{args.runs} "
                f"returncode={result.returncode} stdout_bytes={len(result.stdout.encode())} "
                f"stderr_bytes={len(result.stderr.encode())} result={'PASS' if ok else 'FAIL'}",
                flush=True,
            )
            sys.stdout.write(result.stdout)
            sys.stdout.flush()
            sys.stderr.write(result.stderr)
            sys.stderr.flush()
            failures += not ok
    if failures:
        raise SystemExit(
            f"TSan control failed {failures} independent executions; no retry converts a failure into PASS"
        )
    print("TSan detector control: intentional race rejected with exit 66")


if __name__ == "__main__":
    main()
