"""The supervisor rejects false durations and preserves negative preflight evidence."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

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
        self.git = patch.object(audit_long.subprocess, "check_output", side_effect=["a" * 40, ""])
        self.git.start()
        self.addCleanup(self.git.stop)
        system = patch.object(audit_long.platform, "platform", return_value="test-platform")
        system.start()
        self.addCleanup(system.stop)

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
