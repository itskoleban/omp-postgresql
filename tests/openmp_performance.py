"""Bounded real-server measurement capture; no component timing instrumentation."""

import csv
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import time

FIELDS = (
    "kind",
    "case",
    "level",
    "destination",
    "repetition",
    "index",
    "submission_ms",
    "callback_or_batch_elapsed_ms",
    "timer_lateness_ms",
)
MAX_ROWS = 12 * 8 * (64 + 128 + 1)
PREFIX = "PG_PERF,"
SUMMARY = re.compile(r"log_dropped count=(\d+) sink_failures=(\d+)")
BEGIN = re.compile(r"PG_PERF_BEGIN,(\d+),(\d+),(\d+),(\d+)")


class ObservedContext:
    """Incremental complete-line markers; labels may lag the actual server phase."""

    def __init__(self):
        self.remainder = ""
        self.context = (-1, -1, -1, -1, "startup")

    def feed(self, update):
        lines = (self.remainder + update).split("\n")
        self.remainder = lines.pop()
        if len(self.remainder) > 4096:
            raise ValueError("Overlong performance console line")
        for line in lines:
            match = BEGIN.search(line)
            if match:
                self.context = (
                    *[int(value) for value in match.groups()],
                    "workload_or_observation",
                )
            elif "PG_PERF,batch," in line:
                self.context = (*self.context[:4], "reporting_or_idle")
        return self.context


def parse_rows(content):
    rows = []
    seen = set()
    inventory = {}
    for line in content.splitlines():
        position = line.find(PREFIX)
        if position < 0:
            continue
        fields = line[position + len(PREFIX) :].split(",")
        if len(fields) != len(FIELDS) or fields[0] not in ("job", "timer", "batch"):
            raise ValueError("Malformed performance record")
        try:
            values = [int(value) for value in fields[1:]]
        except ValueError as error:
            raise ValueError("Non-numeric performance record") from error
        case, level, destination, repetition, index, submit, latency, lateness = values
        if (
            not 0 <= case < 12
            or level != (0, 2, 4, 5)[case // 3]
            or destination != case % 3
            or not 0 <= repetition <= 7
            or min(values) < 0
            or max(submit, latency, lateness) > 60000
        ):
            raise ValueError("Out-of-range performance record")
        maximum = {"job": 64, "timer": 128, "batch": 1}[fields[0]]
        if index >= maximum:
            raise ValueError("Out-of-range performance sample index")
        if (
            (fields[0] == "job" and (lateness or latency < submit))
            or (fields[0] == "timer" and (submit or latency))
            or (fields[0] == "batch" and (lateness or latency < submit))
        ):
            raise ValueError("Inconsistent performance measurement")
        identity = fields[0], case, repetition, index
        if identity in seen:
            raise ValueError("Duplicate performance record")
        seen.add(identity)
        inventory.setdefault((case, repetition), {}).setdefault(fields[0], set()).add(index)
        rows.append([fields[0], *values])
        if len(rows) > MAX_ROWS:
            raise ValueError("Performance capture exceeded its row bound")
    for case in range(12):
        for repetition in range(8):
            sample = inventory.get((case, repetition), {})
            timers = sample.get("timer", set())
            if (
                sample.get("job") != set(range(64))
                or sample.get("batch") != {0}
                or not timers
                or timers != set(range(len(timers)))
            ):
                raise ValueError(f"Incomplete performance case {case}, repetition {repetition}")
    return rows


class ProcessSamples:
    """Whole open.mp process CPU seconds/RSS, including its component threads."""

    def __init__(self, pid):
        self.pid = pid
        self.rows = []
        self.started = time.monotonic()
        self.reason = None
        self.handle = None
        if os.name == "nt":
            from ctypes import wintypes

            self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            self.psapi = ctypes.WinDLL("psapi", use_last_error=True)
            self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            self.kernel.OpenProcess.restype = wintypes.HANDLE
            self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            self.kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [
                ctypes.POINTER(wintypes.FILETIME)
            ] * 4

            class Memory(ctypes.Structure):
                _fields_ = [("cb", wintypes.DWORD), ("faults", wintypes.DWORD)] + [
                    (name, ctypes.c_size_t)
                    for name in (
                        "peak_rss",
                        "rss",
                        "peak_paged",
                        "paged",
                        "peak_nonpaged",
                        "nonpaged",
                        "pagefile",
                        "peak_pagefile",
                    )
                ]

            self.memory_type = Memory
            self.psapi.GetProcessMemoryInfo.argtypes = [
                wintypes.HANDLE,
                ctypes.POINTER(Memory),
                wintypes.DWORD,
            ]
            # PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, for this child only.
            self.handle = self.kernel.OpenProcess(0x0400 | 0x0010, False, pid)
            if not self.handle:
                self.reason = "OpenProcess denied; CPU/RSS unavailable"
        elif not Path(f"/proc/{pid}/stat").exists():
            self.reason = "No supported /proc process counters; CPU/RSS unavailable"

    def sample(self, context=(-1, -1, -1, -1, "startup")):
        if self.reason:
            return
        try:
            if os.name == "nt":
                from ctypes import wintypes

                created, exited, kernel, user = (wintypes.FILETIME() for _ in range(4))
                if not self.kernel.GetProcessTimes(
                    self.handle,
                    ctypes.byref(created),
                    ctypes.byref(exited),
                    ctypes.byref(kernel),
                    ctypes.byref(user),
                ):
                    raise OSError("GetProcessTimes failed")
                cpu = sum((v.dwHighDateTime << 32) | v.dwLowDateTime for v in (kernel, user)) / 1e7
                memory = self.memory_type()
                memory.cb = ctypes.sizeof(memory)
                if not self.psapi.GetProcessMemoryInfo(
                    self.handle, ctypes.byref(memory), memory.cb
                ):
                    raise OSError("GetProcessMemoryInfo failed")
                rss = memory.rss
            else:
                stat = Path(f"/proc/{self.pid}/stat").read_text().rsplit(")", 1)[1].split()
                cpu = (int(stat[11]) + int(stat[12])) / os.sysconf("SC_CLK_TCK")
                rss = int(Path(f"/proc/{self.pid}/statm").read_text().split()[1]) * os.sysconf(
                    "SC_PAGE_SIZE"
                )
            if len(self.rows) >= 5000:
                raise ValueError("Process sample bound exceeded")
            self.rows.append((time.monotonic() - self.started, cpu, rss, *context))
        except (OSError, ValueError, IndexError) as error:
            self.reason = str(error)

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def capture(output, content, server, component, gamemode, samples, runtime_paths=()):
    """Validate complete samples, then save raw CSV plus bounded provenance/counters."""
    rows = parse_rows(content)
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf8") as destination:
        writer = csv.writer(destination)
        writer.writerow(FIELDS)
        writer.writerows(rows)
    resources = output.with_suffix(".resources.csv")
    with resources.open("w", newline="", encoding="utf8") as destination:
        writer = csv.writer(destination)
        writer.writerow(
            (
                "observer_elapsed_s",
                "process_cpu_total_s",
                "process_rss_bytes",
                "observed_case",
                "observed_level",
                "observed_destination",
                "observed_repetition",
                "observed_phase",
            )
        )
        writer.writerows(samples.rows)
    # Each sink is counted separately: BOTH can contain the same summary in two
    # places. Absence of a summary is NOT evidence of zero actual drops.
    observed = []
    sinks = [("console", content)]
    files = sorted((Path(server) / "logs").glob("pg-perf-*.log*"))
    if len(files) > 24:
        raise ValueError("Unexpected performance log inventory")
    log_inventory = []
    for path in files:
        if not re.fullmatch(r"pg-perf-[0245]-[12]\.log(?:\.1)?", path.name):
            raise ValueError("Unexpected performance log filename")
        if not path.is_file() or path.stat().st_size > 1024 * 1024 + 512:
            raise ValueError("Performance log exceeded expected rotation bounds")
        sinks.append((path.name, path.read_text(encoding="utf8", errors="strict")))
        log_inventory.append(
            {"name": path.name, "bytes": path.stat().st_size, "sha256": sha256(path)}
        )
    for sink, text in sinks:
        for match in SUMMARY.finditer(text):
            observed.append((sink, int(match[1]), int(match[2])))
    if any(failures for _, _, failures in observed):
        raise ValueError("Performance measurement encountered a reported sink failure")
    for level in (2, 4, 5):
        for destination in (1, 2):
            basename = f"pg-perf-{level}-{destination}.log"
            retained = "\n".join(
                text for name, text in sinks if name in (basename, basename + ".1")
            )
            if "job_terminal" not in retained:
                raise ValueError(
                    f"Performance file sink has no observed terminal records: {basename}"
                )
    with output.with_suffix(".drops.csv").open("w", newline="", encoding="utf8") as destination:
        writer = csv.writer(destination)
        writer.writerow(("sink", "reported_drop_delta", "reported_cumulative_sink_failures"))
        writer.writerows(observed)
    source = Path(__file__).resolve().parents[1]
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    try:
        commit = subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", "HEAD"],
            text=True,
            creationflags=flags,
        ).strip()
        status = subprocess.check_output(
            ["git", "-C", str(source), "status", "--porcelain"],
            text=True,
            creationflags=flags,
        ).splitlines()
    except (OSError, subprocess.CalledProcessError):
        commit, status = None, ["Source Git identity unavailable"]
    metadata = {
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": commit,
        "source_dirty": bool(status),
        "source_status": status[:200],
        "source_status_truncated": len(status) > 200,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "logical_cpus": os.cpu_count(),
        "python": platform.python_version(),
        "sha256": {
            "component": sha256(component),
            "gamemode_amx": sha256(gamemode),
            "server": sha256(
                Path(server) / ("omp-server.exe" if os.name == "nt" else "omp-server")
            ),
            **{
                name: sha256(source / name)
                for name in (
                    "tests/logging_performance.pwn",
                    "tests/run_openmp.py",
                    "tests/openmp_performance.py",
                    "pawn/omp-postgresql.inc",
                )
            },
        },
        "runtime_sha256": {
            name: sha256(Path(server) / name)
            for name in sorted({Path(path).name for path in runtime_paths})
        },
        "workload": "Two lanes; 64 pg_query jobs/batch: 56 SELECT 1, eight division-by-zero errors; 12 level/destination cases; round 0 warmup, rounds 1..7 measured",
        "timing": "Pawn GetTickCount integer milliseconds; callback entry minus pg_query entry; batch elapsed includes 100 ms observation after callbacks; one-shot timer lateness from its scheduled due time; output deferred until all callbacks completed",
        "resources": "Whole open.mp process CPU seconds/RSS sampled by observer; context labels reflect last redirected-output marker, may lag actual phase; intervals crossing labels must be discarded; workload_or_observation includes SQL and timers, reporting_or_idle includes output; no component-only CPU attribution",
        "resource_unavailable_reason": samples.reason,
        "resource_samples": len(samples.rows),
        "component_tick_execution_cost": "unmeasured",
        "drops": "Observed summaries only, per sink; BOTH duplicates exist; append/rotation retention may be incomplete; no summary does not prove zero loss",
        "file_inventory": log_inventory,
        "transport": "As configured by game service profile; run_postgres.py supplies loopback verify-full TLS",
    }
    output.with_suffix(".metadata.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf8"
    )
    return len(rows)
