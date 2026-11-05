"""The harness itself must not turn failed commands into successful test runs."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch

# Standalone discovery works as well as discovery through tests/run.py.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run as runner
from support import run


class RunnerTests(unittest.TestCase):
    def test_registry_has_unique_names(self):
        names = [c.name for c in runner.cases()]
        self.assertEqual(len(names), len(set(names)))

    def test_default_is_self_contained_and_all_excludes_private_images(self):
        self.assertFalse({n for c in runner.select(['default']) for n in c.needs}
                         & {'qemu', 'fuse', 'image'})
        self.assertFalse(any('image' in c.needs for c in runner.select(['all'])))
        with self.assertRaises(ValueError):
            runner.select(['misspelled'])

    def test_missing_qemu_is_an_error(self):
        with patch.object(runner.os, 'access', return_value=True), \
                patch.object(runner.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(ValueError, 'require qemu-img'):
                runner.preflight(runner.select(['qcow2']), {
                    key: '/unused' for key in ['NEXTUFS', 'QCOW2_PROBE', 'QCOW2_FAULT_BIN', 'CORRUPT']})

    def test_offline_binary_rejected_before_fuse_cases(self):
        with patch.object(runner.os, 'access', return_value=True), \
                patch.object(runner.shutil, 'which', return_value='/unused'), \
                patch.object(runner.subprocess, 'run', return_value=subprocess.CompletedProcess(
                    [], 2, b'', b'mount support is unavailable')):
            with self.assertRaisesRegex(ValueError, 'WITH_FUSE=1'):
                runner.preflight(runner.select(['fuse/read']), {'NEXTUFS': '/unused'})

    def test_command_failure_is_not_hidden(self):
        with self.assertRaises(AssertionError):
            run(sys.executable, '-c', 'raise SystemExit(8)')

    def test_failed_assertion_preserves_log_and_reports_failure(self):
        self.check_run('assert False, "deliberate comparison failure"', expected=1)

    def test_checker_crash_is_not_clean(self):
        self.check_run('import os, signal; os.kill(os.getpid(), signal.SIGKILL)', expected=1)

    def test_timeout_is_failure_and_retains_artifacts(self):
        self.check_run('import time; time.sleep(60)', expected=1, options=['--timeout', '0'])

    def test_success_removes_artifacts(self):
        self.check_run('pass', expected=0)

    def check_run(self, code, expected, options=()):
        case = runner.Case('synthetic', (sys.executable, '-c', code))
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(runner, 'cases', return_value=[case]), \
                    patch.object(runner, 'preflight'), redirect_stdout(io.StringIO()):
                status = runner.main(['synthetic', '--work-root', directory, *options])
            self.assertEqual(status, expected)
            results = list(Path(directory).rglob('results.json'))
            if expected:
                self.assertEqual(len(results), 1)
                self.assertNotEqual(json.loads(results[0].read_text())[0]['status'], 0)
                self.assertEqual(len(list(Path(directory).rglob('output.log'))), 1)
            else:
                self.assertEqual(list(Path(directory).iterdir()), [])


if __name__ == '__main__':
    unittest.main()
