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


class WireEvidenceCleanupTests(unittest.TestCase):
    def exercise(self, original_failure, fault):
        source = Path(__file__).with_name('run_postgres.py')
        tree = ast.parse(source.read_text(encoding='utf8'))
        cleanup = next(node.finalbody for node in tree.body
                       if isinstance(node, ast.Try) and node.finalbody)
        body = ast.parse("raise RuntimeError('original workload failure')" if original_failure
                         else 'pass').body
        module = ast.fix_missing_locations(ast.Module(body=[ast.Try(
            body=body, handlers=[], orelse=[], finalbody=cleanup)], type_ignores=[]))
        operations = []

        def copyfile(source, destination):
            operations.append('copy_' + Path(source).parent.name)
            raise OSError('injected wire evidence copy failure')

        def run(command):
            self.assertEqual(command[0], 'pg_ctl')
            self.assertEqual(command[-4:], ['-m', 'immediate', '-w', 'stop'])
            operations.append('postgres_stop')

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = root / 'data'
            data.mkdir()
            (data / 'postmaster.pid').touch()
            build = root / 'build'
            if fault == 'mkdir':
                # A real path collision reproduces unavailable evidence storage.
                build.write_text('not a directory')
            wires = []
            for name in ('first', 'second'):
                path = root / name
                path.mkdir()
                (path / 'wire.jsonl').write_text('{}\n')
                wires.append(SimpleNamespace(directory=path,
                    close=lambda name=name: operations.append('close_' + name)))
            stalled = SimpleNamespace(mode='tls', errors=[],
                close=lambda: operations.append('close_stall'))
            namespace = dict(sys=sys, subprocess=subprocess, Path=Path,
                shutil=SimpleNamespace(copyfile=copyfile),
                network_diagnostic=None, stall_diagnostic=None,
                stop_monitor=SimpleNamespace(set=lambda: operations.append('monitor_stop')),
                monitor=None, proxy=None, rollback_proxy=None,
                wire_proxies=wires, stall_servers=[stalled], startup_attempted=True,
                data=data, root=root, args=SimpleNamespace(build=str(build)), run=run,
                retain_stall_evidence=lambda *args: operations.append('retain_evidence'))
            stderr = io.StringIO()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(stderr):
                expected = 'original workload failure' if original_failure else 'Wire cleanup failed'
                with self.assertRaisesRegex(RuntimeError, expected):
                    exec(compile(module, str(source), 'exec'), namespace)
        required = ['monitor_stop', 'close_first', 'close_second', 'close_stall',
                    'postgres_stop', 'retain_evidence']
        self.assertEqual([operation for operation in operations
                          if not operation.startswith('copy_')], required)
        self.assertIn('Wire evidence retention failed', stderr.getvalue())
        if original_failure:
            self.assertIn('Wire cleanup errors:', stderr.getvalue())

    def test_wire_evidence_failure_preserves_workload_failure_and_cleans_all_fixtures(self):
        for fault in ('mkdir', 'copy'):
            with self.subTest(fault=fault):
                self.exercise(original_failure=True, fault=fault)

    def test_wire_evidence_failure_fails_success_after_cleaning_all_fixtures(self):
        for fault in ('mkdir', 'copy'):
            with self.subTest(fault=fault):
                self.exercise(original_failure=False, fault=fault)


class ResourceCleanupTests(unittest.TestCase):
    def exercise(self, fault, original_failure):
        source = Path(__file__).with_name('run_postgres.py')
        tree = ast.parse(source.read_text(encoding='utf8'))
        cleanup = next(node.finalbody for node in tree.body
                       if isinstance(node, ast.Try) and node.finalbody)
        body = ast.parse("raise RuntimeError('original workload failure')" if original_failure
                         else 'pass').body
        module = ast.fix_missing_locations(ast.Module(body=[ast.Try(
            body=body, handlers=[], orelse=[], finalbody=cleanup)], type_ignores=[]))
        operations = []

        def operation(name):
            def perform(*args, **kwargs):
                operations.append(name)
                if name == fault:
                    raise OSError('injected ' + name + ' failure')
            return perform

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = root / 'data'
            data.mkdir()
            (data / 'postmaster.pid').touch()
            wire = SimpleNamespace(directory=root / 'wire', close=operation('wire_close'))
            stalled = SimpleNamespace(mode='tls', errors=[], close=operation('stall_close'))
            (root / 'stall-tls.log').write_text('closed\n')
            namespace = dict(
                sys=sys, subprocess=subprocess, Path=Path,
                shutil=__import__('shutil'), network_diagnostic=None, stall_diagnostic=None,
                stop_monitor=SimpleNamespace(set=operation('monitor_stop')),
                monitor=SimpleNamespace(join=operation('monitor_join'),
                                        is_alive=lambda: fault == 'monitor_alive'),
                proxy=SimpleNamespace(close=operation('commit_proxy_close')),
                rollback_proxy=SimpleNamespace(close=operation('rollback_proxy_close')),
                wire_proxies=[wire], stall_servers=[stalled], startup_attempted=True,
                data=data, root=root, args=SimpleNamespace(build=str(root / 'build')),
                run=operation('postgres_stop'), retain_stall_evidence=operation('retain_evidence'))
            stderr = io.StringIO()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(stderr):
                expected = 'original workload failure' if original_failure else 'Fixture cleanup failed'
                with self.assertRaisesRegex(RuntimeError, expected):
                    exec(compile(module, str(source), 'exec'), namespace)
            self.assertEqual(operations, ['monitor_stop', 'monitor_join', 'commit_proxy_close',
                                         'rollback_proxy_close', 'wire_close', 'stall_close',
                                         'postgres_stop', 'retain_evidence'])
            self.assertIn('Fixture cleanup', stderr.getvalue())
            self.assertIn('monitor join' if fault == 'monitor_alive' else fault.replace('_', ' '),
                          stderr.getvalue())
            if original_failure:
                self.assertIn('original test failure retained', stderr.getvalue())

    def test_each_cleanup_failure_preserves_primary_failure_and_attempts_later_cleanup(self):
        for fault in ('monitor_join', 'monitor_alive', 'commit_proxy_close',
                      'rollback_proxy_close', 'postgres_stop', 'retain_evidence'):
            with self.subTest(fault=fault):
                self.exercise(fault, original_failure=True)

    def test_each_cleanup_failure_fails_success_after_attempting_later_cleanup(self):
        for fault in ('monitor_join', 'monitor_alive', 'commit_proxy_close',
                      'rollback_proxy_close', 'postgres_stop', 'retain_evidence'):
            with self.subTest(fault=fault):
                self.exercise(fault, original_failure=False)


if __name__ == '__main__':
    unittest.main()
