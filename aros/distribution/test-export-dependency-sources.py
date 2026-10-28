#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('exporter', Path(__file__).with_name('export-dependency-sources.py'))
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


class ExportTests(unittest.TestCase):
    def test_manifest_rejects_drift_duplicates_empty_and_relative(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            file = root / 'source.c'
            file.write_text('retained source')
            record = root / 'source.sha256'
            line = f'{exporter.digest(file)}  {file}\n'
            record.write_text(line)
            self.assertEqual(exporter.manifest(record), {file: exporter.digest(file)})
            for invalid in ('', line + line, line.replace(str(file), 'source.c'), 'bad\n'):
                record.write_text(invalid)
                with self.assertRaises(ValueError):
                    exporter.manifest(record)
            record.write_text(line)
            file.write_text('changed')
            with self.assertRaisesRegex(ValueError, 'Changed'):
                exporter.manifest(record)

    def test_environment_is_data_not_shell(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'env'
            path.write_text('export AROS_SDK=/selected/sdk\n')
            self.assertEqual(exporter.environment(path)['AROS_SDK'], Path('/selected/sdk'))
            for bad in ('touch /tmp/no\n', 'export AROS_SDK=/a\nexport AROS_SDK=/b\n',
                        'export AROS_SDK=/a; touch /tmp/no\n'):
                path.write_text(bad)
                with self.assertRaises(ValueError):
                    exporter.environment(path)

    def test_copy_preserves_exact_bytes_and_rejects_unrecorded_or_symlink(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'source'
            source.mkdir()
            file = source / 'LICENSE'
            file.write_text('unaltered license')
            checked = {file: exporter.digest(file)}
            target = root / 'target'
            self.assertEqual(exporter.copy_tree(source, target, checked), 1)
            self.assertEqual((target / 'LICENSE').read_bytes(), file.read_bytes())
            with self.assertRaises(FileExistsError):
                exporter.copy_tree(source, target, checked)
            with self.assertRaisesRegex(ValueError, 'Unrecorded'):
                exporter.copy_tree(source, root / 'unrecorded', {})
            (source / 'escape').symlink_to(file)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                exporter.copy_tree(source, root / 'symlink', checked)


if __name__ == '__main__':
    unittest.main()
