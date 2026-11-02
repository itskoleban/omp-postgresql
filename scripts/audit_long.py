"""External dedicated-runner entry point. No production connection argument exists.

Configure/build first with PG_STRESS_SECONDS equal to --seconds (120..86400).
Requires the existing run_postgres.py tools; creates its own loopback TLS cluster.
Keeps per-stage logs and a manifest even on timeout/failure. Never publishes anything.
Extended fuzz and every bounded integration check precede the final load campaign.
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
import hashlib
from qualification_metrics import analyze


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--seconds", type=int, default=86400)
    parser.add_argument("--seed", type=int, default=1592594996)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--postgres-bin")
    parser.add_argument("--preflight-only", action="store_true",
                        help="Complete fuzz and bounded checks without starting the load campaign")
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
    # Persist before discovery: missing tools, timeouts and malformed inventories are
    # negative preflight evidence too, even when no integration stage was launched.
    manifest = {"commit": None, "dirty": None, "seconds": args.seconds, "seed": args.seed,
                "started_unix": time.time(), "stages": [], "status": "RUNNING",
                "preflight": {"status": "RUNNING"}, "tool_versions": {},
                "mode": "preflight-only" if args.preflight_only else "preflight-and-load",
                "campaign_started": False}
    fuzz = build / ("pg_audit_fuzz.exe" if os.name == "nt" else "pg_audit_fuzz")
    if not fuzz.is_file():
        fuzz = build / args.config / fuzz.name
    stress = build / ("pg_stress_tests.exe" if os.name == "nt" else "pg_stress_tests")
    if not stress.is_file():
        stress = build / args.config / stress.name
    def save():
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf8")
    save()
    command = [sys.executable, "tests/run_postgres.py", "--build", str(build),
               "--config", args.config]
    if args.postgres_bin:
        command += ["--postgres-bin", str(Path(args.postgres_bin).resolve())]
    try:
        manifest["commit"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
        manifest["dirty"] = subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=root, text=True)
        if manifest["dirty"] and not args.preflight_only:
            raise RuntimeError("Load campaign requires a clean, frozen source checkout")
        manifest["platform"] = platform.platform()
        manifest["python"] = sys.version
        # pg_config describes the client development toolchain on PATH; postgres
        # must identify the actual fixture server selected by --postgres-bin.
        postgres = str(Path(args.postgres_bin).resolve() / (
            "postgres.exe" if os.name == "nt" else "postgres")) if args.postgres_bin else "postgres"
        for tool in (["cmake", "--version"], ["pg_config", "--version"],
                     [postgres, "--version"], ["openssl", "version"]):
            manifest["preflight"]["command"] = tool
            save()
            result = subprocess.run(tool, capture_output=True, text=True, env=env,
                                    creationflags=flags, timeout=30)
            manifest["tool_versions"][" ".join(tool)] = {
                "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
            save()
            result.check_returncode()
        for info in build.glob("package-build-info-*.json"):
            manifest.setdefault("build_info", {})[info.name] = json.loads(info.read_text(encoding="utf8"))
        manifest["executable_sha256"] = {
            str(binary): hashlib.sha256(binary.read_bytes()).hexdigest()
            for binary in (fuzz, stress)}
        save()
        stages = [
            ("extended-fuzz", [str(fuzz)], 600),
            ("bounded-preflight", command + ["--preflight-only", "--doc-sql"], 900),
        ]
        if not args.preflight_only:
            stages.append(("integration-soak", command + ["--soak-only"], args.seconds + 900))
        for name, argv, budget in stages:
            # Source/executable changes invalidate preflight. Do not start a load
            # campaign on different bytes or claim its checks belong to new inputs.
            if name == "integration-soak":
                if manifest["preflight"]["status"] != "PASS":
                    raise RuntimeError("Load campaign requires completed preflight")
                for filename, expected in manifest["executable_sha256"].items():
                    if hashlib.sha256(Path(filename).read_bytes()).hexdigest() != expected:
                        raise RuntimeError("Executable changed after preflight: " + filename)
                if subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip() != manifest["commit"]:
                    raise RuntimeError("Source commit changed after preflight")
                if subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True) != manifest["dirty"]:
                    raise RuntimeError("Source worktree changed after preflight")
                manifest["campaign_started"] = True
            stage = {"name": name, "command": argv, "started_unix": time.time()}
            manifest["stages"].append(stage)
            save()
            stage_env = env.copy()
            if name == "extended-fuzz":
                stage_env["OMP_PG_FUZZ_CASES"] = "100000"
            # A failing fuzz run writes its corpus in this stage's retained directory.
            stage_directory = output / name
            stage_directory.mkdir()
            stage["working_directory"] = str(stage_directory if name == "extended-fuzz" else root)
            if name in ("bounded-preflight", "integration-soak"):
                # Even a launch/import failure must never relabel an earlier report.
                for filename in ("LastTest.log", "qualification.xml"):
                    (build / "Testing/Temporary" / filename).unlink(missing_ok=True)
            with (output / f"{name}.log").open("wb") as log:
                process = subprocess.Popen(
                    argv, cwd=stage["working_directory"], env=stage_env, stdout=log, stderr=subprocess.STDOUT,
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
            # Preserve each CTest report before the next stage can replace it.
            if name in ("bounded-preflight", "integration-soak"):
                for filename in ("LastTest.log", "qualification.xml"):
                    original = build / "Testing/Temporary" / filename
                    if original.is_file():
                        shutil.copyfile(original, output / (name + "-" + filename))
                if name == "integration-soak" and returncode == 0:
                    summary, csv = analyze((output / (name + "-LastTest.log")).read_text(
                        encoding="utf8", errors="replace"), args.seconds)
                    (output / "metrics-summary.json").write_text(json.dumps(summary, indent=2), encoding="utf8")
                    (output / "metrics.csv").write_text(csv, encoding="utf8", newline="")
                    stage["measured_load_seconds"] = summary["measured_load_seconds"]
            save()
            if returncode:
                raise subprocess.CalledProcessError(returncode, argv)
            if name == "bounded-preflight":
                manifest["preflight"]["status"] = "PASS"
                save()
        manifest["status"] = "PASS"
    except BaseException as error:
        if manifest["preflight"]["status"] == "RUNNING":
            manifest["preflight"]["status"] = "FAIL"
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
