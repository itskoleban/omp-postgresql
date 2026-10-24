"""Descriptor evidence integrity tests; fixture output is not an EMFILE execution."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).parents[1]/"scripts"))
import probe_sanitizer_descriptors as probe


class DescriptorEvidenceTests(unittest.TestCase):
    def execute_fixture(self, root):
        for name in probe.INPUTS:
            path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b"English fixture input\n")
        output=root/"output"
        def run(command, **kwargs):
            if "-o" in command:
                Path(command[-1]).write_bytes(b"Fixture binary identity")
                return subprocess.CompletedProcess(command,0,stdout=b"Fixture build log\n")
            failed=command[0].endswith("-1")
            text=b"CONTROL reached actual EMFILE\n"+(b"runtime error: fixture diagnostic\n" if failed else b"CONTROL completed with no component code linked\n")
            return subprocess.CompletedProcess(command,1 if failed else 0,stdout=text,stderr=b"")
        with patch.object(sys,"argv",["probe","--compiler","fixture-cxx","--output",str(output)]),patch.object(probe,"ROOT",root),patch.object(probe,"capture",return_value={"source_commit":"a"*40,"source_dirty":False}),patch.object(probe.subprocess,"check_output",return_value=b"Fixture compiler version 1\n"),patch.object(probe.subprocess,"run",side_effect=run):
            self.assertEqual(probe.main(),1)
        return output

    def test_records_both_builds_source_flags_logs_and_negative_result(self):
        with tempfile.TemporaryDirectory() as directory:
            output=self.execute_fixture(Path(directory))
            manifest=probe.verify(output)
            self.assertEqual([r["status"] for r in manifest["results"]],["PASS","FAIL"])
            self.assertTrue(all(r["actual_emfile"] for r in manifest["results"]))
            self.assertTrue(all(r["factory_command"] and r["run_command"] and r["binary_sha256"] and r["started_utc"] for r in manifest["results"]))
            self.assertEqual(set(manifest["sources"]),set(probe.INPUTS))
            self.assertEqual(manifest["source_commit"],"a"*40)

    def test_rejects_modified_logs_status_or_disabled_vptr(self):
        for change in ("log","status","vptr"):
            with self.subTest(change=change),tempfile.TemporaryDirectory() as directory:
                output=self.execute_fixture(Path(directory))
                path=output/"manifest.json";manifest=json.loads(path.read_text())
                if change=="log":
                    (output/manifest["results"][1]["log"]).write_bytes(b"Changed negative diagnostic")
                elif change=="status":
                    manifest["results"][1]["status"]="PASS"
                else:
                    manifest["results"][1]["command"].append("-fno-sanitize=vptr")
                path.write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):
                    probe.verify(output)

    def test_zero_exit_without_emfile_or_with_diagnostic_is_not_a_pass(self):
        self.assertEqual(probe.outcome(0,b"No exhaustion observed")["status"],"FAIL")
        self.assertEqual(probe.outcome(0,b"CONTROL reached actual EMFILE\nruntime error: fixture\nCONTROL completed with no component code linked")["status"],"FAIL")
