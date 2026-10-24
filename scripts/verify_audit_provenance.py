"""Verify historical evidence from Git blobs, without treating it as current tests."""
import hashlib
from pathlib import PurePosixPath
import subprocess

root = PurePosixPath("audit/stability-evidence")
count = 0
for manifest in (root / "SHA256SUMS.txt", root / "a1-a2/SHA256SUMS.txt"):
    data = subprocess.check_output(["git", "show", f"HEAD:{manifest}"]).decode("utf8")
    for line in data.splitlines():
        digest, name = line.split("  ")
        path = manifest.parent / name
        blob = subprocess.check_output(["git", "show", f"HEAD:{path}"])
        actual = hashlib.sha256(blob).hexdigest()
        if actual != digest:
            raise SystemExit(f"FAIL historical evidence mismatch: {path}: {actual} != {digest}")
        print(f"PASS historical hash only: {path}: {actual}")
        count += 1
print(f"Verified {count} historical blobs. Their source/duration/scope remain historical.")
