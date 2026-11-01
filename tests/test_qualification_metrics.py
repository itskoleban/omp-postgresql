import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).parents[1] / "scripts"))
from qualification_metrics import analyze

class MetricsTests(unittest.TestCase):
    def log(self, elapsed=120000001, configured=120):
        return ("audit_metric elapsed_us=1000000 rss_kib=123 cpu_us=400 threads=5 "
                "descriptors_or_handles=8 p50_us=10 p95_us=20 p99_us=30 sql_operations=100 "
                "pending=0 errors=0 primary_failed_jobs=0 primary_cancelled_jobs=0 "
                "primary_uncertain_jobs=0 primary_reconnects=2\n"
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

    def test_final_counters_cannot_precede_retained_samples(self):
        for text in (self.log().replace("elapsed_us=1000000 ", "elapsed_us=121000000 "),
                     self.log().replace("load_sql_operations=100", "load_sql_operations=99")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                analyze(text, 120)

    def test_failed_quiescent_invariants_cannot_claim_pass(self):
        for key in ("pending", "errors", "primary_failed_jobs", "primary_cancelled_jobs",
                    "primary_uncertain_jobs"):
            with self.subTest(key=key), self.assertRaises(ValueError):
                analyze(self.log().replace(f"{key}=0", f"{key}=1"), 120)

    def test_impossible_latency_order_cannot_claim_pass(self):
        with self.assertRaises(ValueError):
            analyze(self.log().replace("p50_us=10", "p50_us=25"), 120)

    def test_cumulative_cpu_cannot_regress(self):
        sample, final = self.log().splitlines()
        second = sample.replace("elapsed_us=1000000", "elapsed_us=2000000").replace(
            "sql_operations=100", "sql_operations=200").replace("cpu_us=400", "cpu_us=300")
        final = final.replace("load_sql_operations=100", "load_sql_operations=200")
        with self.assertRaises(ValueError):
            analyze("\n".join((sample, second, final)), 120)

if __name__ == "__main__":
    unittest.main()
