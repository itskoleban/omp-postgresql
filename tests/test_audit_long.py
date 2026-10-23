"""The external runner must reject false duration claims before starting workloads."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


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


if __name__ == "__main__":
    unittest.main()
