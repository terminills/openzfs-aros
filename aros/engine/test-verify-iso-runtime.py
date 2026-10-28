#!/usr/bin/env python3
"""Host-only image-evidence tests; not AROS runtime acceptance."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HELPER = Path(__file__).with_name('verify-iso-runtime.sh')


@unittest.skipUnless(shutil.which('xorriso'), 'xorriso required')
class IsoRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='oz-iso-runtime-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        libs = self.root / 'tree/Libs'
        libs.mkdir(parents=True)
        self.selected = self.root / 'selected.library'
        self.selected.write_bytes(b'qualified runtime\x00\xff')
        shutil.copyfile(self.selected, libs / self.selected.name)
        self.iso = self.root / 'fixture.iso'
        subprocess.run(['xorriso', '-as', 'mkisofs', '-quiet', '-R', '-o',
                        str(self.iso), str(self.root / 'tree')], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def verify(self, *inputs):
        return subprocess.run(['bash', str(HELPER), str(self.iso),
                               str(self.root / 'evidence'), *map(str, inputs)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_exact_runtime_and_no_overwrite(self):
        self.assertEqual(self.verify(self.selected).returncode, 0)
        expected = hashlib.sha256(self.selected.read_bytes()).hexdigest()
        self.assertTrue((self.root / 'evidence.sha256').read_text().startswith(expected + '  '))
        self.assertEqual((self.root / 'evidence/selected.library').read_bytes(), self.selected.read_bytes())
        self.assertNotEqual(self.verify(self.selected).returncode, 0)

    def test_mismatch_rejected(self):
        self.selected.write_bytes(b'different intended input')
        self.assertNotEqual(self.verify(self.selected).returncode, 0)
        self.assertFalse((self.root / 'evidence.sha256').exists())

    def test_missing_iso_library_rejected(self):
        other = self.root / 'absent.library'
        other.write_bytes(b'not in ISO')
        self.assertNotEqual(self.verify(other).returncode, 0)
        self.assertFalse((self.root / 'evidence.sha256').exists())

    def test_duplicate_and_empty_selection_rejected_before_extraction(self):
        self.assertNotEqual(self.verify(self.selected, self.selected).returncode, 0)
        self.assertFalse((self.root / 'evidence').exists())
        self.assertNotEqual(self.verify().returncode, 0)
        self.assertFalse((self.root / 'evidence').exists())


if __name__ == '__main__':
    unittest.main()
