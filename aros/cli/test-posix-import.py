#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('posix_import', Path(__file__).with_name('build-posix-import.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class VectorTests(unittest.TestCase):
    def test_exact_vector(self):
        self.assertEqual(module.vector('AROS_GM_STACKCALL(pipe2,PosixC,799);', 'start'), 799)
        self.assertEqual(module.vector('AROS_GM_LIBFUNCSTUB(pipe2, PosixCBase, 799)', 'stub'), 799)

    def test_invalid_vectors(self):
        for text in ('', 'AROS_GM_STACKCALL(pipe,PosixC,799);',
                     'AROS_GM_STACKCALL(pipe2,PosixC,3);',
                     'AROS_GM_STACKCALL(pipe2,PosixC,799);'*2):
            with self.subTest(text=text), self.assertRaises(ValueError):
                module.vector(text, 'start')

    def test_mismatched_generated_abi(self):
        start = 'AROS_GM_STACKCALL(pipe2,PosixC,799);'
        stub = 'AROS_GM_LIBFUNCSTUB(pipe2,PosixCBase,799)'
        self.assertEqual(module.matching_vector(start, stub, start), 799)
        for wrong_stub, wrong_start in ((stub.replace('799', '800'), start),
                                        (stub, start.replace('799', '800'))):
            with self.assertRaisesRegex(ValueError, 'ABI mismatch'):
                module.matching_vector(start, wrong_stub, wrong_start)


if __name__ == '__main__':
    unittest.main()
