"""Validate real-Pawn capture completeness and honest resource/drop accounting."""

import csv
import json
import os
from pathlib import Path
import tempfile
import unittest

from openmp_performance import ObservedContext, ProcessSamples, capture, parse_rows


def complete_output():
    lines = []
    for case in range(12):
        level, destination = (0, 2, 4, 5)[case // 3], case % 3
        for repetition in range(8):
            prefix = f"{case},{level},{destination},{repetition}"
            lines.append(f"[00:00:00] PG_PERF,batch,{prefix},0,1,120,0")
            for index in range(64):
                lines.append(f"PG_PERF,job,{prefix},{index},0,20,0")
            lines.append(f"PG_PERF,timer,{prefix},0,0,0,2")
    return "\n".join(lines)


class PerformanceTests(unittest.TestCase):
    def test_all_levels_destinations_repetitions_jobs_and_timers_required(self):
        rows = parse_rows(complete_output())
        self.assertEqual(len(rows), 12 * 8 * 66)
        self.assertEqual(sum(row[0] == "job" for row in rows), 6144)

    def test_missing_job_timer_or_batch_fails(self):
        lines = complete_output().splitlines()
        for missing in (0, 1, 65):
            with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, "Incomplete"):
                parse_rows("\n".join(lines[:missing] + lines[missing + 1 :]))

    def test_duplicate_record_fails(self):
        content = complete_output()
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            parse_rows(content + "\n" + content.splitlines()[1])

    def test_observed_context_distinguishes_workload_from_reporting(self):
        observer = ObservedContext()
        self.assertEqual(observer.feed("startup\n"), (-1, -1, -1, -1, "startup"))
        self.assertEqual(observer.feed("PG_PERF_BE"), (-1, -1, -1, -1, "startup"))
        self.assertEqual(observer.feed("GIN,0,0,0,0\n")[-1], "workload_or_observation")
        self.assertEqual(observer.feed("PG_PERF,batch,")[-1], "workload_or_observation")
        self.assertEqual(observer.feed("0,0,0,0,0,1,120,0\n")[-1], "reporting_or_idle")
        self.assertEqual(observer.feed("")[-1], "reporting_or_idle")

    def test_bad_fields_and_values_fail(self):
        content = complete_output()
        original = "PG_PERF,job,0,0,0,0,0,0,20,0"
        for replacement in (
            "PG_PERF,job,0,0,0,0,0,0,20",
            "PG_PERF,job,0,0,0,0,0,0,20,no",
            "PG_PERF,job,0,0,0,0,0,-1,20,0",
            "PG_PERF,job,0,0,0,0,0,21,20,0",
            "PG_PERF,job,0,0,0,0,64,0,20,0",
            "PG_PERF,job,0,5,0,0,0,0,20,0",
            "PG_PERF,job,0,0,0,0,0,0,60001,0",
        ):
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                parse_rows(content.replace(original, replacement))

    def test_live_process_sampler_is_bounded_and_monotonic(self):
        samples = ProcessSamples(os.getpid())
        try:
            samples.sample()
            samples.sample()
            if samples.reason:
                self.skipTest(samples.reason)
            self.assertEqual(len(samples.rows), 2)
            self.assertGreaterEqual(samples.rows[1][1], samples.rows[0][1])
            self.assertGreater(samples.rows[1][2], 0)
        finally:
            samples.close()

    def test_capture_keeps_sink_observations_separate_and_records_limits(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "logs").mkdir()
            server = root / ("omp-server.exe" if os.name == "nt" else "omp-server")
            server.write_bytes(b"test executable identity")
            component = root / "component"
            component.write_bytes(b"test component identity")
            gamemode = root / "gamemode"
            gamemode.write_bytes(b"test compiled fixture identity")
            runtime = root / "client.dll"
            runtime.write_bytes(b"test runtime identity")
            summary = "[omp-pg][WARN] log_dropped count=7 sink_failures=0\n"
            for level in (2, 4, 5):
                for destination in (1, 2):
                    text = "[omp-pg][DEBUG] job_terminal\n"
                    if (level, destination) == (4, 2):
                        text += summary
                    (root / "logs" / f"pg-perf-{level}-{destination}.log").write_text(
                        text, encoding="utf8"
                    )
            samples = ProcessSamples(os.getpid())
            try:
                samples.sample()
                output = root / "measurement.csv"
                capture(
                    output,
                    complete_output() + "\n" + summary,
                    root,
                    component,
                    gamemode,
                    samples,
                    [runtime],
                )
                with output.with_suffix(".drops.csv").open(encoding="utf8") as source:
                    drops = list(csv.reader(source))
                self.assertEqual(drops[1:], [["console", "7", "0"], ["pg-perf-4-2.log", "7", "0"]])
                metadata = json.loads(output.with_suffix(".metadata.json").read_text())
                self.assertEqual(metadata["component_tick_execution_cost"], "unmeasured")
                self.assertIn("does not prove zero", metadata["drops"])
                self.assertEqual(len(metadata["sha256"]["component"]), 64)
                self.assertEqual(list(metadata["runtime_sha256"]), ["client.dll"])
                self.assertEqual(len(metadata["runtime_sha256"]["client.dll"]), 64)
            finally:
                samples.close()


if __name__ == "__main__":
    unittest.main()
