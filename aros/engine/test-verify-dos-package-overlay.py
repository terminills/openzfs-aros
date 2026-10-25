#!/usr/bin/env python3
import pathlib
import runpy
import unittest

verify = runpy.run_path(str(pathlib.Path(__file__).with_name(
    'verify-dos-package-overlay.py')))['verify']


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.original = [('exec.library', b'exec'), ('dos.library', b'old'),
                         ('debug.library', b'debug')]

    def test_dos_only(self):
        verify(self.original, [self.original[0], ('dos.library', b'new'), self.original[2]])

    def test_reject_unchanged(self):
        with self.assertRaises(ValueError):
            verify(self.original, self.original)

    def test_reject_other_change(self):
        with self.assertRaises(ValueError):
            verify(self.original, [('exec.library', b'new'), ('dos.library', b'new'), self.original[2]])

    def test_reject_reorder(self):
        with self.assertRaises(ValueError):
            verify(self.original, [self.original[2], ('dos.library', b'new'), self.original[0]])

    def test_reject_missing_module(self):
        with self.assertRaises(ValueError):
            verify(self.original, self.original[:2])

    def test_ahci_only(self):
        original = [('ata.device', b'ata'), ('ahci.device', b'old')]
        verify(original, [original[0], ('ahci.device', b'new')], 'ahci.device')
        with self.assertRaises(ValueError):
            verify(original, [('ata.device', b'changed'), ('ahci.device', b'new')], 'ahci.device')
        with self.assertRaises(ValueError):
            verify(original, original, 'ahci.device')

    def test_unapproved_member(self):
        with self.assertRaises(ValueError):
            verify(self.original, self.original, 'exec.library')

    def test_ata_only(self):
        original = [('ata.device', b'old'), ('ahci.device', b'ahci')]
        verify(original, [('ata.device', b'new'), original[1]], 'ata.device')
        for candidate in (original,
                          [('ata.device', b'new'), ('ahci.device', b'changed')],
                          [original[1], ('ata.device', b'new')],
                          [('ata.device', b'new')]):
            with self.subTest(candidate=candidate), self.assertRaises(ValueError):
                verify(original, candidate, 'ata.device')


if __name__ == '__main__':
    unittest.main()
