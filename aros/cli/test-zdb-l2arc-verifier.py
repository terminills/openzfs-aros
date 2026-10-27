#!/usr/bin/env python3
"""Negative controls for the L2ARC evidence comparator (not runtime proof)."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("verifier", Path(__file__).with_name("verify-zdb-l2arc.py"))
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class Evidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "mode.txt").write_text("l2arc_header_device=1\n")
        (self.root / "l2arc-device-unchanged.log").write_text("Images are identical.\n")
        lines = []
        for i, name in enumerate(("little", "big", "missing", "little")):
            report = "    state: 4\n" + ("L2ARC device header not found\n" if name == "missing" else "No log blocks to read\n")
            (self.root / f"host-l2arc-{name}.log").write_text(report)
            lines += [f"ZDBL2[{i}] {line}" for line in report.splitlines()]
            lines += [f"RESULT native zdb l2arc pass={i} exit=0"]
        lines += ["RESULT native zdb l2arc PASS headers=3 storage=readonly-iso"]
        self.serial = self.root / "serial.log"
        self.serial.write_text("\n".join(lines) + "\n")

    def check(self):
        with contextlib.redirect_stdout(io.StringIO()):
            verifier.verify(self.root)

    def test_matching(self):
        self.check()

    def test_wrong_status(self):
        self.serial.write_text(self.serial.read_text().replace("pass=3 exit=0", "pass=3 exit=1"))
        with self.assertRaises(ValueError): self.check()

    def test_wrong_output(self):
        self.serial.write_text(self.serial.read_text().replace("ZDBL2[1]     state: 4", "ZDBL2[1]     state: 0"))
        with self.assertRaises(ValueError): self.check()

    def test_missing_native(self):
        self.serial.write_text("\n".join(x for x in self.serial.read_text().splitlines() if "[3]" not in x and "pass=3" not in x))
        with self.assertRaises(ValueError): self.check()

    def test_changed_device(self):
        (self.root / "l2arc-device-unchanged.log").write_text("Content mismatch\n")
        with self.assertRaises(ValueError): self.check()

    def test_native_fault(self):
        self.serial.write_text(self.serial.read_text() + "[PF-DBG] fault\n")
        with self.assertRaises(ValueError): self.check()


if __name__ == "__main__":
    unittest.main()
