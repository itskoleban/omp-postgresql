"""Independent cold-vptr EMFILE comparison. Negative results remain failures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    records = []
    failed = False
    for index, compiler in enumerate(args.compiler):
        version = subprocess.check_output([compiler, "--version"], text=True)
        factory = output / f"factory-{index}.o"
        common = [compiler, "-std=c++20", "-g", "-O0", "-fno-pie"]
        subprocess.run(common + ["-c", str(root/"tests/sanitizer_descriptor_factory.cpp"), "-o", str(factory)], check=True)
        for instrument in (False, True):
            binary = output / f"control-{index}-{int(instrument)}"
            command = common + ["-pthread", "-no-pie"]
            if instrument:
                command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
            command += [str(root/"tests/sanitizer_descriptor_control.cpp"), str(factory), "-o", str(binary)]
            subprocess.run(command, check=True)
            result = subprocess.run([str(binary)], capture_output=True, timeout=20,
                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                         UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
            log = result.stdout + result.stderr
            name = binary.name + ".log"
            (output/name).write_bytes(log)
            records.append({"compiler":version, "instrumented":instrument,
                            "command":command, "returncode":result.returncode,
                            "actual_emfile":b"CONTROL reached actual EMFILE" in log,
                            "status":"PASS" if result.returncode == 0 else "FAIL",
                            "log":name, "sha256":hashlib.sha256(log).hexdigest()})
            failed |= result.returncode != 0
    manifest = {"source_commit":subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
                "sources":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in
                           (root/"tests/sanitizer_descriptor_control.cpp",root/"tests/sanitizer_descriptor_factory.cpp")},
                "results":records}
    (output/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf8")
    for row in records:
        print(row["compiler"].splitlines()[0], "instrumented="+str(row["instrumented"]), row["status"], "exit="+str(row["returncode"]))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
