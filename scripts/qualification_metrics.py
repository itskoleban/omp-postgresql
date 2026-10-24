"""Extract finite soak observations; incomplete or shortened runs cannot pass."""
import csv
import io
import re
import statistics

def values(line):
    return {key: int(value) for key, value in re.findall(r"([a-zA-Z0-9_]+)=([0-9]+)", line)}

def analyze(text, seconds):
    samples = [values(line) for line in text.splitlines() if line.startswith("audit_metric ")]
    complete = [values(line) for line in text.splitlines() if line.startswith("load_sql_operations=")]
    if len(complete) != 1 or not samples:
        raise ValueError("Missing complete stress log or metrics")
    final = complete[0]
    if final.get("minimum_seconds") != seconds or final.get("load_elapsed_us", 0) < seconds * 1_000_000:
        raise ValueError("Measured sustained load is shorter than requested duration")
    required = {"elapsed_us", "rss_kib", "cpu_us", "p50_us", "p95_us", "p99_us",
                "sql_operations", "threads", "descriptors_or_handles"}
    if any(not required <= sample.keys() for sample in samples):
        raise ValueError("Incomplete resource metrics")
    if any(a["elapsed_us"] >= b["elapsed_us"] or a["sql_operations"] >= b["sql_operations"]
           for a, b in zip(samples, samples[1:])):
        raise ValueError("Nonmonotonic stress observations")
    rss = [sample["rss_kib"] for sample in samples]
    window = max(1, len(rss) // 5)
    summary = {
        "status": "PASS",
        "requested_seconds": seconds,
        "measured_load_seconds": final["load_elapsed_us"] / 1_000_000,
        "samples": len(samples),
        "load_sql_operations": final["load_sql_operations"],
        "first_rss_kib": rss[0], "last_rss_kib": rss[-1],
        "maximum_sampled_rss_kib": max(rss),
        "first_window_median_rss_kib": statistics.median(rss[:window]),
        "last_window_median_rss_kib": statistics.median(rss[-window:]),
        "cpu_seconds_to_last_sample": samples[-1]["cpu_us"] / 1_000_000,
        "threads_range": [min(s["threads"] for s in samples), max(s["threads"] for s in samples)],
        "descriptors_or_handles_range": [
            min(s["descriptors_or_handles"] for s in samples),
            max(s["descriptors_or_handles"] for s in samples)],
        "median_of_wave_percentiles_us": {
            percentile: statistics.median(s[percentile + "_us"] for s in samples)
            for percentile in ("p50", "p95", "p99")},
        "resource_points": [line for line in text.splitlines() if line.startswith("audit_resource ")],
        "limits": [
            "Finite RSS observations do not establish absence of leaks.",
            "Wave percentile medians are not global latency percentiles.",
            "Primary lane counters exclude destroyed side connections; setup/observer SQL is separate.",
            "Pending=0 is a quiescent wave observation, not continuous queue-depth sampling.",
            "Terminal delivery checks are separate from real Pawn callback latency and player generation tests.",
            "PASS means duration/log/invariant checks completed; trend interpretation requires review."
        ]
    }
    csv_output = io.StringIO()
    writer = csv.DictWriter(csv_output, fieldnames=sorted(set().union(*(s.keys() for s in samples))))
    writer.writeheader()
    writer.writerows(samples)
    return summary, csv_output.getvalue()
