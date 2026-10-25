#!/usr/bin/env python3
"""Host checks for retained POSIX source closure; not native runtime proof."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'snapshot', Path(__file__).with_name('build-posix-snapshot.py'))
snapshot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(snapshot)


class PosixSnapshot(unittest.TestCase):
    def test_local_header_and_missing_definition(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                snapshot.localize_libdefs(root)
            (root / 'posixc_libdefs.h').write_text('/* retained ABI */\n')
            (root / 'posixc_deflibdefs.h').write_text(
                '#define LC_LIBDEFS_FILE "/private/generated/header.h"\n')
            snapshot.localize_libdefs(root)
            self.assertEqual((root / 'posixc_deflibdefs.h').read_text(),
                             '#define LC_LIBDEFS_FILE "posixc_libdefs.h"\n')

    def test_private_dependencies_rejected_including_aliases(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / 'private'
            build.mkdir()
            alias = root / 'alias'
            alias.symlink_to(build, target_is_directory=True)
            for dependency in (build / 'header.h', alias / 'header.h'):
                (root / 'rebuild.json').write_text(json.dumps({
                    'external_inputs': {str(dependency): 'hash'}}))
                with self.assertRaises(ValueError):
                    snapshot.check_private_dependencies(root, build)
            (root / 'rebuild.json').write_text(json.dumps({
                'external_inputs': {str(root / 'sdk/header.h'): 'hash'}}))
            snapshot.check_private_dependencies(root, build)


if __name__ == '__main__':
    unittest.main()
