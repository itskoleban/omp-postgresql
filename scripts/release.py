"""Prepare a source-bound RC inventory or validate publication inputs; no network writes."""

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
from urllib.parse import urljoin

import package

ROOT = Path(__file__).resolve().parents[1]
JOBS = {
    "linux (OFF)",
    "linux (ON)",
    "linux (THREAD)",
    "windows",
    "Linux x86 and open.mp",
    "Windows x86 MSVC and open.mp",
    "Verify downloaded x86 distributions",
}


def source_identity(source):
    commit = package.run(["git", "rev-parse", "HEAD"], cwd=source)
    cmake = package.run(["git", "show", f"{commit}:CMakeLists.txt"], cwd=source)
    base = re.search(r"\bVERSION ([0-9]+\.[0-9]+\.[0-9]+)\b", cmake)
    candidate = re.search(r"^set\(PG_RELEASE_CANDIDATE ([1-9][0-9]*)\)$", cmake, re.MULTILINE)
    if not base or not candidate:
        raise ValueError("Source does not declare a Release Candidate")
    version = f"{base[1]}-rc.{candidate[1]}"
    package.archive_name(version, commit, "linux")
    return commit, version


def validate_run(run, jobs, repository, commit, tag, confirmation):
    if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+-rc\.[1-9][0-9]*", tag):
        raise ValueError("Publication requires an RC tag")
    if confirmation != tag:
        raise ValueError("Explicit confirmation must equal the tag")
    if (
        run.get("status") != "completed"
        or run.get("conclusion") != "success"
        or run.get("event") not in {"push", "workflow_dispatch"}
        or run.get("head_branch") != "master"
        or run.get("head_sha") != commit
        or run.get("path") != ".github/workflows/ci.yml"
        or run.get("repository", {}).get("full_name") != repository
        or run.get("head_repository", {}).get("full_name") != repository
        or not isinstance(run.get("id"), int)
        or run.get("id", 0) <= 0
    ):
        raise ValueError("Publication requires successful exact-commit CI on the main repository")
    inventory = jobs.get("jobs", [])
    if (
        len(inventory) != len(JOBS)
        or {job.get("name") for job in inventory} != JOBS
        or any(
            job.get("conclusion") != "success"
            or job.get("run_id") != run["id"]
            or job.get("head_sha") != commit
            for job in inventory
        )
    ):
        raise ValueError("Every required CI job must have succeeded on the selected run")


def check_publication(args):
    run = json.loads(args.run.read_text(encoding="utf8"))
    jobs = json.loads(args.jobs.read_text(encoding="utf8"))
    commit = run.get("head_sha", "")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Invalid CI source commit")
    validate_run(run, jobs, args.repository, commit, args.tag, args.confirmation)
    tagged = package.run(["git", "rev-parse", f"refs/tags/{args.tag}^{{commit}}"], cwd=ROOT)
    if tagged != commit:
        raise ValueError("Existing tag must identify the tested commit")
    subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, "origin/master"], cwd=ROOT, check=True
    )
    subprocess.run(["git", "checkout", "--detach", commit], cwd=ROOT, check=True)
    if source_identity(ROOT) != (commit, args.tag[1:]):
        raise ValueError("Tag version differs from tested source")
    print(f"Approved publication inputs: {args.tag} at {commit}")


def check_remote_tag(repository, tag, commit):
    if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+-rc\.[1-9][0-9]*", tag):
        raise ValueError("Publication requires an RC tag")
    reference = json.loads(package.run(["gh", "api", f"repos/{repository}/git/ref/tags/{tag}"]))
    target = reference["object"]
    # Support lightweight and annotated tags, but fail closed on unexpected objects.
    for _ in range(8):
        if target["type"] == "commit":
            if target["sha"] != commit:
                raise ValueError("Remote tag no longer identifies the tested commit")
            return
        if target["type"] != "tag" or not re.fullmatch(r"[0-9a-f]{40}", target["sha"]):
            break
        target = json.loads(
            package.run(["gh", "api", f"repos/{repository}/git/tags/{target['sha']}"])
        )["object"]
    raise ValueError("Remote tag does not resolve to a commit within eight objects")


def prepare(packages, output, source=ROOT):
    package.ensure_checkout(source)
    commit, version = source_identity(source)
    expected = {
        package.archive_name(version, commit, platform) for platform in ("windows", "linux")
    }
    if {path.name for path in packages.iterdir()} != expected | {
        f"{name}.sha256" for name in expected
    }:
        raise ValueError("Exactly two platform archives and their checksums are required")
    manifests = {}
    for name in sorted(expected):
        manifest = package.verify(packages / name)
        if manifest["commit"] != commit or manifest["version"] != version:
            raise ValueError("Every distribution must match the exact source and candidate version")
        manifests[manifest["platform"]] = manifest
    notes = (source / "docs/release-notes.md").read_text(encoding="utf8")
    if not notes.startswith(f"# omp-postgresql {version}\n"):
        raise ValueError("Release notes do not match the candidate version")
    base_url = f"https://github.com/itskoleban/omp-postgresql/blob/{commit}/docs/"
    notes = re.sub(r"\]\(([^)]+)\)", lambda match: f"]({urljoin(base_url, match[1])})", notes)
    output.mkdir(parents=True, exist_ok=False)
    for name in sorted(expected):
        shutil.copyfile(packages / name, output / name)
        shutil.copyfile(packages / f"{name}.sha256", output / f"{name}.sha256")
    source_name = f"omp-postgresql-{version}.{commit[:12]}-source.zip"
    subprocess.run(
        [
            "git",
            "archive",
            "--format=zip",
            "--prefix=omp-postgresql/",
            f"--output={output.resolve() / source_name}",
            commit,
        ],
        cwd=source,
        check=True,
    )
    (output / "RELEASE-NOTES.md").write_text(
        f"{notes}\n## Exact candidate identity\n\nSource commit: `{commit}`.\n"
        "Package manifests below record build settings, dependencies, licenses and payload hashes.\n"
        "A CI artifact or this inventory does not authorize publication.\n",
        encoding="utf8",
        newline="\n",
    )
    inventory = {
        "schema": 1,
        "version": version,
        "tag": f"v{version}",
        "commit": commit,
        "source_archive": source_name,
        "source_sha256": package.digest((output / source_name).read_bytes()),
        "distributions": {
            platform: {
                "archive_sha256": package.digest((packages / manifest["archive"]).read_bytes()),
                "manifest": manifest,
            }
            for platform, manifest in manifests.items()
        },
        "provenance": "Unsigned content inventory; authenticated CI run must be checked separately before publication.",
    }
    (output / "release-inventory.json").write_text(
        json.dumps(inventory, indent=4, sort_keys=True) + "\n", encoding="utf8", newline="\n"
    )
    checksums = "".join(
        f"{package.digest(path.read_bytes())}  {path.name}\n" for path in sorted(output.iterdir())
    )
    (output / "SHA256SUMS").write_text(checksums, encoding="utf8", newline="\n")
    print(f"Prepared {version} from {commit}: {output}")
    return inventory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    bundle = commands.add_parser("prepare")
    bundle.add_argument("--packages", type=Path, required=True)
    bundle.add_argument("--output", type=Path, required=True)
    check = commands.add_parser("check-publication")
    check.add_argument("--run", type=Path, required=True)
    check.add_argument("--jobs", type=Path, required=True)
    check.add_argument("--repository", required=True)
    check.add_argument("--tag", required=True)
    check.add_argument("--confirmation", required=True)
    remote = commands.add_parser("check-remote-tag")
    remote.add_argument("--repository", required=True)
    remote.add_argument("--tag", required=True)
    remote.add_argument("--commit", required=True)
    args = parser.parse_args()
    if args.command == "prepare":
        prepare(args.packages, args.output)
    elif args.command == "check-publication":
        check_publication(args)
    else:
        check_remote_tag(args.repository, args.tag, args.commit)


if __name__ == "__main__":
    main()
