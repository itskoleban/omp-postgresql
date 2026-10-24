import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).parents[1] / "scripts"))
from qualification_metrics import analyze

class MetricsTests(unittest.TestCase):
    def log(self, elapsed=120000001, configured=120):
        return ("audit_metric elapsed_us=1000000 rss_kib=123 cpu_us=400 threads=5 "
                "descriptors_or_handles=8 p50_us=10 p95_us=20 p99_us=30 sql_operations=100\n"
                f"load_sql_operations=100 minimum_seconds={configured} load_elapsed_us={elapsed}\n")
    def test_short_or_wrong_configuration_cannot_claim_pass(self):
        for text in (self.log(elapsed=119999999), self.log(configured=0)):
            with self.assertRaises(ValueError):
                analyze(text, 120)
    def test_truncated_success_log_cannot_claim_pass(self):
        with self.assertRaises(ValueError):
            analyze(self.log().split("load_sql_operations=")[0], 120)
    def test_completed_log_retains_metrics_and_limits(self):
        summary, csv = analyze(self.log(), 120)
        self.assertEqual(summary["status"], "PASS")
        self.assertGreaterEqual(summary["measured_load_seconds"], 120)
        self.assertEqual(summary["descriptors_or_handles_range"], [8, 8])
        self.assertIn("not global", summary["limits"][1])
        self.assertIn("rss_kib", csv)

if __name__ == "__main__":
    unittest.main()
