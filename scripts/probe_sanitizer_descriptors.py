"""Independent cold-vptr EMFILE comparison. Negative results remain failures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tests"))
from source_provenance import capture
from real_session_evidence import publish as publish_copies, verify as verify_copies

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ("tests/sanitizer_descriptor_control.cpp", "tests/sanitizer_descriptor_factory.cpp",
          "tests/sanitizer_descriptor_control.hpp", "scripts/probe_sanitizer_descriptors.py")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


def outcome(returncode, log):
    reached = b"CONTROL reached actual EMFILE" in log
    diagnostic = any(marker in log for marker in (b"runtime error:", b"ERROR: AddressSanitizer", b"CHECK failed"))
    complete = b"CONTROL completed with no component code linked" in log
    return {"actual_emfile":reached, "diagnostics_present":diagnostic,
            "status":"PASS" if returncode == 0 and reached and complete and not diagnostic else "FAIL"}


def verify(directory):
    directory = Path(directory)
    manifest = json.loads((directory/"manifest.json").read_text(encoding="utf8"))
    legacy = "schema" not in manifest
    expected_records = 4 if legacy else manifest["expected_records"]
    if (not legacy and manifest["schema"] != 2) or len(manifest["results"]) != expected_records:
        raise ValueError("Descriptor evidence inventory mismatch")
    ledger = {}
    if (directory/"sanitization.json").is_file():
        verify_copies(directory)
        ledger = {row["file"]:row for row in json.loads((directory/"sha256.json").read_bytes())}
    def original_digest(name):
        if ledger:
            return ledger[name]["original_sha256"]
        return digest((directory/name).read_bytes())
    logs = {row["log"]:row["sha256"] for row in manifest["results"]} if legacy else manifest["logs"]
    for name, expected in logs.items():
        path = (directory/name).resolve()
        if not path.is_relative_to(directory.resolve()) or original_digest(name) != expected:
            raise ValueError("Descriptor log identity mismatch")
    for row in manifest["results"]:
        log = (directory/row["log"]).read_bytes()
        if original_digest(row["log"]) != row["sha256"] or any(row[key] != value for key,value in outcome(row["returncode"],log).items() if key in row):
            raise ValueError("Descriptor result or observed EMFILE changed")
        if not row["compiler"] or (not legacy and not row["factory_command"]) or not row["command"]:
            raise ValueError("Descriptor compiler/build provenance missing")
        if row["instrumented"] and not {"-fsanitize=address,undefined", "-fno-sanitize-recover=all"} <= set(row["command"]):
            raise ValueError("Descriptor sanitizer flags were weakened")
        if any(flag.startswith("-fno-sanitize=") for flag in row["command"]) or any("suppressions=" in value for value in row.get("environment",{}).values()):
            raise ValueError("Descriptor sanitizer checks must not be suppressed")
    return manifest


def publish(directory, destination):
    directory = Path(directory)
    manifest = verify(directory)
    paths = [directory/"manifest.json", *sorted(directory.glob("*.log"))]
    ledger = [{"file":path.name,"bytes":path.stat().st_size,"sha256":digest(path.read_bytes())} for path in paths]
    original = directory/"source-sha256.json"
    original.write_bytes((json.dumps(ledger,indent=2)+"\n").encode())
    publish_copies(directory,destination,original_commit=manifest["source_commit"],original_ledger=original)
    verify(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", action="append", default=[])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--instrument", choices=("OFF", "ON", "BOTH"), default="BOTH")
    parser.add_argument("--verify", type=Path, help="Verify identities only; does not relabel FAIL results")
    parser.add_argument("--publish", type=Path, help="Publish verified, sanitized descriptor copies")
    parser.add_argument("--destination", type=Path)
    args = parser.parse_args()
    if args.publish:
        if not args.destination:
            parser.error("--publish requires --destination")
        publish(args.publish,args.destination)
        print("Published descriptor evidence; original identities and FAIL outcomes retained")
        return 0
    if args.verify:
        manifest = verify(args.verify)
        print("Verified descriptor evidence integrity; outcomes retained:", [r["status"] for r in manifest["results"]])
        return 0
    if not args.compiler or not args.output:
        parser.error("--compiler and --output are required for execution")
    root = ROOT
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    modes = (False, True) if args.instrument == "BOTH" else (args.instrument == "ON",)
    source = capture(root)
    manifest = {"schema":2, "run_id":uuid.uuid4().hex, "started_utc":utc(),
                "source_commit":source["source_commit"], "source_provenance":source,
                "sources":{name:digest((root/name).read_bytes()) for name in INPUTS},
                "github_run_id":os.environ.get("GITHUB_RUN_ID"), "github_run_attempt":os.environ.get("GITHUB_RUN_ATTEMPT"),
                "expected_records":len(args.compiler)*len(modes), "logs":{}, "results":[]}
    records = []
    failed = False
    def record_log(name, data):
        (output/name).write_bytes(data)
        manifest["logs"][name] = digest(data)
    def build(command, name):
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        record_log(name,result.stdout)
        save()
        result.check_returncode()
    def save():
        manifest["results"] = records
        (output/"manifest.json").write_bytes((json.dumps(manifest,indent=2)+"\n").encode())
    save()
    for index, compiler in enumerate(args.compiler):
        version_bytes = subprocess.check_output([compiler, "--version"])
        record_log(f"compiler-{index}-version.log",version_bytes)
        version = version_bytes.decode("utf8")
        factory = output / f"factory-{index}.o"
        common = [compiler, "-std=c++20", "-g", "-O0", "-fno-pie"]
        factory_command = common + ["-c", str(root/"tests/sanitizer_descriptor_factory.cpp"), "-o", str(factory)]
        build(factory_command,f"factory-{index}-build.log")
        for instrument in modes:
            binary = output / f"control-{index}-{int(instrument)}"
            command = common + ["-pthread", "-no-pie"]
            if instrument:
                command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
            command += [str(root/"tests/sanitizer_descriptor_control.cpp"), str(factory), "-o", str(binary)]
            build(command,binary.name+"-build.log")
            started = utc()
            result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20,
                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                         UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
            log = result.stdout
            name = binary.name + ".log"
            record_log(name,log)
            records.append({"compiler":version, "instrumented":instrument,
                            "version_command":[compiler,"--version"], "factory_command":factory_command,
                            "command":command, "run_command":[str(binary)], "returncode":result.returncode,
                            "started_utc":started, "finished_utc":utc(),
                            "output_capture":"merged-stdout-stderr",
                            "environment":{"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1", "UBSAN_OPTIONS":"halt_on_error=1:print_stacktrace=1"},
                            "binary_sha256":digest(binary.read_bytes()), "factory_sha256":digest(factory.read_bytes()),
                            **outcome(result.returncode,log), "log":name, "sha256":digest(log)})
            failed |= records[-1]["status"] != "PASS"
            save()
    manifest["finished_utc"] = utc()
    manifest["inputs_unchanged"] = all(digest((root/name).read_bytes())==expected for name,expected in manifest["sources"].items())
    save()
    if not manifest["inputs_unchanged"]:
        raise RuntimeError("Descriptor source inputs changed during execution; evidence retained")
    verify(output)
    for row in records:
        print(row["compiler"].splitlines()[0], "instrumented="+str(row["instrumented"]), row["status"], "exit="+str(row["returncode"]))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
