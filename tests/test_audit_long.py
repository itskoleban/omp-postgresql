"""The supervisor rejects false durations and preserves negative preflight evidence."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).parents[1] / "scripts"))
import audit_long


class AuditLongTests(unittest.TestCase):
    def test_short_build_cannot_claim_requested_soak_duration(self):
        with tempfile.TemporaryDirectory(prefix="omp-pg-long-gate-") as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            (build / "CMakeCache.txt").write_text("PG_STRESS_SECONDS:STRING=0\n", encoding="utf8")
            output = root / "evidence"
            result = subprocess.run(
                [sys.executable, str(Path(__file__).resolve().parents[1] / "scripts/audit_long.py"),
                 "--build", str(build), "--output", str(output), "--seconds", "120"],
                capture_output=True, text=True, timeout=10,
                creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("refusing a short false pass", result.stderr)
            self.assertFalse(output.exists(), "Rejected request created workload state")

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="omp-pg-audit-supervisor-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text("PG_STRESS_SECONDS:STRING=120\n")
        self.output = self.root / "evidence"
        self.argv = ["audit_long.py", "--build", str(self.build), "--output", str(self.output),
                     "--seconds", "120"]
        self.git = patch.object(audit_long.subprocess, "check_output",
                                side_effect=lambda command, **kwargs: "a" * 40 if command[1] == "rev-parse" else "")
        self.git.start()
        self.addCleanup(self.git.stop)
        system = patch.object(audit_long.platform, "platform", return_value="test-platform")
        system.start()
        self.addCleanup(system.stop)

    def stage_fixture(self, codes, mutate=None):
        suffix = ".exe" if os.name == "nt" else ""
        for name in ("pg_audit_fuzz", "pg_stress_tests"):
            (self.build / (name + suffix)).write_bytes(b"fixture executable")
        self.launched = []
        def launch(command, **kwargs):
            self.launched.append((command, kwargs))
            code = codes[len(self.launched) - 1]
            log = self.build / "Testing/Temporary"
            log.mkdir(parents=True, exist_ok=True)
            (log / "LastTest.log").write_text("retained stage " + str(len(self.launched)))
            (log / "qualification.xml").write_text("<testsuite/>")
            if mutate and len(self.launched) == 2:
                (self.build / ("pg_stress_tests" + suffix)).write_bytes(b"changed after preflight")
            return SimpleNamespace(wait=lambda timeout: code)
        return launch

    def successful_tools(self):
        return patch.object(audit_long.subprocess, "run",
                            return_value=subprocess.CompletedProcess([], 0, "version", ""))

    def test_fuzz_and_bounded_checks_precede_the_final_load(self):
        with patch.object(sys, "argv", self.argv), self.successful_tools(), patch.object(
            audit_long.subprocess, "Popen", side_effect=self.stage_fixture([0, 0, 0])
        ), patch.object(audit_long, "analyze", return_value=({"measured_load_seconds": 120}, "csv")):
            audit_long.main()
        manifest = self.manifest()
        self.assertEqual([stage["name"] for stage in manifest["stages"]],
                         ["extended-fuzz", "bounded-preflight", "integration-soak"])
        self.assertEqual(self.launched[0][1]["env"]["OMP_PG_FUZZ_CASES"], "100000")
        self.assertIn("--preflight-only", self.launched[1][0])
        self.assertIn("--doc-sql", self.launched[1][0])
        self.assertIn("--soak-only", self.launched[2][0])
        self.assertNotIn("--doc-sql", self.launched[2][0])
        self.assertEqual(Path(self.launched[0][1]["cwd"]), self.output / "extended-fuzz")
        self.assertEqual(manifest["status"], "PASS")
        self.assertTrue(manifest["campaign_started"])
        self.assertEqual((self.output / "bounded-preflight-LastTest.log").read_text(), "retained stage 2")
        self.assertEqual((self.output / "integration-soak-LastTest.log").read_text(), "retained stage 3")

    def test_failed_fuzz_or_bounded_check_prevents_load_and_retains_failure(self):
        for codes in ([1], [0, 2]):
            with self.subTest(codes=codes):
                self.output = self.root / ("failure-" + str(len(codes)))
                argv = [*self.argv]
                argv[argv.index("--output") + 1] = str(self.output)
                with patch.object(sys, "argv", argv), self.successful_tools(), patch.object(
                    audit_long.subprocess, "Popen", side_effect=self.stage_fixture(codes)
                ), self.assertRaises(subprocess.CalledProcessError):
                    audit_long.main()
                self.assertEqual(len(self.launched), len(codes))
                self.assertEqual(self.manifest()["status"], "FAIL")
                self.assertEqual(self.manifest()["preflight"]["status"], "FAIL")
                self.assertFalse(self.manifest()["campaign_started"])

    def test_preflight_only_cannot_claim_a_load_campaign(self):
        with patch.object(sys, "argv", self.argv + ["--preflight-only"]), self.successful_tools(), patch.object(
            audit_long.subprocess, "Popen", side_effect=self.stage_fixture([0, 0])
        ):
            audit_long.main()
        self.assertEqual(len(self.launched), 2)
        self.assertEqual(self.manifest()["mode"], "preflight-only")
        self.assertEqual(self.manifest()["preflight"]["status"], "PASS")
        self.assertFalse(self.manifest()["campaign_started"])
        self.assertFalse((self.output / "metrics-summary.json").exists())

    def test_changed_executable_invalidates_preflight_before_load(self):
        with patch.object(sys, "argv", self.argv), self.successful_tools(), patch.object(
            audit_long.subprocess, "Popen", side_effect=self.stage_fixture([0, 0], mutate=True)
        ), self.assertRaisesRegex(RuntimeError, "Executable changed after preflight"):
            audit_long.main()
        self.assertEqual(len(self.launched), 2)
        self.assertEqual(self.manifest()["status"], "FAIL")
        self.assertFalse(self.manifest()["campaign_started"])

    def test_dirty_source_cannot_start_a_load_campaign(self):
        with patch.object(sys, "argv", self.argv), patch.object(
            audit_long.subprocess, "check_output", side_effect=["a" * 40, " M tests/stress.cpp\n"]
        ), patch.object(audit_long.subprocess, "Popen") as launch, \
                self.assertRaisesRegex(RuntimeError, "clean, frozen source"):
            audit_long.main()
        launch.assert_not_called()
        self.assertFalse(self.manifest()["campaign_started"])

    def test_runner_launch_failure_cannot_relabel_a_stale_ctest_report(self):
        launch_fuzz = self.stage_fixture([0])
        log = self.build / "Testing/Temporary"
        def launch(command, **kwargs):
            if self.launched:
                self.assertFalse((log / "LastTest.log").exists())
                self.assertFalse((log / "qualification.xml").exists())
                raise OSError("runner launch failed")
            return launch_fuzz(command, **kwargs)
        with patch.object(sys, "argv", self.argv), self.successful_tools(), patch.object(
            audit_long.subprocess, "Popen", side_effect=launch
        ), self.assertRaisesRegex(OSError, "runner launch failed"):
            audit_long.main()
        self.assertEqual(self.manifest()["status"], "FAIL")
        self.assertFalse((self.output / "bounded-preflight-qualification.xml").exists())

    def test_post_launch_manifest_failure_terminates_and_joins_the_child(self):
        launch_stage = self.stage_fixture([0, 0])
        process = SimpleNamespace(pid=54321, returncode=-9, wait=Mock(return_value=-9))
        def launch(command, **kwargs):
            return process if "--soak-only" in command else launch_stage(command, **kwargs)
        original_write = Path.write_text
        injected = []
        def write(path, data, **kwargs):
            if path.name == "manifest.json" and json.loads(data)["campaign_started"] and not injected:
                injected.append(True)
                raise OSError("injected post-launch ENOSPC")
            return original_write(path, data, **kwargs)
        with patch.object(sys, "argv", self.argv), self.successful_tools(), patch.object(
            audit_long.subprocess, "Popen", side_effect=launch
        ), patch.object(Path, "write_text", write), patch.object(
            audit_long.os, "killpg", create=True
        ) as kill_group, self.assertRaisesRegex(OSError, "post-launch ENOSPC"):
            audit_long.main()
        process.wait.assert_called_once_with(timeout=30)
        if os.name != "nt":
            kill_group.assert_called_once()
        manifest = self.manifest()
        self.assertEqual(manifest["status"], "FAIL")
        self.assertIn("post-launch ENOSPC", manifest["error"])

    def manifest(self):
        self.assertTrue((self.output / "manifest.json").is_file())
        return json.loads((self.output / "manifest.json").read_text(encoding="utf8"))

    def test_missing_preflight_tool_retains_failed_manifest(self):
        with patch.object(sys, "argv", self.argv), patch.object(
            audit_long.subprocess, "run", side_effect=FileNotFoundError("missing cmake")
        ), self.assertRaises(FileNotFoundError):
            audit_long.main()
        manifest = self.manifest()
        self.assertEqual(manifest["status"], "FAIL")
        self.assertEqual(manifest["stages"], [])
        self.assertIn("missing cmake", manifest["error"])
        self.assertGreaterEqual(manifest["ended_unix"], manifest["started_unix"])

    def test_timed_out_preflight_retains_failed_manifest(self):
        with patch.object(sys, "argv", self.argv), patch.object(
            audit_long.subprocess, "run", side_effect=subprocess.TimeoutExpired("cmake", 30)
        ), self.assertRaises(subprocess.TimeoutExpired):
            audit_long.main()
        self.assertEqual(self.manifest()["status"], "FAIL")

    def test_nonzero_version_probe_stops_before_integration_and_retains_output(self):
        with patch.object(sys, "argv", self.argv), patch.object(
            audit_long.subprocess, "run",
            return_value=subprocess.CompletedProcess(["cmake", "--version"], 2, "partial", "broken tool")
        ), patch.object(audit_long.subprocess, "Popen") as launch, \
                self.assertRaises(subprocess.CalledProcessError):
            audit_long.main()
        launch.assert_not_called()
        manifest = self.manifest()
        self.assertEqual(manifest["status"], "FAIL")
        self.assertEqual(manifest["preflight"]["status"], "FAIL")
        self.assertEqual(manifest["tool_versions"]["cmake --version"]["stderr"], "broken tool")

    def test_invalid_build_inventory_retains_failed_manifest(self):
        (self.build / "package-build-info-Release.json").write_text("{")
        with patch.object(sys, "argv", self.argv), patch.object(
            audit_long.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "v1", "")
        ), self.assertRaises(json.JSONDecodeError):
            audit_long.main()
        self.assertEqual(self.manifest()["status"], "FAIL")

    def test_postgres_version_matches_explicit_server_directory(self):
        server = self.root / "postgres-bin"
        with patch.object(sys, "argv", self.argv + ["--postgres-bin", str(server)]), patch.object(
            audit_long.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "v1", "")
        ) as run, patch.object(audit_long.subprocess, "Popen", side_effect=OSError("stop before stage")), \
                self.assertRaises(OSError):
            audit_long.main()
        suffix = ".exe" if os.name == "nt" else ""
        self.assertIn([str(server / ("postgres" + suffix)), "--version"],
                      [call.args[0] for call in run.call_args_list])
        self.assertEqual(self.manifest()["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
