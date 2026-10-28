"""Bounded, optional PostgreSQL observer for the stalled-connection integration test."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time


ACTIVITY_SQL = """
SELECT json_build_object('activity',
  (SELECT coalesce(json_agg(row_to_json(s)), '[]'::json) FROM
    (SELECT a.pid, a.state, a.wait_event_type, a.wait_event,
            a.backend_start, a.query_start, a.state_change,
            pg_blocking_pids(a.pid) AS blocking_pids,
            t.ssl, t.version AS tls_version, t.cipher, t.bits
       FROM pg_stat_activity a LEFT JOIN pg_stat_ssl t USING (pid)
      WHERE a.application_name='omp-postgresql' ORDER BY a.pid LIMIT 128) s),
 'locks', (SELECT coalesce(json_agg(row_to_json(s)), '[]'::json) FROM
    (SELECT pid, locktype, mode, granted, relation FROM pg_locks
      WHERE pid IN (SELECT pid FROM pg_stat_activity
                    WHERE application_name='omp-postgresql')
      ORDER BY pid, locktype, mode LIMIT 128) s))
"""


def clear_stall_output(build):
    """Discard only this runner's previous slots before a new core invocation."""
    target = Path(build) / 'Testing' / 'Temporary'
    names = ['server.log', 'stall-activity.log', 'stall-observer.log', 'stall-runtime.log',
             'stall-certificates.log', 'stall-key-acl.log', 'stall-file-times.log', 'stall-evidence.log',
             'qualification.xml', 'LastTest.log', 'LastTestsFailed.log']
    for mode in ('ssl', 'tls', 'startup'):
        names += [f'stall-{mode}.log', f'stall-{mode}-events.log']
    for name in names:
        (target / name).unlink(missing_ok=True)


class StallDiagnostic:
    def __init__(self, root, env, psql):
        self.root = Path(root)
        self.env = {**env, 'PGAPPNAME': 'stall-diagnostic', 'PGCONNECT_TIMEOUT': '2',
                    'PGOPTIONS': '-c statement_timeout=1000'}
        self.psql = str(psql)
        self.stop = threading.Event()
        self.errors = []
        self.samples = 0
        self.thread = threading.Thread(target=self.observe, daemon=True)

    def start(self):
        self.thread.start()

    def observe(self):
        flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
        try:
            while not (self.root / 'stall-active').exists():
                if self.stop.wait(.02):
                    return
            # CTest retains its original 90-second budget. Observation is bounded
            # independently and never sends SQL through the test's healthy Lane.
            deadline = time.monotonic() + 95
            with (self.root / 'stall-activity.log').open('w', encoding='utf8') as output:
                while not self.stop.is_set() and time.monotonic() < deadline:
                    start = time.monotonic_ns()
                    result = subprocess.run(
                        [self.psql, 'service=omp_test', '-XAt', '-v', 'ON_ERROR_STOP=1', '-c', ACTIVITY_SQL],
                        env=self.env, capture_output=True, text=True, timeout=3, creationflags=flags)
                    if result.returncode:
                        raise RuntimeError('Stall activity observer failed, exit=' + str(result.returncode))
                    row = dict(monotonic_ns=start, end_ns=time.monotonic_ns(), utc_ns=time.time_ns(),
                               snapshot=json.loads(result.stdout))
                    output.write(json.dumps(row) + '\n')
                    output.flush()
                    self.samples += 1
                    if (self.root / 'stall-finished').exists() or self.stop.wait(1):
                        break
        except Exception as error:
            self.errors.append(type(error).__name__ + ': ' + str(error))

    def finish(self):
        self.stop.set()
        if self.thread.ident is not None:
            self.thread.join(timeout=4)
        else:
            self.errors.append('Stall activity observer did not start')
        if self.thread.is_alive():
            self.errors.append('Stall activity observer did not stop')
        if not self.samples:
            self.errors.append('Stall activity observer captured no samples')
        try:
            (self.root / 'stall-observer.log').write_text(json.dumps(
                dict(samples=self.samples, errors=self.errors,
                     limits='One observer process per second can perturb scheduling; no SQL text/values captured.')) + '\n',
                encoding='utf8')
        except OSError:
            self.errors.append('Stall observer summary could not be retained')
        return not self.errors


def retain_stall_evidence(root, build):
    """Copy only explicit diagnostic outputs, never profiles or private keys."""
    root = Path(root)
    target = Path(build) / 'Testing' / 'Temporary'
    target.mkdir(parents=True, exist_ok=True)
    names = ['server.log', 'stall-activity.log', 'stall-observer.log',
             'stall-runtime.log', 'stall-certificates.log', 'stall-key-acl.log']
    times = []
    for mode in ('ssl', 'tls', 'startup'):
        names += [f'stall-{mode}.log', f'stall-{mode}-events.log']
        for path in sorted((root / mode).glob('*')):
            if path.name.startswith(('accepted-', 'ready-', 'closed-')) and path.is_file():
                stat = path.stat()
                times.append(dict(mode=mode, marker=path.name, mtime_ns=stat.st_mtime_ns,
                                  ctime_ns=stat.st_ctime_ns,
                                  birthtime_ns=getattr(stat, 'st_birthtime_ns', None)))
    (target / 'stall-file-times.log').write_text(json.dumps(times) + '\n', encoding='utf8')
    copied = {}
    for name in names:
        if (root / name).is_file():
            shutil.copyfile(root / name, target / name)
            copied[name] = hashlib.sha256((target / name).read_bytes()).hexdigest()
    runtime = {}
    if (root / 'stall-runtime.log').is_file():
        for line in (root / 'stall-runtime.log').read_text(encoding='utf8').splitlines():
            name, value = line.split('\t', 1)
            path = Path(value)
            if path.is_absolute() and path.suffix.lower() in ('.dll', '.exe', '') and path.is_file():
                runtime[name] = dict(path=value, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    (target / 'stall-evidence.log').write_text(json.dumps(dict(fixture=root.name, files=copied, runtime=runtime,
        limits='Critical runtime selection at test start, hashes after exit; not an exhaustive module map or immutable load guarantee. '
               'Original marker timestamps retained separately; no private keys or service profiles copied.')) + '\n',
        encoding='utf8')
