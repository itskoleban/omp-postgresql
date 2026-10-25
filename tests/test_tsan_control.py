"""Control-oracle regressions; synthetic exits never qualify a sanitizer run."""

import subprocess
import sys
import tempfile
import unittest
import io
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch
from check_tsan_control import accepted, main, mmap_entropy


# Parser fixtures exercise report structure; they are never sanitizer qualification.
REPORT = """==================
WARNING: ThreadSanitizer: data race (pid=123)
  Write of size 4 at 0x1234 by thread T2:
    #0 writer /isolated/tests/tsan_race_control.cpp:12:9 (pg_tsan_race_control+0x123)

  Previous write of size 4 at 0x1234 by thread T1:
    #0 writer /isolated/tests/tsan_race_control.cpp:12:9 (pg_tsan_race_control+0x123)

SUMMARY: ThreadSanitizer: data race /isolated/tests/tsan_race_control.cpp:12:9 in writer
==================
"""


class TSanControlTests(unittest.TestCase):
    def test_optional_proc_metadata_cannot_prevent_detector_execution(self):
        with patch(
            "check_tsan_control.Path.read_text", side_effect=PermissionError(13, "fixture denial")
        ):
            self.assertEqual(mmap_entropy(), "UNAVAILABLE errno=13")
        with patch("check_tsan_control.Path.read_text", return_value="32\n"):
            self.assertEqual(mmap_entropy(), "32")

    def test_only_real_diagnostic_and_expected_exit_qualify_race_detection(self):
        marker = "WARNING: ThreadSanitizer: data race"
        for code, stderr, expected in (
            (66, REPORT, True),
            (66, marker, False),
            (0, marker, False),
            (0, REPORT, False),
            (66, "", False),
            (-11, "", False),
            (-11, REPORT, False),
            (1, "FATAL: ThreadSanitizer", False),
            (66, REPORT + "FATAL: ThreadSanitizer: unexpected memory mapping", False),
            (66, REPORT.replace("thread T1", "thread T2"), False),
            (66, REPORT.replace("Previous write of size 4 at 0x1234", "Previous write of size 4 at 0x5678"), False),
            (66, REPORT.replace("tsan_race_control.cpp", "unrelated.cpp"), False),
            (66, REPORT.replace("Previous write", "Previous read").replace("  Write of size", "  Read of size"), False),
        ):
            with self.subTest(code=code, stderr=stderr):
                self.assertEqual(
                    accepted(subprocess.CompletedProcess([], code, "", stderr), True), expected
                )

    def test_fake_fixture_diagnostic_is_rejected_by_command(self):
        stdout = io.StringIO()
        with patch("check_tsan_control.Path.read_bytes", return_value=b"fixture"), patch(
            "check_tsan_control.subprocess.run",
            return_value=subprocess.CompletedProcess([], 66, "", "WARNING: ThreadSanitizer: data race\n"),
        ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
            with patch("sys.argv", ["check_tsan_control.py", "fake-fixture"]):
                with self.assertRaises(SystemExit) as failure:
                    main()
        self.assertIn("1 independent executions", str(failure.exception))
        self.assertIn("result=FAIL", stdout.getvalue())

    def test_timeout_preserves_partial_streams_and_next_required_attempt(self):
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch("check_tsan_control.Path.read_bytes", return_value=b"fixture"), patch(
            "check_tsan_control.subprocess.run",
            side_effect=[
                subprocess.TimeoutExpired(["fixture"], 15, output=b"partial stdout\n", stderr=b"partial stderr\n"),
                subprocess.CompletedProcess([], 66, "", REPORT),
            ],
        ) as execute, redirect_stdout(stdout), redirect_stderr(stderr):
            with patch("sys.argv", ["check_tsan_control.py", "fixture", "--runs", "2"]):
                with self.assertRaises(SystemExit) as failure:
                    main()
        self.assertEqual(execute.call_count, 2)
        self.assertIn("1 independent executions", str(failure.exception))
        self.assertIn("iteration=1/2 returncode=TIMEOUT", stdout.getvalue())
        self.assertIn("iteration=2/2 returncode=66", stdout.getvalue())
        self.assertIn("partial stdout", stdout.getvalue())
        self.assertIn("partial stderr", stderr.getvalue())

    def test_process_emitting_fake_warning_and_exit_66_does_not_qualify(self):
        execute = subprocess.run
        stdout = io.StringIO()
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "fake.py"
            fixture.write_text(
                'import sys\nprint("WARNING: ThreadSanitizer: data race", file=sys.stderr)\n'
                'raise SystemExit(66)\n',
                encoding="utf-8",
            )
            # Use the current Python for this portable negative process; the
            # detector command itself always executes the instrumented binary.
            with patch(
                "check_tsan_control.subprocess.run",
                side_effect=lambda argv, **kwargs: execute([sys.executable, *argv], **kwargs),
            ), patch("sys.argv", ["check_tsan_control.py", str(fixture)]), redirect_stdout(
                stdout
            ), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as failure:
                    main()
        self.assertIn("1 independent executions", str(failure.exception))
        self.assertIn("returncode=66", stdout.getvalue())
        self.assertIn("result=FAIL", stdout.getvalue())

    def test_launch_error_is_an_independent_failure(self):
        stdout = io.StringIO()
        with patch("check_tsan_control.Path.read_bytes", return_value=b"fixture"), patch(
            "check_tsan_control.subprocess.run", side_effect=OSError(8, "fixture format")
        ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
            with patch("sys.argv", ["check_tsan_control.py", "fixture"]):
                with self.assertRaises(SystemExit) as failure:
                    main()
        self.assertIn("1 independent executions", str(failure.exception))
        self.assertIn("returncode=LAUNCH_ERROR errno=8", stdout.getvalue())

    def test_startup_requires_main_marker_and_zero_exit(self):
        marker = "TSan startup control entered main"
        for code, stdout, expected in (
            (0, marker, True),
            (0, "", False),
            (-11, "", False),
            (66, marker, False),
        ):
            with self.subTest(code=code):
                self.assertEqual(
                    accepted(subprocess.CompletedProcess([], code, stdout, ""), False), expected
                )

    def test_startup_rejects_sanitizer_failure_despite_main_marker(self):
        result = subprocess.CompletedProcess(
            [], 0, "TSan startup control entered main\n", "FATAL: ThreadSanitizer: unexpected memory mapping"
        )
        self.assertFalse(accepted(result, False))


if __name__ == "__main__":
    unittest.main()
