"""Exercise process-first libpq initialization against an explicitly disposable service."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--attempts", type=int, default=20)
    args = parser.parse_args()
    if not 1 <= args.attempts <= 100:
        parser.error("attempts must be 1..100")
    if not all(os.environ.get(name) for name in ("OMP_PG_TEST_SERVICE", "OMP_PG_TEST_BAD_CONNECT")):
        print("SKIP: disposable valid and refused-connection profiles required", flush=True)
        return 77
    env = dict(os.environ, OMP_PG_CONNECTION_ROUNDS="1")
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    for attempt in range(1, args.attempts + 1):
        print(f"Fresh connection process {attempt}/{args.attempts}", flush=True)
        try:
            result = subprocess.run(
                [str(args.binary.resolve())],
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                encoding="utf8",
                errors="replace",
                creationflags=flags,
                timeout=20,
            )
        except subprocess.TimeoutExpired as error:
            output = error.output or b""
            print(output.decode("utf8", errors="replace") if isinstance(output, bytes) else output)
            print("FAIL: fresh-process startup/teardown exceeded 20 seconds", file=sys.stderr)
            return 1
        print(result.stdout, end="", flush=True)
        if result.returncode:
            print(f"FAIL: child exited {result.returncode}; skips are not passes", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
