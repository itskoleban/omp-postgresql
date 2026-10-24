"""Deterministic legacy-residue and historical-identity checks; no language guessing."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
POLICY = ".github/english-policy.json"
MIGRATION = "audit/language-migration.json"


def inventory(root):
    return subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode("utf8").rstrip("\0").split("\0")


def check(root, paths=None):
    paths = sorted(set(inventory(root) if paths is None else paths))
    policy = json.loads((root/POLICY).read_text(encoding="utf8"))
    rules = [(row["id"], re.compile(row["pattern"], re.IGNORECASE)) for row in policy["residue_patterns"]]
    exceptions = policy["frozen_files"]
    errors = []
    for name, row in exceptions.items():
        parts = PurePosixPath(name).parts
        if not parts or ".." in parts or PurePosixPath(name).is_absolute() or not row["reason"]:
            raise ValueError("Invalid historical exception path/reason")
        if name not in paths:
            errors.append(f"{name}: frozen exception missing from tracked inventory")
        elif hashlib.sha256((root/name).read_bytes()).hexdigest() != row["sha256"]:
            errors.append(f"{name}: historical exception identity changed")
    migration_path = root/MIGRATION
    migration = json.loads(migration_path.read_text(encoding="utf8")) if migration_path.is_file() else {"files":[]}
    aliases = {row["path"]:row["legacy_heading_ids"] for row in migration["files"]}
    scanned = 0
    for name in paths:
        if not name or name in exceptions:
            continue
        data = (root/name).read_bytes()
        if b"\0" in data[:8192]:
            continue
        try:
            content = data.decode("utf8")
        except UnicodeDecodeError:
            errors.append(f"{name}: maintained text must be UTF-8 or have an explicit frozen exception")
            continue
        # Rule vocabulary and exact legacy IDs are machine data. Keep English
        # explanations in those same files subject to the ordinary residue rules.
        if name == POLICY:
            content = json.dumps({k:v for k,v in policy.items() if k != "residue_patterns"})
        elif name == MIGRATION:
            content = json.dumps({**migration,"files":[{k:v for k,v in row.items() if k != "legacy_heading_ids"} for row in migration["files"]]})
        else:
            for alias in aliases.get(name, []):
                content = content.replace('<a id="'+alias+'"></a>', "")
        scanned += 1
        for number, line in enumerate(content.splitlines(), 1):
            for rule, expression in rules:
                if expression.search(line):
                    errors.append(f"{name}:{number}: legacy residue {rule}; review/translate maintained prose")
    if errors:
        raise ValueError("\n".join(errors))
    print(f"English maintenance check: {scanned} maintained text files; {len(exceptions)} exact historical/license identities verified")
    return scanned, len(exceptions)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    check(parser.parse_args().root.resolve())
