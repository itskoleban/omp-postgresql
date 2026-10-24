"""Deterministic residue rules preserve exact evidence and legitimate technical text."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).parents[1]/"scripts"))
from check_english import check


class EnglishPolicyTests(unittest.TestCase):
    def fixture(self, root):
        (root/".github").mkdir()
        policy = {"residue_patterns":[{"id":"known-legacy-term","pattern":r"\bprue\u0062as\b"}],"frozen_files":{}}
        (root/".github/english-policy.json").write_text(json.dumps(policy),encoding="utf8")
        return policy

    def test_repository_policy(self):
        check(Path(__file__).parents[1])

    def test_residue_rejected_and_unicode_technical_text_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            path=root/"guide.md"
            path.write_text("TLS SELECT SQLSTATE 23505; latency 10 \u03bcs; UTF-8 \u2192 wire\n",encoding="utf8")
            check(root,[".github/english-policy.json","guide.md"])
            path.write_text("prue\u0062as",encoding="utf8")
            with self.assertRaisesRegex(ValueError,"legacy residue"):
                check(root,[".github/english-policy.json","guide.md"])

    def test_only_exact_historical_bytes_are_exempt(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);policy=self.fixture(root)
            (root/"audit").mkdir()
            path=root/"audit/original.txt";path.write_bytes(b"prue\x62as\n")
            policy["frozen_files"]["audit/original.txt"]={"sha256":hashlib.sha256(path.read_bytes()).hexdigest(),"reason":"Original external diagnostic capture"}
            (root/".github/english-policy.json").write_text(json.dumps(policy),encoding="utf8")
            paths=[".github/english-policy.json","audit/original.txt"]
            check(root,paths)
            path.write_bytes(b"Changed result\n")
            with self.assertRaisesRegex(ValueError,"identity changed"):
                check(root,paths)
            with self.assertRaisesRegex(ValueError,"missing from tracked inventory"):
                check(root,[".github/english-policy.json"])

    def test_new_capture_does_not_inherit_directory_exemption(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            (root/"audit").mkdir()
            (root/"audit/new.txt").write_text("prue\u0062as",encoding="utf8")
            with self.assertRaisesRegex(ValueError,"legacy residue"):
                check(root,[".github/english-policy.json","audit/new.txt"])
