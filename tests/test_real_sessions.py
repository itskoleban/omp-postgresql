"""Evidence-parser regressions; these are NOT network integration passes."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).parent))
from run_real_sessions import parse_events


class RealSessionEvidenceTests(unittest.TestCase):
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
