#!/usr/bin/env python3
"""Invalid exports must fail before invoking compilers or allocating builds."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class ReplayGuardTests(unittest.TestCase):
    def test_invalid_exports_fail_before_build(self):
        script = Path(__file__).with_name('rebuild-exported-dependencies.sh')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'source.c'
            source.write_text('original')
            sha = hashlib.sha256(source.read_bytes()).hexdigest()
            record = f'{sha}  source.c\n'
            env = dict(os.environ, AROS_CC='/not-invoked', AROS_CXX='/not-invoked',
                       AROS_SDK='/not-invoked', AROS_HOST_GO='/not-invoked',
                       AROS_ARTIFACT_ROOT=str(root), AROS_BUILD_JOBS='8')
            cases = [('bad\n', 'Malformed'), ('', 'Incomplete'),
                     (record + record, 'duplicate'),
                     (f'{sha}  ../outside\n', 'Unsafe'),
                     (f'{sha}  {source}\n', 'Unsafe'),
                     ('0' * 64 + '  source.c\n', 'changed')]
            for text, diagnostic in cases:
                with self.subTest(diagnostic=diagnostic, text=text):
                    (root / 'SHA256SUMS').write_text(text)
                    result = subprocess.run(['bash', str(script), str(root)], env=env,
                                            capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(diagnostic, result.stderr)
                    self.assertNotIn('Exported dependency rebuild:', result.stdout)
                    self.assertFalse(list(root.glob('exported-dependencies.*')))


if __name__ == '__main__':
    unittest.main()
