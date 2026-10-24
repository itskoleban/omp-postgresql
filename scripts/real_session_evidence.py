"""Publish explicitly sanitized copies; keep original byte hashes and observations.

This module never edits the source evidence directory. Historical replacement in
Git is performed from a separate, verified local original, with the old ledger
retained as original-sha256.json. No password/token value is collected here.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
from datetime import datetime, timedelta


PROFILE = "local-paths-v1"
HOME_PATH = re.compile(r'''(?i)\b[a-z]\^?:[\\/^]+Users[\\/^]+[^\\/\s<>^"']+(?:[\\/^]+[^\\/\s<>^"']+)*''')
PRIVATE_MARKERS = re.compile(
    rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|"
    rb"\bAKIA[0-9A-Z]{16}\b|\bgh[pousr]_[A-Za-z0-9]{20,}|\bgithub_pat_[A-Za-z0-9_]{20,}|"
    rb"\$2[aby]\$\d\d\$[./A-Za-z0-9]{53}"
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def timeline_records(data):
    """Validate the fixture's deliberately payload-free, ordered JSONL records."""
    labels = {"event", "case", "status", "server", "observed", "name", "sqlstate"}
    numbers = {"monotonic_ns", "pid", "udp_port", "exit_code", "account", "callback_job",
               "callback_token", "closed", "connected", "forwarded", "job", "npc", "p",
               "ready", "state", "success", "token", "lane", "tick_ms", "attempts",
               "pending", "active", "load", "clan", "save", "property", "request"}

    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate timeline field")
            result[key] = value
        return result

    records = []
    for line in data.decode("utf8").splitlines():
        record = json.loads(line, object_pairs_hook=unique_object)
        if not isinstance(record, dict) or not {"utc", "monotonic_ns", "event"} <= record.keys():
            raise ValueError("Timeline record lacks clock/event identity")
        if set(record) - labels - numbers - {"utc"}:
            raise ValueError("Unexpected timeline field; payloads and secrets are forbidden")
        if not isinstance(record["utc"], str) or datetime.fromisoformat(record["utc"].replace("Z", "+00:00")).utcoffset() != timedelta(0):
            raise ValueError("Timeline clock must be UTC")
        for key, value in record.items():
            if key in labels and (not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_ -]{0,80}", value)):
                raise ValueError("Timeline labels must not contain paths or private payloads")
            if key in numbers and not (type(value) is int or (key == "exit_code" and value is None)):
                raise ValueError("Timeline numeric identity has an invalid type")
        if record["monotonic_ns"] < 0:
            raise ValueError("Invalid monotonic timeline clock")
        records.append(record)
    return records


def neutral_windows_path(value):
    path = re.sub(r"/+", "/", value.replace("^", "").replace("\\", "/"))
    parts = path.split("/")
    lower = [part.lower() for part in parts]
    anchors = {
        "omp-postgresql": "<WORKSPACE>",
        "client-repro-win": "<CLIENT_BUILD>",
        "client-repro-linux": "<CLIENT_BUILD>",
        "raksamp-cli": "<CLIENT_SOURCE>",
    }
    for anchor, replacement in anchors.items():
        if anchor in lower:
            index = len(lower) - 1 - lower[::-1].index(anchor)
            return replacement + ("/" + "/".join(parts[index+1:]) if parts[index+1:] else "")
    for index, part in enumerate(lower):
        if re.fullmatch(r"python\d+", part):
            return "<PYTHON_RUNTIME>/" + "/".join(parts[index:])
    for index, part in enumerate(lower):
        if part.startswith("omp-pg-integration-"):
            return "<POSTGRES_CLUSTER>" + ("/" + "/".join(parts[index+1:]) if parts[index+1:] else "")
    if "temp" in lower:
        index = lower.index("temp") + 1
        if index < len(parts):
            return "<TMP>/fixture-" + sha(parts[index].encode())[:12] + ("/" + "/".join(parts[index+1:]) if parts[index+1:] else "")
    if "work" in lower:
        index = len(lower) - 1 - lower[::-1].index("work")
        tail = parts[index+1:]
        if tail and tail[0].lower() in {"libpq32", "compiler", "stdlib", "sdk", "server17", "server17-min", "postgresql-17.11", "pawn32"}:
            return "<DEPENDENCIES>/" + "/".join(tail)
        if len(tail)>1 and tail[1].lower()=="evidence":
            return "<EVIDENCE>" + ("/" + "/".join(tail[2:]) if tail[2:] else "")
    if lower[-1] in {"real_sessions.amx", "real_custom_sessions.amx"}:
        return "<FIXTURES>/" + parts[-1]
    if lower[-1] in {"samp_bcrypt.dll", "samp_bcrypt.so"}:
        return "<PROVIDER>/plugins/" + parts[-1]
    # Unknown personal locations: expose a stable opaque locator, not private dirs.
    return "<LOCAL_PATH>/" + sha(path.encode())[:12]


def sanitize_text(value):
    counts = Counter()

    def replace(pattern, replacement, rule):
        nonlocal value
        value, count = re.subn(pattern, replacement, value)
        if count:
            counts[rule] += count

    replace(HOME_PATH, lambda m: neutral_windows_path(m.group()), "windows-personal-path")
    replacements = (
        (r"/home/runner/work/_temp", "<RUNNER_TEMP>", "runner-temp"),
        (r"/home/runner/work/[^/\s\"']+/[^/\s\"']+", "<RUNNER_WORKSPACE>", "runner-workspace"),
        (r"/tmp/omp-pg-integration-[A-Za-z0-9_-]+", "<POSTGRES_CLUSTER>", "postgres-cluster"),
        (r"/tmp/linux-evidence/[^/\s\"']+/evidence", "<EVIDENCE>", "linux-evidence"),
        (r"/tmp/client-repro-linux", "<CLIENT_BUILD>", "client-build"),
        (r"/tmp/build32", "<WORKSPACE>/build-linux32", "linux-build"),
        (r"/tmp/omp-official/Server", "<OPENMP_SERVER>", "official-server"),
        (r"/tmp/(stdlib|compiler|libpq32|bcrypt)(?=/|\b)", r"<DEPENDENCIES>/\1", "linux-dependency"),
        (r"/work/omp-postgresql", "<WORKSPACE>", "linux-workspace"),
        (r"/work/RakSAMP-cli", "<CLIENT_SOURCE>", "linux-client-source"),
        (r"/tmp/tmp[A-Za-z0-9_-]+", lambda m: "<TMP>/fixture-" + sha(m.group().encode())[:12], "temporary-fixture"),
        (r"/tmp/", "<TMP>/", "temporary-root"),
    )
    for pattern, replacement, rule in replacements:
        replace(pattern, replacement, rule)
    return value, dict(counts)


def sanitized_bytes(data, suffix):
    counts = Counter()
    if suffix == ".jsonl":
        # Valid records have no free-form payload/path fields. Preserve even their
        # original formatting and line endings; secret markers are checked below.
        timeline_records(data)
        return data, {}
    if suffix == ".json":
        original = json.loads(data)

        def walk(value):
            if isinstance(value, str):
                sanitized, changes = sanitize_text(value)
                counts.update(changes)
                return sanitized
            if isinstance(value, list):
                return [walk(item) for item in value]
            if isinstance(value, dict):
                return {key: walk(item) for key, item in value.items()}
            return value

        result = walk(original)
        # Leave original formatting/bytes intact if there is nothing to sanitize.
        serialized = json.dumps(result, indent=2) + "\n"
        if b"\r\n" in data:
            serialized = serialized.replace("\n", "\r\n")
        return (serialized.encode() if counts else data), dict(counts)
    value, changes = sanitize_text(data.decode("utf8"))
    if suffix == ".xml":
        value, hosts = re.subn(r'hostname="(?!SANITIZED-HOST")[^"]*"', 'hostname="SANITIZED-HOST"', value)
        if hosts:
            changes["host-identity"] = hosts
        value = re.sub(r"<(WORKSPACE|DEPENDENCIES|CLIENT_BUILD|CLIENT_SOURCE|POSTGRES_CLUSTER|PYTHON_RUNTIME|LOCAL_PATH|TMP|EVIDENCE|OPENMP_SERVER|RUNNER_TEMP|RUNNER_WORKSPACE|FIXTURES|PROVIDER)>", r"&lt;\1&gt;", value)
    return value.encode("utf8"), changes


def outcome_summary(path, data):
    """Only observations, never environment paths, for preservation checks."""
    if path.suffix == ".jsonl":
        return timeline_records(data)
    if path.suffix == ".json":
        value = json.loads(data)
        if isinstance(value, dict) and "results" in value:
            def normalize(item):
                if isinstance(item,str):
                    return sanitize_text(item)[0]
                if isinstance(item,list):
                    return [normalize(x) for x in item]
                if isinstance(item,dict):
                    return {k:normalize(v) for k,v in item.items()}
                return item
            return normalize(value["results"])
    if path.name == "server-output.txt" or path.suffix == ".txt":
        return [sanitize_text(line)[0] for line in data.decode("utf8").splitlines() if "QA event=" in line]
    if path.suffix == ".xml":
        import xml.etree.ElementTree as ET
        root = ET.fromstring(data)
        return [(c.get("name"), c.get("status"), [x.tag for x in c if x.tag in ("failure", "error", "skipped")]) for c in root.iter("testcase")]
    return None


def observation_hash(path, data):
    return sha(json.dumps(outcome_summary(path, data),sort_keys=True,separators=(",",":"),ensure_ascii=True).encode())


def publish(source, destination, *, original_commit=None, original_ledger=None):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination or destination.is_relative_to(source):
        raise ValueError("Sanitized copies require a separate destination outside originals")
    destination.mkdir(parents=True, exist_ok=False)
    if original_ledger:
        entries = json.loads(Path(original_ledger).read_bytes())
        paths = [source / entry["file"] for entry in entries]
        for entry, path in zip(entries, paths):
            if not path.resolve().is_relative_to(source) or sha(path.read_bytes()) != entry["sha256"] or path.stat().st_size != entry["bytes"]:
                raise ValueError("Original evidence does not match its registered ledger")
        ledger_bytes = Path(original_ledger).read_bytes()
        (destination / "original-sha256.json").write_bytes(ledger_bytes)
    else:
        paths = sorted(p for p in source.rglob("*") if p.is_file() and
                       (p.name in ("results.json", "timeline.jsonl", "server-output.txt", "client-output.txt", "client-build.json") or
                        (p.parent == source and p.name.startswith("postgres-") and p.suffix == ".txt")))
        ledger_bytes = None
    ledger = []
    for path in sorted(paths):
        data = path.read_bytes()
        result, transformations = sanitized_bytes(data, path.suffix)
        if outcome_summary(path, data) != outcome_summary(path, result):
            raise ValueError("Sanitization changed a test observation")
        if PRIVATE_MARKERS.search(result):
            raise ValueError("Credential/private-key/bcrypt value must not be published")
        if HOME_PATH.search(result.decode("utf8")):
            raise ValueError("Unmapped personal path")
        relative = path.relative_to(source).as_posix()
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(result)
        ledger.append({"file":relative, "bytes":len(result), "sha256":sha(result),
                       "original_bytes":len(data), "original_sha256":sha(data),
                       "observations_sha256":observation_hash(path,data),
                       "representation":"sanitized-copy" if result != data else "unchanged-original",
                       "transformations":transformations})
    (destination / "sha256.json").write_text(json.dumps(ledger, indent=2)+"\n", encoding="utf8")
    metadata = {"schema":1, "profile":PROFILE, "original_commit":original_commit,
                "original_ledger_sha256":sha(ledger_bytes) if ledger_bytes else None,
                "original_ledger":"original-sha256.json" if ledger_bytes else None,
                "files":len(ledger), "sanitized_copies":sum(e["representation"] == "sanitized-copy" for e in ledger),
                "observations_changed":False,
                "notes":["Original hashes identify pre-transformation bytes, not sanitized copies.",
                         "Originals are not duplicated here. Old Git history is not rewritten.",
                         "Only environment path strings change; session/job tokens, outcomes and binary hashes remain."]}
    (destination / "sanitization.json").write_text(json.dumps(metadata, indent=2)+"\n", encoding="utf8")
    verify(destination)
    return metadata


def verify(directory):
    directory = Path(directory)
    ledger = json.loads((directory / "sha256.json").read_bytes())
    metadata = json.loads((directory / "sanitization.json").read_bytes())
    originals = {}
    if metadata["original_ledger"]:
        data = (directory / metadata["original_ledger"]).read_bytes()
        if sha(data) != metadata["original_ledger_sha256"]:
            raise ValueError("Original ledger hash mismatch")
        originals = {e["file"]:e for e in json.loads(data)}
    for entry in ledger:
        path = (directory / entry["file"]).resolve()
        if not path.is_relative_to(directory.resolve()):
            raise ValueError("Evidence path escaped directory")
        data = path.read_bytes()
        if sha(data) != entry["sha256"] or len(data) != entry["bytes"]:
            raise ValueError("Sanitized evidence hash/size mismatch")
        if observation_hash(path,data) != entry["observations_sha256"]:
            raise ValueError("Test observation fingerprint mismatch")
        if originals and (entry["original_sha256"] != originals[entry["file"]]["sha256"] or entry["original_bytes"] != originals[entry["file"]]["bytes"]):
            raise ValueError("Original identity was overwritten")
        if HOME_PATH.search(data.decode("utf8")) or PRIVATE_MARKERS.search(data):
            raise ValueError("Sensitive evidence content")
    if len(ledger) != metadata["files"]:
        raise ValueError("Evidence inventory mismatch")
    expected = {e["file"] for e in ledger} | {"sha256.json", "sanitization.json", "README.md"}
    if metadata["original_ledger"]:
        expected.add(metadata["original_ledger"])
    if len({e["file"] for e in ledger}) != len(ledger) or any(p.relative_to(directory).as_posix() not in expected for p in directory.rglob("*") if p.is_file()):
        raise ValueError("Unexpected or duplicate evidence file")
    return len(ledger)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("publish")
    create.add_argument("--source", type=Path, required=True)
    create.add_argument("--destination", type=Path, required=True)
    create.add_argument("--original-ledger", type=Path)
    create.add_argument("--original-commit")
    check = sub.add_parser("verify")
    check.add_argument("directory", type=Path)
    args = parser.parse_args()
    if args.command == "verify":
        print(f"Verified {verify(args.directory)} original/sanitized identities")
    else:
        print(json.dumps(publish(args.source, args.destination, original_commit=args.original_commit, original_ledger=args.original_ledger)))


if __name__ == "__main__":
    main()
