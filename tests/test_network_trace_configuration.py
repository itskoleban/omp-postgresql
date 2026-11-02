"""Inspect actual CMake/CTest configuration for both diagnostic test invocations."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class NetworkTraceConfigurationTests(unittest.TestCase):
    def test_diagnostic_invocations_have_distinct_writable_capture_paths(self):
        source = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="omp-pg-network-config-") as directory:
            build = Path(directory) / "build"
            command = [
                "cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                "-DPG_BUILD_COMPONENT=OFF", "-DPG_SANITIZE=ON",
                "-DPG_NETWORK_DIAGNOSTICS=ON", "-DBUILD_TESTING=ON",
            ]
            if os.name == "nt":
                # Configuration-only Linux target: no generated binary is executed.
                # This exercises the real Linux-only diagnostics gate on Windows too.
                compiler = shutil.which("gcc")
                self.assertIsNotNone(compiler, "Windows configuration control requires GCC")
                prefix = Path(compiler).resolve().parents[1]
                command.extend(("-DCMAKE_SYSTEM_NAME=Linux",
                                "-DCMAKE_C_COMPILER=" + compiler,
                                "-DCMAKE_CXX_COMPILER=" + shutil.which("g++"),
                                "-DPostgreSQL_INCLUDE_DIR=" + str(prefix / "include"),
                                "-DPostgreSQL_LIBRARY=" + str(prefix / "lib/libpq.dll.a")))
            configured = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
            inventory = subprocess.run(
                ["ctest", "--test-dir", str(build), "--show-only=json-v1"],
                capture_output=True, text=True, timeout=20,
            )
            self.assertEqual(inventory.returncode, 0, inventory.stdout + inventory.stderr)
            tests = {test["name"]: test for test in json.loads(inventory.stdout)["tests"]}
            paths = []
            for name in ("network_recovery_integration", "network_boundaries_integration"):
                with self.subTest(test=name):
                    properties = {row["name"]: row["value"] for row in tests[name]["properties"]}
                    environment = dict(value.split("=", 1) for value in properties.get("ENVIRONMENT", []))
                    self.assertIn("OMP_PG_NETWORK_TRACE", environment)
                    trace = Path(environment["OMP_PG_NETWORK_TRACE"])
                    if name == "network_recovery_integration":
                        self.assertEqual(trace.parent, build / "network-diagnostic")
                        self.assertFalse(trace.parent.exists(), "Collector must retain exclusive directory creation")
                        trace.parent.mkdir()  # Same reservation performed by Collector.__init__.
                    else:
                        self.assertEqual(trace.parent, build)
                        self.assertTrue(trace.parent.is_dir(), "Standalone capture parent must already exist")
                    self.assertIn("log_parameter_max_length=0", environment["PGOPTIONS"])
                    # O_EXCL capture creation must be possible without the optional collector.
                    with trace.open("xb") as output:
                        output.write(b"configuration-control\n")
                    paths.append(trace)
            self.assertEqual(len(set(paths)), 2, "Independent processes must not share an O_EXCL trace file")


if __name__ == "__main__":
    unittest.main()
