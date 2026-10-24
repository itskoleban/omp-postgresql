"""Evidence-parser regressions; these are NOT network integration passes."""
import sys
from pathlib import Path
import unittest
import json
import tempfile
from unittest.mock import patch
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).parent))
from run_real_sessions import parse_events, Suite, Server


class RealSessionEvidenceTests(unittest.TestCase):
    def test_zero_exit_does_not_hide_pawn_stack_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            server = Server.__new__(Server)
            server.root = Path(directory)
            server.log_path = server.root/"server-output.txt"
            server.log_path.write_text("[Error] Stack/heap collision (insufficient stack size)\n")
            server.log = server.log_path.open("ab")
            server.clients = []
            server.proc = SimpleNamespace(returncode=0, pid=42, poll=lambda:0)
            server.suite = SimpleNamespace(password="fixture-password", wrong="fixture-wrong", trace=lambda *a,**k:None)
            with self.assertRaisesRegex(AssertionError, "Pawn VM runtime error"):
                server.close()

    def test_timeline_records_clock_and_process_identity_without_payloads(self):
        with tempfile.TemporaryDirectory() as directory:
            suite = Suite.__new__(Suite)
            suite.output = Path(directory)
            suite.password = "fixture-password"
            suite.wrong = "fixture-wrong"
            suite.trace("server_started", server="custom-load", pid=42, udp_port=1234)
            suite.trace("event_failed", server="custom-load", pid=42, exit_code=None)
            rows = [json.loads(line) for line in (suite.output/"timeline.jsonl").read_text().splitlines()]
            self.assertEqual([row["event"] for row in rows], ["server_started", "event_failed"])
            self.assertLessEqual(rows[0]["monotonic_ns"], rows[1]["monotonic_ns"])
            self.assertTrue(all(row["utc"].endswith("+00:00") and row["pid"] == 42 for row in rows))
            self.assertNotIn("command", rows[0])

    def test_initial_source_and_binary_identity_are_not_rebound(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in ("component","gamemode","client","provider","omp-server.exe","omp-server"):
                (root/name).write_bytes(b"initial")
            suite=Suite.__new__(Suite)
            suite.config={key:str(root/key) for key in ("component","gamemode","client","provider")}
            suite.config["server"]=str(root)
            suite.output=root;suite.results=[];suite.sql_results=[]
            suite.password="fixture-secret-one";suite.wrong="fixture-secret-two";suite.provenance=None
            source={"source_commit":"a"*40,"source_dirty":True,"tracked_source_modified":False,"untracked_entries":[{"path":"postgres.zip"}]}
            with patch("run_real_sessions.capture_source",return_value=source) as capture:
                suite.save()
                before=json.loads((root/"results.json").read_text())
                (root/"component").write_bytes(b"later")
                suite.save()
                after=json.loads((root/"results.json").read_text())
                capture.assert_called_once()
            self.assertEqual(before["source_provenance"],after["source_provenance"])
            self.assertEqual(before["inputs"],after["inputs"])

    def test_sqlstate_keeps_leading_zeroes_and_numeric_codes(self):
        events = parse_events("[Info] QA event=sql_begin p=0 callback_token=12 sqlstate=23505\nQA event=sql_begin p=1 sqlstate=08006")
        self.assertEqual(events[0]["sqlstate"], "23505")
        self.assertEqual(events[1]["sqlstate"], "08006")
        self.assertEqual(events[0]["callback_token"], 12)

    def test_connection_identity_and_npc_are_observed_separately(self):
        events = parse_events("unrelated npc=0\nQA event=connect p=0 name=QA\nQA event=connected p=0 connected=1 npc=0 token=3\nQA event=npc_created p=7 connected=1 npc=1")
        self.assertEqual(len(events), 3)
        self.assertEqual(events[1]["npc"], 0)
        self.assertEqual(events[2]["npc"], 1)
        self.assertEqual(events[0]["name"], "QA")


if __name__ == "__main__":
    unittest.main()
