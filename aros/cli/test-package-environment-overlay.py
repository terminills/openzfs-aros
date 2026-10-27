#!/usr/bin/env python3
"""Exercise the overlay against the selected real AROS package tool."""
import io
import lzma
import os
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
import unittest


class OverlayTests(unittest.TestCase):
    def test_preservation_and_invalid_size(self):
        tool = Path(os.environ['AROS_PACKAGE_TOOL']).resolve(strict=True)
        api = runpy.run_path(str(tool))
        script = Path(__file__).with_name('package-environment-overlay.py')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            members = root / 'members'
            members.mkdir()
            (members / 'partition.library').write_bytes(b'original')
            (members / 'unrelated.library').write_bytes(b'preserve\0exactly')
            raw = root / 'input.pkg'
            subprocess.run([sys.executable, str(tool), 'c', str(raw), str(members),
                            'partition.library', 'unrelated.library'], check=True,
                           stdout=subprocess.DEVNULL)
            original = raw.read_bytes()
            for name, data, accepted in (
                ('valid', original, True),
                ('short-size', original[:4] + struct.pack('!I', 8) + original[8:], False),
                ('long-size', original[:4] + struct.pack('!I', len(original)+1) + original[8:], False),
                ('truncated', original[:-1], False),
            ):
                with self.subTest(name=name):
                    base = root / (name + '.xz')
                    base.write_bytes(lzma.compress(data))
                    out = root / name
                    out.mkdir()
                    (out / 'partition.library').write_bytes(b'replacement')
                    result = subprocess.run([sys.executable, str(script), str(base),
                                             str(out), 'partition.library', 'aros-fs'],
                                            capture_output=True)
                    self.assertEqual(result.returncode == 0, accepted, result.stderr)
                    self.assertEqual(base.read_bytes(), lzma.compress(data))
                    if not accepted:
                        self.assertFalse((out / 'aros-fs.pkg.xz').exists())
                        continue
                    stream = io.BytesIO(lzma.decompress((out / 'aros-fs.pkg.xz').read_bytes()))
                    api['readHeader'](stream)
                    entries = [api['readFile'](stream), api['readFile'](stream)]
                    self.assertEqual([(n.rstrip(b'\0'), d) for n, d in entries],
                                     [(b'partition.library', b'replacement'),
                                      (b'unrelated.library', b'preserve\0exactly')])
                    self.assertEqual(stream.read(), b'')


if __name__ == '__main__':
    unittest.main()
