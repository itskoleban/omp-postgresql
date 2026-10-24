"""Read-only Git provenance: never equate generated files with tracked edits."""
import hashlib
from pathlib import Path
import re
import subprocess


GENERATED_ROOTS = {"libpq32", "postgresql-17.11", "postgresql-17.11.tar.gz",
                   "postgresql-17.11.tar.bz2", "postgres.zip", "openmp.zip",
                   "openmp-linux-dynssl.tar.gz"}


def git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root)


def parse_status(data):
    records, index = data.decode("utf8", errors="surrogateescape").split("\0"), 0
    tracked, untracked = [], []
    while index < len(records):
        record = records[index]
        index += 1
        if not record:
            continue
        status, path = record[:2], record[3:]
        if status == "??":
            root_name = path.rstrip("/").split("/",1)[0]
            untracked.append({"path":path, "classification":"expected-ci-generated" if root_name in GENERATED_ROOTS else "unclassified"})
        else:
            entry = {"status":status, "path":path}
            if "R" in status or "C" in status:
                entry["original_path"] = records[index]
                index += 1
            tracked.append(entry)
    return tracked, untracked


def capture(root):
    root = Path(root)
    commit = git(root, "rev-parse", "HEAD").decode().strip()
    tree = git(root, "rev-parse", "HEAD^{tree}").decode().strip()
    status = git(root, "status", "--porcelain=v1", "-z", "--untracked-files=normal")
    tracked, untracked = parse_status(status)
    diff = git(root, "diff", "--binary", "HEAD")
    state = "tracked-modified" if tracked else ("generated-files-only" if untracked and all(x["classification"]=="expected-ci-generated" for x in untracked) else ("untracked-unclassified" if untracked else "clean"))
    return {"schema":1, "source_commit":commit, "head_tree":tree,
            "source_dirty":bool(tracked or untracked), "tracked_source_modified":bool(tracked),
            "tracked_changes":tracked, "untracked_entries":untracked,
            "untracked_count":len(untracked), "source_state":state,
            "tracked_patch_sha256":hashlib.sha256(diff).hexdigest() if diff else None,
            "status_sha256":hashlib.sha256(status).hexdigest(),
            "generated_classification_basis":"Exact roots created by .github/workflows/ci.yml; all other entries remain unclassified"}
