#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("verify", Path(__file__).with_name("verify-environment-matrix.py"))
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


def fixture(defects=False):
    lines = []
    good = int(not defects)
    for variant, access in (("Plain", 0), ("Environ", 1)):
        for mode in verify.CASES + (("alias", "replace") if access else ()):
            status = "FAIL" if defects and mode not in ("ordinary", "null", "failed") else "PASS"
            final = "nested-final" if mode == "nested" else mode
            lines += [f"RESULT OpenZFS environment matrix {status} mode={final} environ={access}",
                      f"RESULT environment shell variant={variant} mode={mode} exit={status}"]
            if mode not in ("ordinary", "failed"):
                for scope in (mode, "nested-final") if mode == "nested" else (mode,):
                    global_ok = 1 if scope == "null" else good
                    lines.append(f"RESULT OpenZFS environment matrix scope mode={scope} environ={access} value=1 parent=1 global={global_ok}")
        lines += [f"RESULT OpenZFS environment matrix nested-first={good} environ={access}",
                  f"RESULT OpenZFS environment matrix mutations environ={access} local-coherent=1 override=1 unset-hidden={good}",
                  f"RESULT OpenZFS environment matrix failed-exec result=-1 errno=2 intact=1 environ={access}"]
    lines += [f"RESULT OpenZFS environment matrix replacement observed={good}",
              "RESULT environment matrix DONE cases=16"]
    return "\n".join(lines)


class Tests(unittest.TestCase):
    def test_good(self):
        self.assertTrue(verify.validate(fixture()).startswith("PASS:"))

    def test_baseline_is_not_pass(self):
        self.assertIn("NOT qualified", verify.validate(fixture(True), True))
        with self.assertRaises(ValueError):
            verify.validate(fixture(True))

    def test_truncated(self):
        with self.assertRaises(ValueError):
            verify.validate(fixture().replace("RESULT environment matrix DONE cases=16", ""))

    def test_duplicate(self):
        with self.assertRaises(ValueError):
            verify.validate(fixture() + "\nRESULT environment matrix DONE cases=16")

    def test_inner_failure(self):
        with self.assertRaises(ValueError):
            verify.validate(fixture().replace("nested-first=1 environ=0", "nested-first=0 environ=0"))

    def test_shell_mismatch(self):
        with self.assertRaises(ValueError):
            verify.validate(fixture().replace("mode=null exit=PASS", "mode=null exit=FAIL", 1))

    def test_mutation_failure(self):
        with self.assertRaises(ValueError):
            verify.validate(fixture().replace("local-coherent=1", "local-coherent=0", 1))

    def test_fixture_failure(self):
        with self.assertRaises(ValueError):
            verify.validate(fixture() + "\nRESULT OpenZFS environment matrix FAIL mode=fixture environ=0")


if __name__ == "__main__":
    unittest.main()
