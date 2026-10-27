#!/usr/bin/env python3
"""Fixture mutation boundaries and fail-closed transcript tests."""
import importlib.util
import tempfile
import unittest
from pathlib import Path


def load(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


prepare = load("make-zdb-bad-labels").prepare
verify = load("verify-zdb-bad-labels").verify


class Tests(unittest.TestCase):
    def test_exact_mutation_and_unchanged_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = bytes([173]) * (2 * 1024 * 1024)
            source = root / "source"
            source.write_bytes(original)
            destination = root / "out"
            prepare(source, destination)
            for name, indices in (("one-bad", [0]), ("all-bad", range(4))):
                expected = bytearray(original)
                offsets = [0, 262144, len(original)-524288, len(original)-262144]
                for i in indices:
                    start = offsets[i] + 16384
                    expected[start:start+114688] = bytes(114688)
                self.assertEqual((destination/name).read_bytes(), expected)
            self.assertEqual(source.read_bytes(), original)
            with self.assertRaises(FileExistsError):
                prepare(source, destination)

    def test_refuses_small_or_unaligned(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            for size in (1, 1048577):
                source.write_bytes(bytes(size))
                with self.assertRaises(ValueError):
                    prepare(source, root / "out")
                self.assertFalse((root / "out").exists())

    def test_transcripts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reports = {"one-bad": "failed to unpack label 0\nlabels = 1 2 3 \n",
                       "all-bad": "".join(f"failed to unpack label {i}\n" for i in range(4)),
                       "labels": "labels = 0 1 2 3 \n"}
            lines = []
            for name, report in reports.items():
                (root/f"host-{name}.log").write_text(report)
            for name, status in (("one-bad", 1), ("all-bad", 2)):
                (root/f"host-{name}.status").write_text(str(status))
            for index, (name, status) in enumerate((("one-bad", 1), ("labels", 0),
                                                    ("all-bad", 2), ("labels", 0))):
                lines.append(f"RESULT native zdb damaged pass={index} exit={status}")
                lines.extend(f"ZDBBAD[{index}] {x}" for x in reports[name].splitlines())
            lines += ["RESULT native zdb damaged PASS cases=4",
                      "RESULT native zdb labels PASS runs=2 storage=readonly-iso"]
            serial = root / "serial.log"
            serial.write_text("\n".join(lines))
            verify(root)
            for bad in (lines[:-1], lines+[lines[0]],
                        [x.replace("pass=2 exit=2", "pass=2 exit=0") for x in lines],
                        [x for x in lines if not x.startswith("ZDBBAD[3]")],
                        lines+["[PF-DBG] unexpected fault"]):
                serial.write_text("\n".join(bad))
                with self.assertRaises(ValueError):
                    verify(root)


if __name__ == "__main__":
    unittest.main()
