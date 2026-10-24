#!/usr/bin/env python3
"""Snapshot drift/path checks; native qualification is separate."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('replay', Path(__file__).with_name('rebuild-fat-snapshot.py'))
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


class Snapshot(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source.c'
        self.source.write_text('int example;\n')
        self.manifest = self.root / 'retained.sha256'
        self.manifest.write_text(replay.digest(self.source) + '  source.c\n')

    def test_exact(self):
        self.assertEqual(replay.verify_snapshot(self.root), [Path('source.c')])

    def test_changed(self):
        self.source.write_text('int changed;\n')
        with self.assertRaises(ValueError):
            replay.verify_snapshot(self.root)

    def test_missing(self):
        self.source.unlink()
        with self.assertRaises(FileNotFoundError):
            replay.verify_snapshot(self.root)

    def test_traversal_and_absolute(self):
        for name in ('../source.c', str(self.source)):
            self.manifest.write_text(replay.digest(self.source) + '  ' + name + '\n')
            with self.assertRaises(ValueError):
                replay.verify_snapshot(self.root)

    def test_symlink(self):
        link = self.root / 'alias.c'
        link.symlink_to(self.source)
        self.manifest.write_text(replay.digest(self.source) + '  alias.c\n')
        with self.assertRaises(ValueError):
            replay.verify_snapshot(self.root)


if __name__ == '__main__':
    unittest.main()
