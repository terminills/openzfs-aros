#!/usr/bin/env python3
import importlib.util
import pathlib
import struct
import unittest

spec = importlib.util.spec_from_file_location('omit', pathlib.Path(__file__).with_name('omit-boot-module.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def package(entries):
    body = b''
    for name, data in entries:
        padded = (len(name) + 4) & ~3
        body += struct.pack('!I', padded - 1) + name.encode().ljust(padded, b'\0')
        body += struct.pack('!I', len(data)) + data
    return b'PKG\x01' + struct.pack('!I', len(body) + 8) + body


class Tests(unittest.TestCase):
    def test_exact_retention(self):
        raw = package([('ata.device', b'123\0'), ('security.library', b'x'), ('timer.device', b'\xff')])
        self.assertEqual(module.omit(raw, 'security.library'), package([('ata.device', b'123\0'), ('timer.device', b'\xff')]))

    def test_missing(self):
        with self.assertRaises(ValueError):
            module.omit(package([('ata.device', b'123')]), 'security.library')

    def test_duplicate(self):
        with self.assertRaises(ValueError):
            module.records(package([('security.library', b'a'), ('security.library', b'b')]))

    def test_truncations(self):
        raw = package([('security.library', b'abcdef')])
        for end in range(len(raw)):
            broken = raw[:end]
            if end >= 8:
                broken = broken[:4] + struct.pack('!I', end) + broken[8:]
            if end == 8:
                continue  # Empty package is valid.
            with self.assertRaises(ValueError):
                module.records(broken)

    def test_bad_header_and_name(self):
        for raw in [b'BAD\x01' + struct.pack('!I', 8), package([('../escape', b'x')]), package([('a\0b', b'x')])]:
            with self.assertRaises(ValueError):
                module.records(raw)


unittest.main()
