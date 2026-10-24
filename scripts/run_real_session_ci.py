"""Prepare pinned clients/provider and execute the bounded real-session suite.

Uses the already-built x86 component, official server and Pawn compiler. The
PostgreSQL runner always creates its own cluster. Never uses a public server.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from real_session_evidence import publish


PROVIDER_HASHES = {
    "windows": "673e7511699a8e92452a323d4e35567e2c990c0779e1ef2d20b298940cd031c3",
    "linux": "ebf76b12d799046a605e3063bb7b64b08e1fad9bdb3c299b357ec215a0793b5d",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("output", "component", "server", "compiler", "stdlib", "pawn-include"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--runtime", type=Path, action="append", default=[])
    parser.add_argument("--postgres-bin", default="")
    parser.add_argument("--mingw-bin")
    parser.add_argument("--bash", default="bash")
    parser.add_argument("--client", type=Path, help="Use an already prepared pinned client")
    args = parser.parse_args()
    repo = Path(__file__).parents[1]
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    def run(argv, **kwargs):
        subprocess.run([str(x) for x in argv], check=True, **kwargs)
    client = args.client
    if client is None:
        command = [sys.executable, repo / "scripts/prepare_real_clients.py", "--output", root / "client", "--bash", args.bash]
        if args.mingw_bin:
            command += ["--mingw-bin", args.mingw_bin]
        run(command)
        client = root / "client/build" / ("RakSAMPClient.exe" if os.name == "nt" else "RakSAMPClient")
    system = "windows" if os.name == "nt" else "linux"
    archive = root / "samp-bcrypt-0.4.1.zip"
    urllib.request.urlretrieve(f"https://github.com/Sreyas-Sreelal/samp-bcrypt/releases/download/0.4.1/samp-bcrypt-{system}-x86.zip", archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != PROVIDER_HASHES[system]:
        raise RuntimeError("Provider archive SHA-256 differs from reviewed input")
    with zipfile.ZipFile(archive) as source:
        source.extractall(root / "provider")
    gamemodes = []
    for name in ("real_sessions", "real_custom_sessions"):
        output = root / (name+".amx")
        run([args.compiler.resolve(), repo / "tests" / (name+".pwn"), "-E",
             "-i"+str(repo / "pawn"), "-i"+str(args.stdlib.resolve()),
             "-i"+str(args.pawn_include.resolve()), "-i"+str(root / "provider/includes"), "-o"+str(output)])
        gamemodes.append(output)
    config = {"server": str(args.server.resolve()), "component": str(args.component.resolve()),
              "gamemode": str(gamemodes[0]), "custom_gamemode": str(gamemodes[1]),
              "client": str(client.resolve()), "provider": str(root / "provider/plugins" / ("samp_bcrypt.dll" if os.name == "nt" else "samp_bcrypt.so")),
              "runtime": [str(p.resolve()) for p in args.runtime], "output": str(root / "evidence")}
    config_path = root / "config.json"
    config_path.write_text(json.dumps(config, indent=2)+"\n", encoding="utf8")
    try:
        run([sys.executable, repo / "tests/run_postgres.py", "--only-real-sessions", "--real-sessions", config_path,
             "--postgres-bin", args.postgres_bin], timeout=600)
    finally:
        # Raw local files remain available to the operator. Only explicitly
        # labelled copies with original/new byte hashes are uploaded by CI.
        evidence = root / "evidence"
        if evidence.exists():
            client_manifest = client.resolve().parent.parent / "client-build.json"
            if client_manifest.is_file():
                shutil.copy2(client_manifest, evidence / "client-build.json")
            publish(evidence, root / "sanitized-evidence")
            ledger = json.loads((root/"sanitized-evidence/sha256.json").read_text(encoding="utf8"))
            timeline = next((row for row in ledger if row["file"] == "timeline.jsonl"), None)
            if (evidence/"timeline.jsonl").is_file():
                if timeline is None:
                    raise RuntimeError("Available timeline was omitted from public evidence")
                events = len((root/"sanitized-evidence/timeline.jsonl").read_bytes().splitlines())
                print(f"REAL SESSION TIMELINE events={events} original_sha256={timeline['original_sha256']} sha256={timeline['sha256']} observations_sha256={timeline['observations_sha256']}", flush=True)
            if (evidence / "results.json").is_file():
                results = json.loads((evidence / "results.json").read_text(encoding="utf8"))
                if os.environ.get("GITHUB_ACTIONS") == "true" and results["source_provenance"]["tracked_source_modified"]:
                    raise RuntimeError("Tracked source changed before session qualification; evidence retained")


if __name__ == "__main__":
    main()
