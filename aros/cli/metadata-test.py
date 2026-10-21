#!/usr/bin/env python3
import pathlib
import runpy
import unittest

generate = runpy.run_path(str(pathlib.Path(__file__).with_name('generate-metadata.py')))['header']


class MetadataTests(unittest.TestCase):
    def test_release(self):
        self.assertIn('#define ZFS_META_ALIAS "zfs-2.4.4-1"',
                      generate('Name: zfs\nVersion: 2.4.4\nRelease: 1\n'))

    def test_optional_release(self):
        self.assertIn('#define ZFS_META_ALIAS "zfs-2.4.4"',
                      generate('Name: zfs\nVersion: 2.4.4\n'))

    def test_invalid(self):
        for source in ('Version: 1', 'Name: zfs',
                       'Name: zfs\nVersion: 1\nVersion: 2',
                       'Name: zfs\nVersion: "bad"',
                       'Name: zfs\nVersion: 1\nRelease: x y', 'not metadata'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                generate(source)

    def test_checkout(self):
        meta = pathlib.Path(__file__).resolve().parents[2] / 'META'
        self.assertIn('#define ZFS_META_ALIAS "zfs-2.4.4-1"', generate(meta.read_text()))


if __name__ == '__main__':
    unittest.main()
