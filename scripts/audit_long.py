"""External dedicated-runner entry point. No production connection argument exists.

Configure/build first with PG_STRESS_SECONDS equal to --seconds (120..86400).
Requires the existing run_postgres.py tools; creates its own loopback TLS cluster.
Keeps per-stage logs and a manifest even on timeout/failure. Never publishes anything.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import signal
import shutil
import platform
from qualification_metrics import analyze


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--seconds", type=int, default=86400)
    parser.add_argument("--seed", type=int, default=1592594996)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--postgres-bin")
    args = parser.parse_args()
    if not 120 <= args.seconds <= 86400 or not 1 <= args.seed <= 4294967295:
        parser.error("seconds must be 120..86400; seed must be 1..4294967295")
    root = Path(__file__).resolve().parents[1]
    build = Path(args.build).resolve()
    cache = (build / "CMakeCache.txt").read_text(encoding="utf8")
    if f"PG_STRESS_SECONDS:STRING={args.seconds}\n" not in cache:
        parser.error("Build PG_STRESS_SECONDS must equal requested duration; refusing a short false pass")
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env["OMP_PG_STRESS_SEED"] = str(args.seed)
    env["OMP_PG_FUZZ_SEED"] = str(args.seed)
    # The runner and all child processes inherit local temporary storage.
    temporary = output / "temporary"
    temporary.mkdir()
    env["TEMP"] = env["TMP"] = env["TMPDIR"] = str(temporary)
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True)
    manifest = {"commit": sha, "dirty": dirty, "seconds": args.seconds, "seed": args.seed,
                "started_unix": time.time(), "stages": [], "status": "RUNNING"}
    manifest["platform"] = platform.platform()
    manifest["python"] = sys.version
    manifest["tool_versions"] = {}
    for tool in (["cmake", "--version"], ["pg_config", "--version"],
                 ["postgres", "--version"], ["openssl", "version"]):
        result = subprocess.run(tool, capture_output=True, text=True, env=env,
                                creationflags=flags, timeout=30)
        manifest["tool_versions"][" ".join(tool)] = {
            "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
    for info in build.glob("package-build-info-*.json"):
        manifest.setdefault("build_info", {})[info.name] = json.loads(info.read_text(encoding="utf8"))
    fuzz = build / ("pg_audit_fuzz.exe" if os.name == "nt" else "pg_audit_fuzz")
    if not fuzz.is_file():
        fuzz = build / args.config / fuzz.name
    def save():
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf8")
    save()
    command = [sys.executable, "tests/run_postgres.py", "--build", str(build),
               "--config", args.config, "--doc-sql"]
    if args.postgres_bin:
        command += ["--postgres-bin", str(Path(args.postgres_bin).resolve())]
    try:
        for name, argv, budget in [
            ("integration-soak", command, args.seconds + 900),
            ("extended-fuzz", [str(fuzz)], 600),
        ]:
            stage = {"name": name, "command": argv, "started_unix": time.time()}
            manifest["stages"].append(stage)
            save()
            if name == "extended-fuzz":
                env["OMP_PG_FUZZ_CASES"] = "100000"
            with (output / f"{name}.log").open("wb") as log:
                process = subprocess.Popen(
                    argv, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                    creationflags=flags | (subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0),
                    start_new_session=os.name != "nt",
                )
                try:
                    returncode = process.wait(timeout=budget)
                except BaseException as error:
                    # Kill only this newly launched process tree, never a server discovered globally.
                    if os.name == "nt":
                        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                       stdout=log, stderr=subprocess.STDOUT, timeout=30,
                                       creationflags=flags)
                    else:
                        os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=30)
                    stage.update(returncode=process.returncode, elapsed_seconds=time.time()-stage["started_unix"], error=repr(error))
                    save()
                    raise
            stage.update(returncode=returncode, elapsed_seconds=time.time()-stage["started_unix"])
            # Preserve the complete soak before another tool can replace LastTest.log.
            if name == "integration-soak":
                for filename in ("LastTest.log", "qualification.xml"):
                    original = build / "Testing/Temporary" / filename
                    if original.is_file():
                        shutil.copyfile(original, output / ("integration-" + filename))
                if returncode == 0:
                    summary, csv = analyze((output / "integration-LastTest.log").read_text(
                        encoding="utf8", errors="replace"), args.seconds)
                    (output / "metrics-summary.json").write_text(json.dumps(summary, indent=2), encoding="utf8")
                    (output / "metrics.csv").write_text(csv, encoding="utf8", newline="")
                    stage["measured_load_seconds"] = summary["measured_load_seconds"]
            save()
            if returncode:
                raise subprocess.CalledProcessError(returncode, argv)
        manifest["status"] = "PASS"
    except BaseException as error:
        manifest["status"] = "FAIL"
        manifest["error"] = repr(error)
        raise
    finally:
        # A supervisor timeout can interrupt the fixture's own finally block. Every candidate
        # here is inside this invocation's newly created output directory, never an existing DB.
        cleanup = []
        for data in temporary.glob("omp-pg-integration-*/data"):
            if (data / "postmaster.pid").is_file():
                assert data.resolve().is_relative_to(temporary.resolve())
                pg_ctl = str(Path(args.postgres_bin).resolve() / ("pg_ctl.exe" if os.name == "nt" else "pg_ctl")) if args.postgres_bin else "pg_ctl"
                try:
                    result = subprocess.run([pg_ctl, "-D", str(data), "-m", "immediate", "-w", "stop"],
                                            env=env, capture_output=True, timeout=30, creationflags=flags)
                    cleanup.append({"data": str(data), "returncode": result.returncode})
                except Exception as error:
                    cleanup.append({"data": str(data), "error": repr(error)})
        manifest["fallback_cleanup"] = cleanup
        if any(item.get("returncode", 1) for item in cleanup):
            manifest["status"] = "FAIL"
        manifest["ended_unix"] = time.time()
        save()
        if manifest["status"] == "FAIL" and sys.exc_info()[0] is None:
            raise RuntimeError("Disposable fixture cleanup failed; inspect manifest")


if __name__ == "__main__":
    main()
