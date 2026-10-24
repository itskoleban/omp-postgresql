"""Control-oracle regressions; synthetic exits never qualify a sanitizer run."""

import subprocess
import unittest
from check_tsan_control import accepted


class TSanControlTests(unittest.TestCase):
    def test_only_real_diagnostic_and_expected_exit_qualify_race_detection(self):
        marker = "WARNING: ThreadSanitizer: data race"
        for code, stderr, expected in (
            (66, marker, True),
            (0, marker, False),
            (66, "", False),
            (-11, "", False),
            (1, "FATAL: ThreadSanitizer", False),
        ):
            with self.subTest(code=code, stderr=stderr):
                self.assertEqual(
                    accepted(subprocess.CompletedProcess([], code, "", stderr), True), expected
                )

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


if __name__ == "__main__":
    unittest.main()
