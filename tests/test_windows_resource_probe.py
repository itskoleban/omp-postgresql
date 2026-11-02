"""Verify that resource supervision preserves failures and terminates timed-out children."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
from unittest import mock
import unittest


source = Path(__file__).resolve().parents[1] / 'scripts/probe_windows_resources.py'
spec = importlib.util.spec_from_file_location('probe_windows_resources', source)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ResourceSupervisorTests(unittest.TestCase):
    def run_child(self, child, query=None):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'control.log'
            with mock.patch.object(probe.subprocess, 'Popen', return_value=child), \
                    mock.patch.object(probe, 'postexit_handles', return_value=query or dict(ok=True, count=0, error=0)):
                return probe.run_one(Path('probe.exe'), 'observer', output)

    def test_timeout_kills_and_reaps_exact_child_before_recording_failure(self):
        child = mock.Mock(pid=123)
        child.wait.side_effect = [subprocess.TimeoutExpired('probe.exe', 60), 1]
        child.poll.return_value = 1
        result = self.run_child(child)
        child.kill.assert_called_once_with()
        self.assertEqual(child.wait.call_args_list, [mock.call(timeout=60), mock.call(timeout=10)])
        self.assertEqual((result['pid'], result['exit_code'], result['status']), (123, 1, 'FAIL'))
        self.assertTrue(result['timed_out'])
        self.assertTrue(result['process_shutdown_confirmed'])
        self.assertEqual(result['postexit_handles'], dict(ok=True, count=0, error=0))

    def test_child_failure_and_denied_postexit_query_are_retained(self):
        child = mock.Mock(pid=456)
        child.wait.return_value = 9
        child.poll.return_value = 9
        result = self.run_child(child, dict(ok=False, count=None, error=5))
        child.kill.assert_not_called()
        self.assertEqual((result['pid'], result['exit_code'], result['status']), (456, 9, 'FAIL'))
        self.assertEqual(result['postexit_handles'], dict(ok=False, count=None, error=5))
        self.assertIn('sha256', result['log'])

    def test_supervisor_exception_does_not_abandon_running_child(self):
        child = mock.Mock(pid=789)
        child.wait.side_effect = [OSError('injected wait failure'), 1]
        child.poll.side_effect = [None, 1]
        result = self.run_child(child)
        child.kill.assert_called_once_with()
        self.assertTrue(result['process_shutdown_confirmed'])
        self.assertEqual(result['status'], 'FAIL')
        self.assertIn('injected wait failure', result['error'])

    def test_successful_child_does_not_hide_failed_postexit_observation(self):
        child = mock.Mock(pid=321)
        child.wait.return_value = 0
        child.poll.return_value = 0
        result = self.run_child(child, dict(ok=False, count=None, error=5))
        self.assertEqual(result['exit_code'], 0)
        self.assertEqual(result['status'], 'FAIL')
        self.assertEqual(result['postexit_handles']['error'], 5)

    def test_log_hash_failure_retains_process_outcome(self):
        child = mock.Mock(pid=654)
        child.wait.return_value = 0
        child.poll.return_value = 0
        with mock.patch.object(Path, 'read_bytes', side_effect=PermissionError('injected log read failure')):
            result = self.run_child(child)
        self.assertEqual((result['pid'], result['exit_code'], result['status']), (654, 0, 'FAIL'))
        self.assertTrue(result['process_shutdown_confirmed'])
        self.assertEqual(result['postexit_handles']['count'], 0)
        self.assertIn('injected log read failure', result['log_error'])


if __name__ == '__main__':
    unittest.main()
