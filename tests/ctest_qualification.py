"""Require every configured CTest case to execute successfully in qualification runs."""

import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def verify_report(report, expected):
    """Validate CTest's JUnit output, including cases that do not affect its exit code."""
    if not expected or len(expected) != len(set(expected)):
        raise RuntimeError("CTest qualification requires a nonempty, unique test inventory")
    root = ET.parse(report).getroot()
    if root.tag != "testsuite":
        raise RuntimeError("Unexpected CTest JUnit report structure")
    cases = root.findall("testcase")
    names = [case.get("name") for case in cases]
    if len(names) != len(set(names)) or set(names) != set(expected):
        raise RuntimeError("CTest report does not match the configured test inventory")
    incomplete = [
        case.get("name")
        for case in cases
        if case.get("status") != "run"
        or any(case.find(tag) is not None for tag in ("skipped", "failure", "error"))
    ]
    if incomplete:
        raise RuntimeError(
            "CTest qualification rejected unsuccessful tests: " + ", ".join(incomplete)
        )
    return len(cases)


def qualify_ctest(build, config, env, run, test_regex=None):
    """Run CTest using the caller's logged subprocess runner and disposable environment."""
    build = Path(build).resolve()
    report = build / "Testing" / "Temporary" / "qualification.xml"
    report.parent.mkdir(parents=True, exist_ok=True)
    # Never accept an earlier report if CTest fails before writing a new one.
    report.unlink(missing_ok=True)
    command = ["ctest", "--test-dir", str(build), "-C", config]
    if test_regex is not None:
        command.extend(("-R", test_regex))
    inventory = run([*command, "--show-only=json-v1"], env=env, stdout=subprocess.PIPE, text=True)
    expected = [test["name"] for test in json.loads(inventory.stdout)["tests"]]
    if not expected:
        raise RuntimeError("CTest qualification found no configured tests")
    run(
        [*command, "--no-tests=error", "--output-on-failure", "--output-junit", str(report)],
        env=env,
    )
    count = verify_report(report, expected)
    print(f"CTest qualification: {count} tests executed successfully; report: {report}", flush=True)
