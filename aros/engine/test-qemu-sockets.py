#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('sockets', Path(__file__).with_name('check-qemu-sockets.py'))
sockets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sockets)


class SocketPathTests(unittest.TestCase):
    def test_exact_limit(self):
        sockets.check('/' + 'a' * 96)  # directory 97 + slash + longest name 9 = 107
        with self.assertRaisesRegex(ValueError, '107 bytes'):
            sockets.check('/' + 'a' * 97)

    def test_bytes_not_characters(self):
        sockets.check('/' + '\u00e9' * 48)
        with self.assertRaises(ValueError):
            sockets.check('/' + '\u00e9' * 49)


if __name__ == '__main__':
    unittest.main()
