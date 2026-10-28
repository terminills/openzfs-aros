#!/usr/bin/env python3
"""Synthetic log layout tests; not native cache or rebuild qualification."""
import importlib.util
import io
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("fixture", Path(__file__).with_name("make-zdb-l2arc.py"))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class Logs(unittest.TestCase):
    def test_fletcher_known_words(self):
        self.assertEqual(fixture.fletcher4(struct.pack("<4I", 1, 2, 3, 4)), (10, 20, 35, 56))

    def test_endian_layout_and_corruption(self):
        offset = 4 * 1024 * 1024
        for endian in ("<", ">"):
            with self.subTest(endian=endian):
                good, bad = io.BytesIO(), io.BytesIO()
                fixture.log_fixture(good, endian, False, offset, 64*1024*1024)
                fixture.log_fixture(bad, endian, True, offset, 64*1024*1024)
                data = good.getvalue()
                header = struct.unpack_from(endian + "64Q", data, offset)
                self.assertEqual(header[4], 2)
                self.assertEqual(header[25:27], (65536, 1))
                block = data[header[9]:header[9]+65536]
                self.assertEqual(fixture.fletcher4(block), header[13:17])
                self.assertEqual(struct.unpack_from(endian + "Q", block)[0], 0x4c4f47424c4b4844)
                for i in range(2):
                    self.assertEqual(struct.unpack_from(endian + "Q", block, 128+i*64+16)[0], 42+i)
                changed = bad.getvalue()
                differences = [i for i, (a, b) in enumerate(zip(data, changed)) if a != b]
                self.assertEqual(differences, [header[9]+128])
                self.assertNotEqual(fixture.fletcher4(changed[header[9]:header[9]+65536]), header[13:17])


if __name__ == "__main__":
    unittest.main()
