#!/usr/bin/env python3
"""Keep hosted reports from being accepted as ISO/QEMU evidence."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class EvidenceModes(unittest.TestCase):
    def test_modes_are_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            expected = ["    name: 'topo_source'", "    labels = 0 1 2 3 "]
            (root / "host.log").write_text("\n".join(expected) + "\n")
            for storage in ("readonly-iso", "private-hosted"):
                lines = []
                for index in range(2):
                    lines.extend(f"ZDB[{index}] {line}" for line in expected)
                    lines.append(f"RESULT native zdb labels run={index} exit=0")
                lines.append(f"RESULT native zdb labels PASS runs=2 storage={storage}")
                (root / "serial.log").write_text("\n".join(lines))
                for args in ([], ["--hosted"], ["--unknown"]):
                    result = subprocess.run([sys.executable, str(Path(__file__).with_name("verify-zdb-labels.py")),
                                             str(root / "serial.log"), str(root / "host.log"), *args],
                                            capture_output=True)
                    valid = args == (["--hosted"] if storage == "private-hosted" else [])
                    self.assertEqual(result.returncode == 0, valid)


if __name__ == "__main__":
    unittest.main()
