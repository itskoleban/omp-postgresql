"""Stable publication must preserve the RC route and reject experimental relabelling."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

import test_release as fixtures
release = fixtures.release
from test_package import payload

class StableReleaseTests(unittest.TestCase):
    def test_exact_main_run_accepts_stable_and_rc_but_rejects_other_channels(self):
        fixture = fixtures.ReleaseTests()
        fixture.setUp()
        for tag in ("v0.1.0", "v0.1.0-rc.1", "v1.0.0-rc.12"):
            release.validate_run(fixture.run, fixture.jobs, fixture.repository,
                                 fixture.commit, tag, tag)
        for tag in ("v0.1.0-experimental", "v0.1.0-rc.0", "v0.1.0-rc.01",
                    "v00.1.0", "v0.01.0", "v0.1.00", "v0.1.0+build", "v0x1x0"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.publication_channel(tag)
        with self.assertRaises(ValueError):
            release.validate_run(fixture.run, fixture.jobs, fixture.repository,
                                 fixture.commit, "v0.1.0", "yes")

    def test_channel_metadata_is_required_on_both_manifest_and_build(self):
        files, historical = payload()
        # Historical bare versions remain experimental and are still verifiable.
        release.package.validate_payload(files, historical)
        stable = copy.deepcopy(historical)
        stable["release_channel"] = stable["build"]["release_channel"] = "stable"
        stable["archive"] = release.package.archive_name("0.1.0", stable["commit"], "windows", "stable")
        release.package.validate_payload(files, stable)
        self.assertNotIn("experimental", stable["archive"])
        for target in ("manifest", "build"):
            altered = copy.deepcopy(stable)
            del (altered if target == "manifest" else altered["build"])["release_channel"]
            with self.subTest(target=target), self.assertRaises(ValueError):
                release.package.validate_payload(files, altered)
        for version, channel in (("0.1.0-rc.1", "stable"), ("0.1.0", "rc"),
                                 ("0.1.0", "unknown")):
            with self.assertRaises(ValueError):
                release.package.archive_name(version, "a" * 40, "linux", channel)

    def test_stable_source_bundle_is_repeatable_and_rejects_legacy_experimental(self):
        fixture = fixtures.ReleaseTests()
        fixture.setUp()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            fixture.source_fixture(source)
            cmake = source / "CMakeLists.txt"
            cmake.write_text(cmake.read_text().replace("PG_RELEASE_CANDIDATE 1",
                                                       "PG_RELEASE_CANDIDATE 0"), encoding="utf8")
            notes = source / "docs/release-notes.md"
            notes.write_text(notes.read_text().replace("0.1.0-rc.1", "0.1.0"), encoding="utf8")
            subprocess.run(["git", "add", "."], cwd=source, check=True)
            subprocess.run(["git", "-c", "user.name=Fixture", "-c",
                            "user.email=fixture@example.invalid", "commit", "-qm", "Stable"],
                           cwd=source, check=True)
            commit, version = release.source_identity(source)
            self.assertEqual(version, "0.1.0")
            inputs = root / "inputs"
            fixture.packages_fixture(inputs, commit)
            for old in list(inputs.glob("*.zip")):
                with zipfile.ZipFile(old) as archive:
                    content = {name: archive.read(name) for name in archive.namelist()}
                manifest = json.loads(content["omp-postgresql/manifest.json"])
                manifest["version"] = manifest["build"]["version"] = version
                manifest["release_channel"] = manifest["build"]["release_channel"] = "stable"
                manifest["archive"] = release.package.archive_name(version, commit,
                                                                    manifest["platform"], "stable")
                content["omp-postgresql/manifest.json"] = json.dumps(manifest).encode()
                destination = inputs / manifest["archive"]
                with zipfile.ZipFile(destination, "w") as archive:
                    for name, data in content.items():
                        archive.writestr(name, data)
                destination.with_suffix(".zip.sha256").write_text(
                    f"{release.package.digest(destination.read_bytes())}  {destination.name}\n")
                old.unlink()
                old.with_suffix(".zip.sha256").unlink()
            first, second = root / "first", root / "second"
            inventory = release.prepare(inputs, first, source)
            release.prepare(inputs, second, source)
            self.assertEqual(inventory["release_channel"], "stable")
            self.assertFalse(inventory["prerelease"])
            self.assertEqual(inventory["commit"], commit)
            self.assertEqual({p.name: p.read_bytes() for p in first.iterdir()},
                             {p.name: p.read_bytes() for p in second.iterdir()})
            # A historical experimental package cannot satisfy a stable source gate.
            archive = next(inputs.glob("*.zip"))
            archive.rename(archive.with_name(archive.name.replace("0.1.0.", "0.1.0-experimental.")))
            with self.assertRaises(ValueError):
                release.prepare(inputs, root / "rejected", source)
            self.assertFalse((root / "rejected").exists())

if __name__ == "__main__":
    unittest.main()
