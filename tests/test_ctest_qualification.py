"""Exercise qualification against real CTest, including exit-zero skipped tests."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from ctest_qualification import qualify_ctest, verify_report


class QualificationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="omp-pg-ctest-")
        self.addCleanup(temporary.cleanup)
        self.build = Path(temporary.name)
        self.testfile = self.build / "CTestTestfile.cmake"
        self.testfile.write_text("", encoding="utf8")
        self.report = self.build / "Testing" / "Temporary" / "qualification.xml"
        self.results = []

    def add_case(self, name, code="pass", properties=""):
        with self.testfile.open("a", encoding="utf8") as output:
            output.write(f'add_test({name} "{Path(sys.executable).as_posix()}" -c [=[{code}]=])\n')
            if properties:
                output.write(f"set_tests_properties({name} PROPERTIES {properties})\n")

    def run_command(self, command, **kwargs):
        kwargs.setdefault("stdout", subprocess.PIPE)
        kwargs.setdefault("stderr", subprocess.STDOUT)
        kwargs.setdefault("text", True)
        if os.name == "nt":
            kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
        result = subprocess.run(command, timeout=20, **kwargs)
        self.results.append(result)
        result.check_returncode()
        return result

    def qualify(self):
        qualify_ctest(self.build, "Release", dict(os.environ), self.run_command)

    def test_successful_inventory(self):
        self.add_case("unit")
        self.add_case("integration")
        self.qualify()
        self.assertEqual(verify_report(self.report, ["unit", "integration"]), 2)

    def test_focused_inventory_is_explicit_and_still_rejects_skips(self):
        self.add_case("other", "raise SystemExit(1)")
        self.add_case("network", "raise SystemExit(77)", "SKIP_RETURN_CODE 77")
        with self.assertRaisesRegex(RuntimeError, "unsuccessful tests: network"):
            qualify_ctest(self.build, "Release", dict(os.environ), self.run_command, "^network$")
        self.assertIn("-R", self.results[-1].args)

    def test_skip_return_code_is_rejected_despite_ctest_success(self):
        self.add_case("unit")
        self.add_case("integration", "raise SystemExit(77)", "SKIP_RETURN_CODE 77")
        with self.assertRaisesRegex(RuntimeError, "unsuccessful tests: integration"):
            self.qualify()
        self.assertEqual(self.results[-1].returncode, 0)

    def test_disabled_case_is_rejected_despite_ctest_success(self):
        self.add_case("unit")
        self.add_case("integration", properties="DISABLED TRUE")
        with self.assertRaisesRegex(RuntimeError, "unsuccessful tests: integration"):
            self.qualify()
        self.assertEqual(self.results[-1].returncode, 0)

    def test_skip_output_is_rejected_despite_ctest_success(self):
        self.add_case(
            "integration", "print('SKIP fixture missing')", 'SKIP_REGULAR_EXPRESSION "SKIP"'
        )
        with self.assertRaisesRegex(RuntimeError, "unsuccessful tests: integration"):
            self.qualify()
        self.assertEqual(self.results[-1].returncode, 0)

    def test_failures_and_timeouts_remain_failures(self):
        self.add_case("failure", "raise SystemExit(1)")
        self.add_case("timeout", "import time; time.sleep(10)", "TIMEOUT 0.1")
        with self.assertRaises(subprocess.CalledProcessError):
            self.qualify()
        self.assertTrue(self.report.is_file(), "Failed run must retain its report")

    def test_missing_executable_remains_failure(self):
        self.testfile.write_text('add_test(missing "does-not-exist-omp-pg")\n', encoding="utf8")
        with self.assertRaises(subprocess.CalledProcessError):
            self.qualify()

    def test_empty_inventory_rejects_and_removes_stale_report(self):
        self.report.parent.mkdir(parents=True)
        self.report.write_text('<testsuite><testcase name="old" status="run"/></testsuite>')
        with self.assertRaisesRegex(RuntimeError, "no configured tests"):
            self.qualify()
        self.assertFalse(self.report.exists())

    def test_missing_duplicate_and_unknown_cases_rejected(self):
        self.report.parent.mkdir(parents=True)
        for cases in (
            '<testcase name="unit" status="run"/>',
            '<testcase name="unit" status="run"/>' * 2,
            '<testcase name="unit" status="run"/><testcase name="other" status="run"/>',
        ):
            with self.subTest(cases=cases):
                self.report.write_text(f"<testsuite>{cases}</testsuite>")
                with self.assertRaisesRegex(RuntimeError, "inventory"):
                    verify_report(self.report, ["unit", "integration"])

    def test_invalid_or_absent_report_rejected(self):
        with self.assertRaises(FileNotFoundError):
            verify_report(self.report, ["unit"])
        self.report.parent.mkdir(parents=True)
        self.report.write_text("<other/>")
        with self.assertRaisesRegex(RuntimeError, "structure"):
            verify_report(self.report, ["unit"])

    def test_nonpassing_case_statuses_and_failure_elements_rejected(self):
        self.report.parent.mkdir(parents=True)
        for case in (
            '<testcase name="unit"/>',
            '<testcase name="unit" status="notrun"/>',
            '<testcase name="unit" status="run"><failure/></testcase>',
            '<testcase name="unit" status="run"><error/></testcase>',
            '<testcase name="unit" status="run"><skipped/></testcase>',
        ):
            with self.subTest(case=case):
                self.report.write_text(f"<testsuite>{case}</testsuite>")
                with self.assertRaisesRegex(RuntimeError, "unsuccessful"):
                    verify_report(self.report, ["unit"])


if __name__ == "__main__":
    unittest.main()
