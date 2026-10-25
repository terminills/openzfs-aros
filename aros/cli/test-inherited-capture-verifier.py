#!/usr/bin/env python3
# SPDX-License-Identifier: CDDL-1.0
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('capture_verify', Path(__file__).with_name('verify-inherited-capture.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    lines = ['RESULT inherited capture BEGIN runs=16 workers=1']
    outcomes = [(0, 23, 0), (0, 23, 3), (0, 23, 2), (85, 23, 0),
                (84, 23, 0), (8, -1, 0), (0, 23, 1)]
    for run in range(16):
        for mode, (error, status, count) in enumerate(outcomes):
            lines.append(f'RESULT inherited capture worker=0 run={run} mode={mode} error={error} status={status} lines={count}')
    lines += ['RESULT inherited capture PASS runs=16 workers=1 cases=112 cleanup=1',
              'RESULT inherited capture shell PASS', 'RESULT inherited capture audit DONE']
    return '\n'.join(lines)


class VerifierTests(unittest.TestCase):
    def test_complete(self):
        self.assertEqual(module.verify(fixture(), 1), 112)

    def test_reject_corruption(self):
        text = fixture()
        row = text.splitlines()[1]
        cases = {
            'missing': text.replace(row, '', 1),
            'duplicate': text.replace(row, row + '\n' + row, 1),
            'errno': text.replace('error=85', 'error=0', 1),
            'status': text.replace('status=23', 'status=0', 1),
            'truncated': text.replace('RESULT inherited capture audit DONE', ''),
            'failure': text + '\nRESULT inherited capture FAIL line=1',
            'order': text.replace('RESULT inherited capture audit DONE', '') + '\n' + row,
        }
        for name, value in cases.items():
            with self.subTest(name=name), self.assertRaises(ValueError):
                module.verify(value, 1)


if __name__ == '__main__':
    unittest.main()
