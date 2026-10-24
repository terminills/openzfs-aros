#!/usr/bin/env python3
"""Host-only command/provenance tests; not native runtime qualification."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('record', Path(__file__).with_name('record-source-build.py'))
record = importlib.util.module_from_spec(spec)
spec.loader.exec_module(record)


class Recording(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'build'
        self.root.mkdir()

    def invoke(self, *args):
        with patch('sys.argv', ['record', *args]):
            record.main()

    def test_run_preserves_argv_and_adds_dependency_capture(self):
        obj = self.root / 'sample.o'
        with patch.object(record.subprocess, 'run') as run:
            self.invoke('run', str(self.root), '/bin/true', '-c', 'sample.c', '-o', str(obj))
        command = json.loads((self.root / 'build-commands.jsonl').read_text())
        self.assertEqual(command[-3:], ['-MD', '-MF', str(obj.with_suffix('.d'))])
        run.assert_called_once_with(command, check=True)

    def test_failure_is_not_silenced(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.invoke('run', str(self.root), '/bin/false')
        self.assertFalse((self.root / 'rebuild.json').exists())

    def test_seal_binds_local_and_external_inputs(self):
        source = self.root / 'sample.c'
        source.write_text('source')
        external = self.root.parent / 'external.h'
        external.write_text('header')
        (self.root / 'sample.d').write_text(f'sample.o: {source} \\\n {external}\n')
        (self.root / 'sample.map').write_text('')
        (self.root / 'sample.library').write_bytes(b'binary')
        (self.root / 'build-commands.jsonl').write_text(json.dumps(['/bin/true']) + '\n')
        self.invoke('seal', str(self.root), 'sample.library', 'sample.map')
        recipe = json.loads((self.root / 'rebuild.json').read_text())
        self.assertEqual(recipe['artifact'], 'sample.library')
        self.assertEqual(recipe['expected_binary'], hashlib.sha256(b'binary').hexdigest())
        self.assertIn(str(external), recipe['external_inputs'])
        self.assertNotIn(str(source), recipe['external_inputs'])
        self.assertIn('  sample.c\n', (self.root / 'retained.sha256').read_text())

        # Re-sealing must not hash the previous manifest into its replacement.
        original_manifest = (self.root / 'retained.sha256').read_text()
        self.invoke('seal', str(self.root), 'sample.library', 'sample.map')
        manifest = (self.root / 'retained.sha256').read_text()
        self.assertEqual(manifest, original_manifest)
        self.assertNotIn('  retained.sha256\n', manifest)
        for line in manifest.splitlines():
            expected, name = line.split('  ', 1)
            self.assertEqual(record.digest(self.root / name), expected)

    def test_seal_rejects_nonlocal_output(self):
        with self.assertRaises(ValueError):
            self.invoke('seal', str(self.root), '../out', 'sample.map')


if __name__ == '__main__':
    unittest.main()
