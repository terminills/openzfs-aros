#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('verify', Path(__file__).with_name('verify-zdb-pool.py'))
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)
BLOCKS = 'No leaks (block sum matches space maps exactly)\n' + '\n'.join(
    f'{key}: {value}' for key, value in zip(
        ('bp count', 'ganged count', 'bp logical', 'bp physical', 'bp allocated', 'bp deduped', 'bp cloned'),
        (90, 0, 2637312, 66560, 173568, 0, 0)))
OBJECTS = ('Dataset mos [META], ID 0, 54 objects\n'
           ' 0 2 131072 16384 16896 512 98304 28.12 DMU dnode\n'
           'Verified encryption feature refcount of 0 is correct\n')


def transport(data):
    lines = ['RESULT native zdb pool exit pass=0 status=0']
    for offset in range(0, len(data), 128):
        chunk = data[offset:offset+128]
        lines.append(f'ZDBDATA[0] offset={offset} bytes={len(chunk)} {chunk.hex()}')
    lines.append(f'RESULT native zdb pool report pass=0 bytes={len(data)}')
    return '\n'.join(lines)


class TestVerifier(unittest.TestCase):
    def test_complete_and_rounding(self):
        self.assertEqual(verify.compare(BLOCKS, OBJECTS.replace('28.12', '28.13'), BLOCKS, OBJECTS), [1, 1, 1])

    def test_accounting_mismatch(self):
        with self.assertRaises(ValueError):
            verify.compare(BLOCKS.replace('173568', '173569'), OBJECTS, BLOCKS, OBJECTS)

    def test_missing_inventory(self):
        for text in (OBJECTS.replace('Verified encryption feature refcount of 0 is correct', ''),
                     OBJECTS.replace(' 0 2 131072 16384 16896 512 98304 28.12 DMU dnode', ''),
                     OBJECTS.replace('ID 0', 'ID 1')):
            with self.assertRaises(ValueError):
                verify.compare(BLOCKS, text, BLOCKS, OBJECTS)

    def test_contradictory_integrity(self):
        for error in ('refcount mismatch', 'MOS object 88 leaked', 'ASSERT at test.c:1'):
            with self.assertRaises(ValueError):
                verify.compare(BLOCKS, OBJECTS+error, BLOCKS, OBJECTS)

    def test_lossless_long_lines(self):
        data = b'x'*2048 + b'\r\n' + bytes(range(256))
        self.assertEqual(verify.decode(transport(data), 0), data)

    def test_missing_duplicate_reordered_chunks(self):
        serial = transport(b'x'*256)
        for changed in (serial.replace('offset=128', 'offset=129'),
                        serial.replace('bytes=128', 'bytes=127'),
                        serial.replace('status=0', 'status=1'),
                        serial+'\nRESULT native zdb pool exit pass=0 status=1',
                        serial.replace('bytes=256', 'bytes=255')):
            with self.assertRaises(ValueError):
                verify.decode(changed, 0)


if __name__ == '__main__':
    unittest.main()
