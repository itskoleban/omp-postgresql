"""Execute the runner's real finally block with a failed diagnostic writer."""

import ast
import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest


class NetworkCleanupTests(unittest.TestCase):
    def exercise(self, original_failure):
        # Avoid starting PostgreSQL merely to exercise cleanup. Compile the actual
        # runner cleanup, with only its external resources replaced by recorders.
        source = Path(__file__).with_name('run_postgres.py')
        tree = ast.parse(source.read_text(encoding='utf8'))
        cleanup = next(node.finalbody for node in tree.body
                       if isinstance(node, ast.Try) and node.finalbody)
        body = ast.parse("raise RuntimeError('original workload failure')" if original_failure
                         else 'pass').body
        module = ast.fix_missing_locations(ast.Module(body=[ast.Try(
            body=body, handlers=[], orelse=[], finalbody=cleanup)], type_ignores=[]))
        operations = []

        def collector_finish():
            operations.append('diagnostic_finish')
            raise OSError('injected diagnostic write failure')

        def run(command):
            self.assertEqual(command[0], 'pg_ctl')
            self.assertEqual(command[-4:], ['-m', 'immediate', '-w', 'stop'])
            operations.append('postgres_stop')

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = root / 'data'
            data.mkdir()
            (data / 'postmaster.pid').touch()
            namespace = dict(
                sys=sys, subprocess=subprocess, Path=Path,
                network_diagnostic=SimpleNamespace(finish=collector_finish),
                stall_diagnostic=None, stop_monitor=SimpleNamespace(
                    set=lambda: operations.append('monitor_stop')),
                monitor=None,
                proxy=SimpleNamespace(close=lambda: operations.append('commit_proxy_close')),
                rollback_proxy=SimpleNamespace(close=lambda: operations.append('rollback_proxy_close')),
                wire_proxies=[], stall_servers=[], startup_attempted=True,
                data=data, root=root, args=SimpleNamespace(build=str(root / 'build')),
                run=run, retain_stall_evidence=lambda *args: operations.append('retain_evidence'))
            stderr = io.StringIO()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(stderr):
                expected = 'original workload failure' if original_failure else 'Network diagnostic collection failed'
                with self.assertRaisesRegex(RuntimeError, expected):
                    exec(compile(module, str(source), 'exec'), namespace)
        self.assertEqual(operations, ['diagnostic_finish', 'monitor_stop', 'commit_proxy_close',
                                     'rollback_proxy_close', 'postgres_stop', 'retain_evidence'])
        self.assertIn('Network diagnostic shutdown failed: OSError', stderr.getvalue())
        if original_failure:
            self.assertIn('original test failure retained', stderr.getvalue())

    def test_capture_write_failure_preserves_workload_failure_and_stops_fixture(self):
        self.exercise(original_failure=True)

    def test_capture_write_failure_fails_after_successful_workload_and_stops_fixture(self):
        self.exercise(original_failure=False)


if __name__ == '__main__':
    unittest.main()
