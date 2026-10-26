"""Installed-byte qualification gates; these do not claim a network execution."""
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parents[1] / "scripts"))
from test_package import payload
from run_real_session_ci import package_inputs
import run_real_session_ci
from run_real_sessions import Suite


def digest(data):
    return hashlib.sha256(data).hexdigest()


def installed_fixture(root, system):
    files, manifest = payload()
    if system == "linux":
        binary = bytearray(100)
        binary[:6] = b"\x7fELF\x01\x01"
        struct.pack_into("<H", binary, 18, 3)
        del files["components/omp-postgresql.dll"], files["libpq.dll"]
        files["components/omp-postgresql.so"] = bytes(binary)
        files["lib/libpq.so.5"] = bytes(binary)
        manifest.update(platform="linux", archive=manifest["archive"].replace("windows", "linux"))
        manifest["build"]["system"] = "Linux"
        manifest["dependencies"] = {"components/omp-postgresql.so": {"imports": ["libpq.so.5"]},
                                    "lib/libpq.so.5": {"imports": ["libc.so.6"]}}
        manifest["files"] = {name: digest(data) for name, data in files.items()}
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf8")
    return manifest


class PackagedRealSessionTests(unittest.TestCase):
    def test_both_platforms_select_installed_component_all_runtimes_and_include(self):
        for system in ("linux", "windows"):
            with self.subTest(system=system), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                manifest = installed_fixture(root, system)
                component, runtime, include, identity = package_inputs(root, system, manifest["commit"])
                self.assertEqual(component.parent, root / "components")
                self.assertEqual(include, root / "qawno/include")
                self.assertEqual({p.relative_to(root).as_posix() for p in runtime},
                                 set(manifest["dependencies"]) - {component.relative_to(root).as_posix()})
                self.assertEqual(identity["component_sha256"], digest(component.read_bytes()))
                self.assertEqual(identity["runtime_sha256"], {p.name: digest(p.read_bytes()) for p in runtime})
                self.assertEqual(identity["manifest_sha256"], digest((root / "manifest.json").read_bytes()))

    def test_modified_component_runtime_include_and_extra_files_are_rejected(self):
        for name in ("components/omp-postgresql.dll", "libpq.dll", "qawno/include/omp-postgresql.inc", "extra.dll"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                manifest = installed_fixture(root, "windows")
                (root / name).write_bytes(b"altered")
                with self.assertRaises(ValueError):
                    package_inputs(root, "windows", manifest["commit"])

    def test_stale_source_or_other_platform_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = installed_fixture(root, "windows")
            for system, commit in (("linux", manifest["commit"]), ("windows", "b" * 40)):
                with self.subTest(system=system, commit=commit), self.assertRaisesRegex(ValueError, "platform/source commit"):
                    package_inputs(root, system, commit)

    def test_cli_uses_installed_include_and_runtimes_for_the_complete_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            system = "windows" if os.name == "nt" else "linux"
            manifest = installed_fixture(root / "package", system)
            archive = root / "provider.zip"
            with zipfile.ZipFile(archive, "w") as provider:
                provider.writestr("includes/fixture.inc", "// Fixture only\n")
            output = root / "run"
            argv = ["runner", "--output", str(output), "--package-root", str(root / "package"),
                    "--client", str(root / "client/build/fixture"), "--openssl", "fixture-openssl"]
            for name in ("server", "compiler", "stdlib", "pawn-include"):
                argv += ["--" + name, str(root / name)]
            commands = []
            def download(url, target):
                Path(target).write_bytes(archive.read_bytes())
            def run(command, **kwargs):
                commands.append(command)
            with patch.object(sys, "argv", argv), patch.object(run_real_session_ci.subprocess, "check_output", return_value=manifest["commit"]), patch.object(run_real_session_ci.subprocess, "run", side_effect=run), patch.object(run_real_session_ci.urllib.request, "urlretrieve", side_effect=download), patch.object(run_real_session_ci, "PROVIDER_HASHES", {system: digest(archive.read_bytes())}):
                run_real_session_ci.main()
            config = json.loads((output / "config.json").read_text())
            self.assertNotIn("cases", config)
            self.assertEqual(config["package"]["commit"], manifest["commit"])
            self.assertEqual(Path(config["component"]).parent, root / "package/components")
            self.assertEqual({Path(p).relative_to(root / "package").as_posix() for p in config["runtime"]},
                             set(manifest["dependencies"]) - {Path(config["component"]).relative_to(root / "package").as_posix()})
            for command in commands[:2]:
                self.assertIn("-i" + str(root / "package/qawno/include"), command)
                self.assertNotIn("-i" + str(Path(__file__).parents[1] / "pawn"), command)
            self.assertIn("--only-real-sessions", commands[-1])
            self.assertEqual(commands[-1][-2:], ["--openssl", "fixture-openssl"])

    def test_suite_binds_package_manifest_and_rejects_changed_inputs_before_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = installed_fixture(root / "package", "windows")
            component, runtime, _, identity = package_inputs(root / "package", "windows", manifest["commit"])
            for name in ("gamemode", "client", "provider", "omp-server", "omp-server.exe"):
                (root / name).write_bytes(b"fixture")
            suite = Suite.__new__(Suite)
            suite.config = {key: str(root / key) for key in ("gamemode", "client", "provider")}
            suite.config.update(component=str(component), runtime=[str(p) for p in runtime],
                                server=str(root), package=identity)
            suite.output = root
            suite.results, suite.sql_results = [], []
            suite.password, suite.wrong = "fixture-secret-one", "fixture-secret-two"
            source = {"source_commit": manifest["commit"], "source_dirty": False, "tracked_source_modified": False}
            with patch("run_real_sessions.capture_source", return_value=source):
                suite.provenance = None
                suite.save()
                before = (root / "results.json").read_bytes()
                self.assertEqual(json.loads(before)["package"], identity)
                for change in ("component", "runtime", "commit"):
                    with self.subTest(change=change):
                        suite.config["package"] = copy.deepcopy(identity)
                        if change == "commit":
                            suite.config["package"]["commit"] = "b" * 40
                        elif change == "component":
                            suite.config["package"]["component_sha256"] = "0" * 64
                        else:
                            suite.config["package"]["runtime_sha256"][runtime[0].name] = "0" * 64
                        suite.provenance = None
                        with self.assertRaisesRegex(ValueError, "identity changed"):
                            suite.save()
                        self.assertEqual((root / "results.json").read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
