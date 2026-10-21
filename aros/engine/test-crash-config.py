#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("decoder", Path(__file__).with_name("decode-crash-config.py"))
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


class ConfigDecode(unittest.TestCase):
    valid = b"SLOG_CONFIG_BEGIN=4\r\nSLOG_CONFIG 01234567\r\nSLOG_CONFIG_END=0\r\n"

    def test_valid(self):
        self.assertEqual(decoder.decode(self.valid), bytes.fromhex("01234567"))

    def test_known_interleaving(self):
        note = b"0000000000000001 0x1234 | 000 | [security.library] secTaskNotify: task 0123 removed\r\n"
        capture = self.valid.replace(b"01234567", b"0123" + note + b"4567")
        self.assertEqual(decoder.decode(capture), bytes.fromhex("01234567"))

    def test_reject_malformed(self):
        cases = [self.valid.replace(b"BEGIN=4", b"BEGIN=5"),
                 self.valid.replace(b"01234567", b"0123Z567"),
                 self.valid.replace(b"01234567", b"0123456"),
                 self.valid.replace(b"END=0", b"END=5"),
                 self.valid + self.valid,
                 self.valid.split(b"SLOG_CONFIG_END")[0],
                 self.valid.replace(b"BEGIN=4", b"BEGIN=1048577"),
                 self.valid.replace(b"01234567", b"0123unexpected\n4567")]
        for capture in cases:
            with self.subTest(capture=capture), self.assertRaises(ValueError):
                decoder.decode(capture)


if __name__ == "__main__":
    unittest.main()
