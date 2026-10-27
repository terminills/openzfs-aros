#!/usr/bin/env python3
"""Host-only provenance gate tests; no AROS installation is modified."""
import hashlib
import json
import pathlib
import subprocess
import tempfile
import unittest

GATE = pathlib.Path(__file__).with_name('verify-engine.sh').resolve()


def entry(path):
    return {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


class EngineInputTests(unittest.TestCase):
    def test_selected_manifest_and_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            engine, other, header = (root / n for n in ('engine.o', 'other.o', 'header.h'))
            for path, value in ((engine, 0), (other, 1)):
                subprocess.run(['cc', '-x', 'c', '-c', '-o', str(path), '-'],
                               input=f'int main(void){{return {value};}} void zfsdev_ioctl_common(void){{}}',
                               text=True, check=True, capture_output=True)
            header.write_text('original')
            (root / 'service-build-options.txt').write_text('service_test_modes=0\nfull_dispatcher=1\n')
            (root / 'engine-object.sha256').write_text(entry(engine)['sha256'] + '  engine.o\n')
            def run():
                return subprocess.run(['bash', str(GATE), str(engine), str(root)],
                                      text=True, capture_output=True)
            # Historical builds without this newer manifest keep their existing gate.
            self.assertEqual(run().returncode, 0)
            manifest = {'format': 1, 'object_count': 1, 'inputs': [entry(header)],
                        'objects': [entry(engine)], 'linked_object': entry(engine)}
            target = root / 'compiler-inputs.json'
            target.write_text(json.dumps(manifest))
            result = run()
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('PASS selected engine compiler-input manifest', result.stdout)
            header.write_text('changed')
            result = run()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Changed build input', result.stderr)
            header.write_text('original')
            manifest['objects'] = [entry(other)]
            manifest['linked_object'] = entry(other)
            target.write_text(json.dumps(manifest))
            result = run()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('does not describe selected engine', result.stderr)
            target.write_text('{}')
            self.assertNotEqual(run().returncode, 0)


if __name__ == '__main__':
    unittest.main()
