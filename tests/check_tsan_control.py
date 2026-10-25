"""Require real race detection; preserve exit/stream evidence for every control."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys


def accepted(result, race):
    if re.search(r"(?:FATAL: ThreadSanitizer|ThreadSanitizer:DEADLYSIGNAL)", result.stderr):
        return False
    if race:
        if result.returncode != 66:
            return False
        # A fixture printing only the warning token must not qualify. Runtime/source
        # identities are verified separately; text parsing cannot authenticate an
        # arbitrary program that deliberately forges a complete sanitizer report.
        report = re.search(
            r"^WARNING: ThreadSanitizer: data race \(pid=\d+\)\n"
            r"(?P<body>.*?)^SUMMARY: ThreadSanitizer: data race .*tsan_race_control\.cpp:\d+",
            result.stderr,
            re.MULTILINE | re.DOTALL,
        )
        if report is None:
            return False
        accesses = re.findall(
            r"^  (?:Previous )?(?P<kind>[Ww]rite|[Rr]ead) of size \d+ at "
            r"(?P<address>0x[0-9a-f]+) by thread (?P<thread>T\d+):\n"
            r"(?P<stack>(?:    #\d+[^\n]*\n)+)",
            report.group("body"),
            re.MULTILINE,
        )
        if len(accesses) != 2:
            return False
        first, second = accesses
        return (
            first[1] == second[1]
            and first[2] != second[2]
            and any(access[0].lower() == "write" for access in accesses)
            and all("tsan_race_control.cpp:" in access[3] for access in accesses)
        )
    return (
        result.returncode == 0
        and result.stdout.strip() == "TSan startup control entered main"
        and "ThreadSanitizer:" not in result.stderr
    )


def partial_text(stream):
    # TimeoutExpired exposes bytes even when subprocess.run was asked for text.
    return stream.decode("utf-8", errors="backslashreplace") if isinstance(stream, bytes) else stream or ""


def mmap_entropy():
    try:
        return Path("/proc/sys/vm/mmap_rnd_bits").read_text().strip()
    except OSError as error:
        return f"UNAVAILABLE errno={error.errno}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--startup-probe", type=Path)
    parser.add_argument("--runs", type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.runs <= 20:
        parser.error("runs must be 1..20")
    print("TSan environment mmap_rnd_bits=" + mmap_entropy(), flush=True)
    failures = 0
    for binary, race in ((args.startup_probe, False), (args.binary, True)):
        if binary is None:
            continue
        print(
            f"TSan input race={int(race)} sha256={hashlib.sha256(binary.read_bytes()).hexdigest()}",
            flush=True,
        )
        for iteration in range(args.runs):
            outcome = "completed"
            try:
                result = subprocess.run(
                    [str(binary)],
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="backslashreplace",
                    env=dict(os.environ, TSAN_OPTIONS="halt_on_error=1:exitcode=66"),
                    timeout=15,
                )
            except subprocess.TimeoutExpired as error:
                outcome = "TIMEOUT"
                result = subprocess.CompletedProcess(
                    [str(binary)], None, partial_text(error.stdout), partial_text(error.stderr)
                )
            except OSError as error:
                outcome = f"LAUNCH_ERROR errno={error.errno}"
                result = subprocess.CompletedProcess([str(binary)], None, "", "")
            ok = accepted(result, race)
            print(
                f"TSan control race={int(race)} iteration={iteration+1}/{args.runs} "
                f"returncode={result.returncode if result.returncode is not None else outcome} "
                f"stdout_bytes={len(result.stdout.encode())} "
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
