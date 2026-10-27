#!/usr/bin/env python3
import importlib.util
import pathlib
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('inputs', pathlib.Path(__file__).with_name('record-build-inputs.py'))
inputs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inputs)


class InputTests(unittest.TestCase):
    def test_gcc_rules_and_escaped_paths(self):
        target, paths = inputs.dependency_words('obj.o: src.c \\\n sdk/header.h escaped\\ space.h hash\\#.h dollar$$.h\nsdk/header.h:\n')
        self.assertEqual(target, 'obj.o')
        self.assertEqual(paths, ['src.c', 'sdk/header.h', 'escaped space.h', 'hash#.h', 'dollar$.h'])
        for bad in ('', 'obj.o', 'obj.o:', ': x', 'obj.o: trailing\\'):
            with self.assertRaises(ValueError):
                inputs.dependency_words(bad)

    def test_drift_and_symlink_retargeting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            header, other, link, obj = (root / n for n in ('header.h', 'other.h', 'link.h', 'object.o'))
            header.write_text('first')
            other.write_text('second')
            link.symlink_to(header)
            obj.write_bytes(b'object')
            manifest = {'format': 1, 'object_count': 1,
                        'inputs': [{'path': str(link), 'sha256': inputs.sha256(link)}],
                        'objects': [{'path': str(obj), 'sha256': inputs.sha256(obj)}],
                        'linked_object': {'path': str(obj), 'sha256': inputs.sha256(obj)}}
            inputs.verify(manifest)
            link.unlink(); link.symlink_to(other)
            with self.assertRaises(ValueError):
                inputs.verify(manifest)
            link.unlink(); link.symlink_to(header)
            obj.write_bytes(b'changed')
            with self.assertRaises(ValueError):
                inputs.verify(manifest)

    def test_missing_inventory_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'objects').mkdir()
            for count in (0, 1):
                with self.assertRaises(ValueError):
                    inputs.record(root, root, count)

    def test_equal_counts_do_not_accept_stale_object_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'objects').mkdir()
            (root / 'objects/stale.c.o.d').write_text('stale.c.o: stale.c\n')
            (root / 'sources.txt').write_text('current.c\n')
            with self.assertRaisesRegex(ValueError, 'paths differ'):
                inputs.record(root, root, 1)
            (root / 'sources.txt').write_text('../outside.c\n')
            with self.assertRaisesRegex(ValueError, 'Unsafe'):
                inputs.record(root, root, 1)


if __name__ == '__main__':
    unittest.main()
