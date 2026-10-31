"""Fail-closed candidate identity, bundle and publication checks; no network writes."""

import copy
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from test_package import payload

sys.path.insert(0, str(Path(__file__).parents[1] / "scripts"))
spec = importlib.util.spec_from_file_location("release", Path(sys.path[0]) / "release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.commit = "a" * 40
        self.tag = "v0.1.0-rc.1"
        self.repository = "itskoleban/omp-postgresql"
        self.run = {
            "id": 42,
            "status": "completed",
            "conclusion": "success",
            "event": "push",
            "head_branch": "master",
            "head_sha": self.commit,
            "path": ".github/workflows/ci.yml",
            "repository": {"full_name": self.repository},
            "head_repository": {"full_name": self.repository},
        }
        self.jobs = {
            "jobs": [
                {"name": name, "conclusion": "success", "run_id": 42, "head_sha": self.commit}
                for name in sorted(release.JOBS)
            ]
        }

    def validate(self):
        release.validate_run(self.run, self.jobs, self.repository, self.commit, self.tag, self.tag)

    def test_complete_exact_main_run_is_accepted(self):
        self.validate()
        self.run["event"] = "workflow_dispatch"
        self.validate()

    def test_incomplete_failed_preview_fork_wrong_workflow_and_commit_are_rejected(self):
        for field, value in (
            ("status", "in_progress"),
            ("conclusion", "failure"),
            ("conclusion", "cancelled"),
            ("event", "pull_request"),
            ("head_branch", "feature"),
            ("head_sha", "b" * 40),
            ("path", ".github/workflows/other.yml"),
            ("repository", {"full_name": "fork/repo"}),
            ("head_repository", {"full_name": "fork/repo"}),
            ("id", "42"),
        ):
            with self.subTest(field=field, value=value):
                altered = {**self.run, field: value}
                with self.assertRaises(ValueError):
                    release.validate_run(
                        altered, self.jobs, self.repository, self.commit, self.tag, self.tag
                    )

    def test_skipped_missing_duplicate_or_other_run_jobs_are_rejected(self):
        for field, value in (
            ("conclusion", "skipped"),
            ("conclusion", "failure"),
            ("run_id", 43),
            ("head_sha", "b" * 40),
        ):
            with self.subTest(field=field):
                jobs = copy.deepcopy(self.jobs)
                jobs["jobs"][0][field] = value
                with self.assertRaises(ValueError):
                    release.validate_run(
                        self.run, jobs, self.repository, self.commit, self.tag, self.tag
                    )
        for inventory in (
            self.jobs["jobs"][:-1],
            self.jobs["jobs"] + [self.jobs["jobs"][0]],
            [self.jobs["jobs"][0]] * 7,
        ):
            with self.assertRaises(ValueError):
                release.validate_run(
                    self.run, {"jobs": inventory}, self.repository, self.commit, self.tag, self.tag
                )

    def test_canonical_release_tag_and_explicit_exact_confirmation_are_required(self):
        for tag, confirmation in (
            (self.tag, "yes"),
            (self.tag, ""),
            ("v0.1.0-experimental", "v0.1.0-experimental"),
            ("--help", "--help"),
            ("v0.1.0-rc.1; echo bad", self.tag),
        ):
            with self.subTest(tag=tag, confirmation=confirmation), self.assertRaises(ValueError):
                release.validate_run(
                    self.run, self.jobs, self.repository, self.commit, tag, confirmation
                )

    def test_remote_lightweight_and_annotated_tags_are_peeled_and_bound(self):
        commit = {"type": "commit", "sha": self.commit}
        annotated = {"type": "tag", "sha": "b" * 40}
        for objects in ((commit,), (annotated, commit)):
            with self.subTest(objects=objects), patch.object(
                release.package, "run", side_effect=[json.dumps({"object": obj}) for obj in objects]
            ) as call:
                release.check_remote_tag(self.repository, self.tag, self.commit)
                self.assertEqual(call.call_count, len(objects))
        for obj in (
            {"type": "commit", "sha": "b" * 40},
            {"type": "tree", "sha": self.commit},
            annotated,
        ):
            with self.subTest(obj=obj), patch.object(
                release.package, "run", return_value=json.dumps({"object": obj})
            ), self.assertRaises(ValueError):
                release.check_remote_tag(self.repository, self.tag, self.commit)
        with patch.object(
            release.package, "run", side_effect=subprocess.CalledProcessError(1, ["gh", "api"])
        ), self.assertRaises(subprocess.CalledProcessError):
            release.check_remote_tag(self.repository, self.tag, self.commit)

    def test_publication_checks_existing_tag_merged_source_and_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            commit = self.source_fixture(source)
            subprocess.run(
                ["git", "update-ref", "refs/remotes/origin/master", commit], cwd=source, check=True
            )
            # Tags exist only in this disposable fixture, never in the project repository.
            subprocess.run(["git", "tag", self.tag, commit], cwd=source, check=True)
            self.run["head_sha"] = commit
            for job in self.jobs["jobs"]:
                job["head_sha"] = commit
            run_path, jobs_path = root / "run.json", root / "jobs.json"
            run_path.write_text(json.dumps(self.run), encoding="utf8")
            jobs_path.write_text(json.dumps(self.jobs), encoding="utf8")
            args = type(
                "Args",
                (),
                {
                    "run": run_path,
                    "jobs": jobs_path,
                    "repository": self.repository,
                    "tag": self.tag,
                    "confirmation": self.tag,
                },
            )()
            with patch.object(release, "ROOT", source):
                release.check_publication(args)
                args.tag = args.confirmation = "v0.1.0-rc.2"
                subprocess.run(["git", "tag", args.tag, commit], cwd=source, check=True)
                with self.assertRaisesRegex(ValueError, "Tag version"):
                    release.check_publication(args)

    def source_fixture(self, source):
        subprocess.run(["git", "init", "-q", str(source)], check=True)
        (source / "CMakeLists.txt").write_text(
            "project(omp-postgresql VERSION 0.1.0)\nset(PG_RELEASE_CANDIDATE 1)\n", encoding="utf8"
        )
        (source / "docs").mkdir()
        (source / "docs/release-notes.md").write_text(
            "# omp-postgresql 0.1.0-rc.1\n\nTesting release.\n"
            "[API](native-reference.md) [audit](independent-audit.md) [validation](validation.md) "
            "[dependencies](../DEPENDENCIES.md) [installation](installation.md)\n",
            encoding="utf8",
        )
        subprocess.run(["git", "add", "."], cwd=source, check=True)
        subprocess.run(
            [
                "git",
                "-c",
                "user.name=Fixture",
                "-c",
                "user.email=fixture@example.invalid",
                "commit",
                "-qm",
                "Candidate",
            ],
            cwd=source,
            check=True,
        )
        return release.source_identity(source)[0]

    def packages_fixture(self, directory, commit):
        directory.mkdir()
        for platform in ("windows", "linux"):
            files, manifest = payload()
            manifest.update(version="0.1.0-rc.1", commit=commit, platform=platform)
            manifest["build"].update(version=manifest["version"], commit=commit)
            if platform == "linux":
                binary = bytearray(52)
                binary[:6] = b"\x7fELF\x01\x01"
                struct.pack_into("<H", binary, 18, 3)
                del files["components/PostgreSQL.dll"], files["libpq.dll"]
                files.update(
                    {"components/PostgreSQL.so": bytes(binary), "lib/libpq.so.5": bytes(binary)}
                )
                manifest["build"]["system"] = "Linux"
                manifest["dependencies"] = {
                    "components/PostgreSQL.so": {"imports": ["libpq.so.5"]},
                    "lib/libpq.so.5": {"imports": ["libc.so.6"]},
                }
                del manifest["libpq_build"]
            manifest["files"] = {name: release.package.digest(data) for name, data in files.items()}
            manifest["archive"] = release.package.archive_name(
                manifest["version"], commit, platform
            )
            archive = directory / manifest["archive"]
            with zipfile.ZipFile(archive, "w") as output:
                for name, data in files.items():
                    output.writestr(f"omp-postgresql/{name}", data)
                output.writestr("omp-postgresql/manifest.json", json.dumps(manifest))
            archive.with_suffix(".zip.sha256").write_text(
                f"{release.package.digest(archive.read_bytes())}  {archive.name}\n", encoding="utf8"
            )

    def test_two_real_archive_gates_source_inventory_hashes_and_repeatability(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            commit = self.source_fixture(source)
            inputs = root / "inputs"
            self.packages_fixture(inputs, commit)
            # Untracked files outside install scopes must never enter the source archive.
            (source / "private.txt").write_text("FAKE_PRIVATE_SENTINEL", encoding="utf8")
            first, second = root / "first", root / "second"
            with patch.dict(os.environ, {"TZ": "UTC+5"}):
                inventory = release.prepare(inputs, first, source)
            with patch.dict(os.environ, {"TZ": "UTC-9"}):
                release.prepare(inputs, second, source)
            self.assertEqual(inventory["commit"], commit)
            self.assertEqual(set(inventory["distributions"]), {"windows", "linux"})
            notes = (first / "RELEASE-NOTES.md").read_text(encoding="utf8")
            prefix = f"https://github.com/itskoleban/omp-postgresql/blob/{commit}/"
            for path in (
                "docs/native-reference.md",
                "docs/independent-audit.md",
                "docs/validation.md",
                "DEPENDENCIES.md",
                "docs/installation.md",
            ):
                self.assertIn(f"]({prefix}{path})", notes)
            self.assertIn("](native-reference.md)", (source / "docs/release-notes.md").read_text())
            self.assertEqual(
                {p.name: p.read_bytes() for p in first.iterdir()},
                {p.name: p.read_bytes() for p in second.iterdir()},
            )
            checksum_lines = (first / "SHA256SUMS").read_text().splitlines()
            # Portable byte order: pathlib ordering folds case on Windows.
            checksum_names = [line.split("  ")[1] for line in checksum_lines]
            self.assertEqual(checksum_names, sorted(checksum_names))
            for line in checksum_lines:
                digest, name = line.split("  ")
                self.assertEqual(digest, release.package.digest((first / name).read_bytes()))
            with zipfile.ZipFile(first / inventory["source_archive"]) as archive:
                self.assertEqual(
                    set(archive.namelist()),
                    {
                        "omp-postgresql/",
                        "omp-postgresql/CMakeLists.txt",
                        "omp-postgresql/docs/",
                        "omp-postgresql/docs/release-notes.md",
                    },
                )
            with self.assertRaises(FileExistsError):
                release.prepare(inputs, first, source)

    def test_missing_extra_old_commit_and_altered_archives_are_rejected_before_output(self):
        for mode in ("missing", "extra", "old-commit", "altered"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                source = root / "source"
                commit = self.source_fixture(source)
                inputs = root / "inputs"
                self.packages_fixture(inputs, "b" * 40 if mode == "old-commit" else commit)
                archive = next(inputs.glob("*.zip"))
                if mode == "missing":
                    archive.unlink()
                elif mode == "extra":
                    (inputs / "secret.txt").write_text("FAKE_PRIVATE_SENTINEL", encoding="utf8")
                elif mode == "altered":
                    with archive.open("ab") as output:
                        output.write(b"altered")
                with self.assertRaises(ValueError):
                    release.prepare(inputs, root / "output", source)
                self.assertFalse((root / "output").exists())


if __name__ == "__main__":
    unittest.main()
