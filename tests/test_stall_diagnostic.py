"""Observation stays bounded and retained outputs exclude fixture credentials."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from stall_diagnostic import StallDiagnostic, clear_stall_output, retain_stall_evidence


class StallDiagnosticTests(unittest.TestCase):
    def test_new_invocation_cannot_retain_an_earlier_stall_or_ctest_result(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'Testing/Temporary'
            target.mkdir(parents=True)
            for name in ('qualification.xml', 'LastTest.log', 'stall-runtime.log',
                         'stall-startup-events.log', 'stall-observer.log', 'server.log'):
                (target / name).write_text('EARLIER_ATTEMPT')
            (target / 'unrelated.log').write_text('OTHER_OUTPUT')
            clear_stall_output(directory)
            self.assertEqual([p.name for p in target.iterdir()], ['unrelated.log'])
            self.assertEqual((target / 'unrelated.log').read_text(), 'OTHER_OUTPUT')

    def test_activity_is_separate_and_includes_successful_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'stall-active').write_text('123')
            (root / 'stall-finished').write_text('0')
            result = subprocess.CompletedProcess([], 0, '{"activity":[],"locks":[]}', '')
            observer = StallDiagnostic(root, {'UNCHANGED': 'value'}, 'fixture-psql')
            with patch('stall_diagnostic.subprocess.run', return_value=result) as run:
                # Run synchronously so finish cannot stop observation before its first sample.
                observer.observe()
            self.assertEqual(observer.samples, 1)
            self.assertEqual(observer.env['UNCHANGED'], 'value')
            self.assertEqual(observer.env['PGAPPNAME'], 'stall-diagnostic')
            self.assertEqual(run.call_args.kwargs['timeout'], 3)
            row = json.loads((root / 'stall-activity.log').read_text())
            self.assertEqual(row['snapshot'], {'activity': [], 'locks': []})
            self.assertGreaterEqual(row['end_ns'], row['monotonic_ns'])

    def test_observer_error_is_not_a_successful_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'stall-active').write_text('123')
            result = subprocess.CompletedProcess([], 1, '', 'fixture failure')
            observer = StallDiagnostic(root, {}, 'fixture-psql')
            with patch('stall_diagnostic.subprocess.run', return_value=result):
                observer.start()
                observer.thread.join(timeout=2)
                self.assertFalse(observer.finish())
            self.assertEqual(observer.samples, 0)
            self.assertTrue(json.loads((root / 'stall-observer.log').read_text())['errors'])

    def test_not_started_observer_does_not_prevent_fixture_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            observer = StallDiagnostic(directory, {}, 'fixture-psql')
            self.assertFalse(observer.finish())
            self.assertTrue(observer.stop.is_set())
            self.assertIn('Stall activity observer did not start', observer.errors)

    def test_summary_write_failure_does_not_prevent_fixture_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            observer = StallDiagnostic(directory, {}, 'fixture-psql')
            with patch('stall_diagnostic.Path.write_text', side_effect=OSError('fixture write failure')):
                self.assertFalse(observer.finish())
            self.assertTrue(observer.stop.is_set())
            self.assertIn('Stall observer summary could not be retained', observer.errors)

    def test_retention_preserves_original_bytes_and_never_exports_keys_or_profiles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'fixture'
            root.mkdir()
            (root / 'server.log').write_bytes(b'original\r\nbytes\n')
            (root / 'client.key').write_text('FAKE_PRIVATE_KEY_SENTINEL')
            (root / 'pg_service.conf').write_text('FAKE_PROFILE_SENTINEL')
            mode = root / 'startup'
            mode.mkdir()
            (mode / 'ready-1').touch()
            target = Path(directory) / 'build'
            retain_stall_evidence(root, target)
            retained = target / 'Testing/Temporary'
            self.assertEqual((retained / 'server.log').read_bytes(), b'original\r\nbytes\n')
            self.assertEqual(json.loads((retained / 'stall-file-times.log').read_text())[0]['marker'], 'ready-1')
            payload = b''.join(path.read_bytes() for path in retained.iterdir())
            self.assertNotIn(b'FAKE_PRIVATE_KEY_SENTINEL', payload)
            self.assertNotIn(b'FAKE_PROFILE_SENTINEL', payload)
            self.assertFalse((retained / 'client.key').exists())
            self.assertFalse((retained / 'pg_service.conf').exists())


if __name__ == '__main__':
    unittest.main()
