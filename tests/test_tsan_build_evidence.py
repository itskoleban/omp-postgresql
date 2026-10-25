"""Fail closed on absent instrumentation and preserve original/public identities."""

import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from record_tsan_build import EvidenceError, Recorder, compiler_argv, require_instrumentation


class TSanBuildEvidenceTests(unittest.TestCase):
    def test_race_requires_real_undefined_instrumentation_references(self):
        symbols = "\n".join("                 U " + symbol for symbol in (
            "__tsan_init", "__tsan_write4", "__tsan_atomic32_load", "__tsan_atomic32_store"
        ))
        self.assertEqual(len(require_instrumentation(symbols, True)), 4)
        for symbol in ("__tsan_init", "__tsan_write4", "__tsan_atomic32_load", "__tsan_atomic32_store"):
            with self.subTest(missing=symbol), self.assertRaises(EvidenceError):
                require_instrumentation(symbols.replace("U " + symbol, "T " + symbol), True)
        self.assertEqual(require_instrumentation("                 U __tsan_init\n", False), ["__tsan_init"])

    def test_ninja_link_shell_wrapping_is_parsed_without_execution(self):
        command = ': && /usr/bin/clang++-19 -g -fsanitize=thread "object name.o" -o fixture && :'
        self.assertEqual(compiler_argv(command, "/usr/bin/clang++-19"), [
            "/usr/bin/clang++-19", "-g", "-fsanitize=thread", "object name.o", "-o", "fixture"
        ])
        with self.assertRaises(EvidenceError):
            compiler_argv(command, "/different/compiler")

    def test_original_and_sanitized_command_streams_have_separate_identities(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            build = source / "build"
            recorder = Recorder(source, build)
            raw = (str(source) + "/tests/fixture.cpp " + str(build) + "/fixture.o\n").encode()
            with patch("record_tsan_build.subprocess.run", return_value=subprocess.CompletedProcess([], 0, raw, b"")):
                recorder.command("fixture", ["tool", str(source / "tests/fixture.cpp")])
            stream = recorder.manifest["commands"][0]["stdout"]
            public = (recorder.output / stream["sanitized_path"]).read_bytes()
            self.assertEqual(stream["original_sha256"], hashlib.sha256(raw).hexdigest())
            self.assertEqual(stream["sanitized_sha256"], hashlib.sha256(public).hexdigest())
            self.assertNotEqual(stream["original_sha256"], stream["sanitized_sha256"])
            self.assertNotIn(str(source).encode(), public)
            self.assertIn(b"<SOURCE>/tests/fixture.cpp <BUILD>/fixture.o", public)

    def test_required_command_failure_preserves_negative_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            recorder = Recorder(source, source / "build")
            with patch("record_tsan_build.subprocess.run", return_value=subprocess.CompletedProcess([], 1, b"", b"missing symbols")):
                with self.assertRaises(EvidenceError):
                    recorder.command("fixture", ["nm", "-u", "fixture.o"])
            self.assertEqual(recorder.manifest["status"], "FAIL")
            self.assertEqual(recorder.manifest["commands"][0]["returncode"], 1)
            self.assertEqual((recorder.output / "commands/fixture.stderr.txt").read_bytes(), b"missing symbols")

    def test_hidden_capture_paths_fail_before_upload_can_omit_them(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            workflow = source / ".github/workflows/ci.yml"
            workflow.parent.mkdir(parents=True)
            workflow.write_bytes(b"name: fixture\n")
            recorder = Recorder(source, source / "build")
            with self.assertRaises(EvidenceError):
                recorder.copy_input(workflow, Path("source/.github/workflows/ci.yml"), "source-input")
            row = recorder.copy_input(workflow, Path("source/github-workflows/ci.yml"), "source-input")
            self.assertTrue(row["copy_is_byte_identical"])
            self.assertEqual(row["sha256"], hashlib.sha256(workflow.read_bytes()).hexdigest())
            self.assertIn(".github/workflows/ci.yml", row["input_path"].replace("\\", "/"))
            self.assertNotIn("/.github/", row["copy_path"])


if __name__ == "__main__":
    unittest.main()
