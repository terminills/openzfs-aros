#!/usr/bin/env python3
"""Host-side safety guards; never starts or signals a VM."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HELPER = Path(__file__).with_name('qmp-recovery-cut.py')


class CutGuards(unittest.TestCase):
    def invoke(self, folder, timeout='60'):
        # This Python test process is deliberately not a QEMU process.
        result = subprocess.run([sys.executable, str(HELPER), str(folder),
                                 str(os.getpid()), '--timeout', timeout],
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(list(Path(folder).iterdir()), [])
        return result.stderr

    def test_non_qemu_pid_is_refused_before_creating_log(self):
        with tempfile.TemporaryDirectory(prefix='native-import-run.') as folder:
            self.assertIn('PID is not this run', self.invoke(folder))

    def test_unrelated_directory_is_refused(self):
        with tempfile.TemporaryDirectory(prefix='not-a-native-run.') as folder:
            self.assertIn('invalid private run', self.invoke(folder))

    def test_unbounded_deadline_is_refused(self):
        with tempfile.TemporaryDirectory(prefix='native-import-run.') as folder:
            self.assertIn('invalid private run or deadline', self.invoke(folder, '999999'))


if __name__ == '__main__':
    unittest.main()
