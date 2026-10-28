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

    def test_populated_logs_and_rejections(self):
        (self.root / "mode.txt").write_text("l2arc_log_iso=1\n")
        lines = []
        names = ("little", "big", "missing", "log-little", "log-big", "log-bad", "log-little")
        for i, name in enumerate(names):
            if name.startswith("log-"):
                report = "    state: 4\n" + ("Invalid cksum\n" if name == "log-bad" else
                         "log_blk_count:\t 1 with valid cksum\n")
                (self.root / f"host-l2arc-{name}.log").write_text(report)
            else:
                report = (self.root / f"host-l2arc-{name}.log").read_text()
            lines.extend(f"ZDBL2[{i}] {line}" for line in report.splitlines())
            lines.append(f"RESULT native zdb l2arc pass={i} exit={int(name == 'log-bad')}")
        lines += ["RESULT native zdb l2arc PASS headers=3 storage=readonly-iso",
                  "RESULT native zdb l2arc PASS logs=4 endian=2 corrupt=1 recovery=1"]
        self.serial.write_text("\n".join(lines))
        self.check()
        for bad in (lines[:-1],
                    [x.replace("pass=5 exit=1", "pass=5 exit=0") for x in lines],
                    [x for x in lines if "[6]" not in x and "pass=6" not in x],
                    [x.replace("ZDBL2[4] log_blk_count:", "ZDBL2[4] wrong:") for x in lines]):
            self.serial.write_text("\n".join(bad))
            with self.assertRaises(ValueError): self.check()


if __name__ == "__main__":
    unittest.main()
