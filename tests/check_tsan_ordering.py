"""Require the instrumented, happens-before ordered fixture to remain race-free."""

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys

from check_tsan_control import accepted, partial_text


EXPECTED_STDOUT = (
    "TSan race control workers_ready=2\n"
    "TSan race control workers_completed=2\n"
)


def accepted_ordered(result):
    return (
        result.returncode == 0
        and result.stdout == EXPECTED_STDOUT
        and result.stderr == ""
        and not accepted(result, True)
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--runs", type=int, default=20)
    args = parser.parse_args()
    if not 1 <= args.runs <= 20:
        parser.error("runs must be 1..20")
    identity = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    failures = 0
    for iteration in range(args.runs):
        outcome = "completed"
        try:
            result = subprocess.run(
                [str(args.binary)],
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
                [str(args.binary)], None, partial_text(error.stdout), partial_text(error.stderr)
            )
        except OSError as error:
            outcome = f"LAUNCH_ERROR errno={error.errno}"
            result = subprocess.CompletedProcess([str(args.binary)], None, "", "")
        ok = accepted_ordered(result)
        print(
            f"TSan ordering control iteration={iteration+1}/{args.runs} sha256={identity} "
            f"returncode={result.returncode if result.returncode is not None else outcome} "
            f"stdout_bytes={len(result.stdout.encode())} stderr_bytes={len(result.stderr.encode())} "
            f"race_accepted={int(accepted(result, True))} result={'PASS' if ok else 'FAIL'}",
            flush=True,
        )
        sys.stdout.write(result.stdout)
        sys.stdout.flush()
        sys.stderr.write(result.stderr)
        sys.stderr.flush()
        failures += not ok
    if failures:
        raise SystemExit(
            f"TSan ordering control failed {failures} independent executions; no retry converts a failure into PASS"
        )
    print("TSan ordering control: all required executions completed without a race diagnostic")


if __name__ == "__main__":
    main()
