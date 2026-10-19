"""Run smoke.amx in an isolated supplied open.mp distribution.

The directory must be a disposable extracted server; edits only its config,
smoke.amx, component and explicitly supplied runtime DLLs. PostgreSQL profile
'game' must be configured in PGSERVICEFILE. No public announcement or bind.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument("--server", required=True)
p.add_argument("--component", required=True)
p.add_argument("--gamemode", required=True)
p.add_argument("--runtime", action="append", default=[])
p.add_argument(
    "--sql-file", action="append", default=[], help="Trusted SQL fixture, copied by basename"
)
p.add_argument("--gmx-rounds", type=int, default=3)
p.add_argument("--unload-script")
p.add_argument("--success-marker", default="PG SMOKE PASS")
p.add_argument("--failure-marker", default="PG SMOKE FAILED")
args = p.parse_args()
root = Path(args.server).resolve()
config_path = root / "config.json"
config = json.loads(config_path.read_text(encoding="utf8"))
config.update(announce=False, enable_query=False, name="omp-postgresql isolated test")
config["network"].update(bind="127.0.0.1", port=57891)
config["pawn"].update(main_scripts=["smoke 1"], side_scripts=[], legacy_plugins=[])
if args.unload_script:
    config["pawn"]["side_scripts"] = ["filterscripts/pg_unload"]
config["rcon"]["enable"] = False
config["artwork"]["enable"] = False
config_path.write_text(json.dumps(config, indent=2), encoding="utf8")
shutil.copy2(args.component, root / "components" / Path(args.component).name)
shutil.copy2(args.gamemode, root / "gamemodes" / "smoke.amx")
if args.unload_script:
    shutil.copy2(args.unload_script, root / "filterscripts" / "pg_unload.amx")
for dll in args.runtime:
    shutil.copy2(dll, root / Path(dll).name)
if args.sql_file:
    sql_root = root / "scriptfiles" / "sql"
    sql_root.mkdir(parents=True, exist_ok=True)
    for filename in args.sql_file:
        source = Path(filename)
        if source.suffix != ".sql" or not source.is_file():
            raise ValueError("SQL fixtures must be existing .sql files")
        shutil.copy2(source, sql_root / source.name)
exe = root / ("omp-server.exe" if os.name == "nt" else "omp-server")
flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
log_names = {
    "PG SMOKE PASS": "pg-smoke-output.log",
    "PG CALLBACK PASS": "pg-callback-output.log",
    "PG EXTENSION PASS": "pg-extension-output.log",
    "PG BOOTSTRAP PASS": "pg-bootstrap-output.log",
    "PG SECTION PASS": "pg-section-output.log",
    "PG LOGGING PASS": "pg-logging-output.log",
    "PG BASIC PASS": "pg-basic-output.log",
    "PG VALUES PASS": "pg-values-output.log",
    "PG SAVING PASS": "pg-saving-output.log",
    "PG TRANSFER PASS": "pg-transfer-output.log",
}
log_path = root / log_names.get(args.success_marker, "pg-custom-output.log")
with log_path.open("wb") as log:
    proc = subprocess.Popen(
        [str(exe)],
        cwd=root,
        stdout=log,
        stderr=subprocess.STDOUT,
        stdin=subprocess.PIPE,
        creationflags=flags,
    )
    passed = False

    def command(text):
        # The Windows open.mp console reads UTF-16 wchar_t from redirected stdin.
        proc.stdin.write((text + "\n").encode("utf-16le" if os.name == "nt" else "utf8"))
        proc.stdin.flush()

    try:
        deadline = time.monotonic() + 75
        rounds = 0
        unloading = False
        while time.monotonic() < deadline:
            content = log_path.read_text(encoding="utf8", errors="replace")
            if args.unload_script and "PG UNLOAD PENDING" in content and not unloading:
                command("unloadfs pg_unload")
                unloading = True
            passes = content.count(args.success_marker)
            unloaded = not args.unload_script or "PG UNLOAD EXIT" in content
            # Wait for the unload acknowledgement before sending GMX. The Linux
            # console can otherwise coalesce adjacent stdin commands, and the
            # pending query must be unloaded before a mode reload is tested.
            if passes > rounds and unloaded:
                rounds = passes
                if rounds <= args.gmx_rounds:
                    command("gmx")
            if rounds > args.gmx_rounds and unloaded:
                # Allow a late result to expose delivery to an unloaded AMX.
                time.sleep(3.2 if args.unload_script else 0.1)
                passed = "PG UNLOAD CALLBACK" not in log_path.read_text(
                    encoding="utf8", errors="replace"
                )
                break
            if (
                args.failure_marker in content
                or "PG SMOKE LOCAL FAILURE" in content
                or "PG SMOKE CONNECTION FAILED" in content
                or proc.poll() is not None
            ):
                break
            time.sleep(0.1)
    finally:
        if proc.poll() is None:
            try:
                command("exit")
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.terminate()
                proc.wait(timeout=5)
print(log_path.read_text(encoding="utf8", errors="replace"))
if not passed:
    raise SystemExit("open.mp smoke did not pass")
if proc.returncode != 0:
    raise SystemExit(f"open.mp did not shut down cleanly (exit {proc.returncode})")
if args.success_marker == "PG LOGGING PASS":
    destination = root / "logs" / "postgresql.log"
    output = destination.read_text(encoding="utf8")
    if "Z [omp-pg][" not in output or "job_terminal" not in output:
        raise SystemExit("Component-owned log did not contain timestamped numeric events")
    if any(
        secret in output
        for secret in ("load_acount", "accounts.sql", "ReportParser", "SELECT", "password")
    ):
        raise SystemExit("Component-owned log leaked private source/callback content")
    print("Component-owned timestamped log/privacy check passed")
