#!/usr/bin/env python3
"""Fail-closed device evidence parsing; not a device I/O test."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class Tests(unittest.TestCase):
    def test_three_modes_and_rejections(self):
        verifier = Path(__file__).with_name("verify-zdb-device.py")
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            expected = ["    name: 'topo_source'", "    labels = 0 1 2 3 "]
            (run/"host-labels.log").write_text("\n".join(expected)+"\n")
            for mode in ("whole", "partitions", "unconfigured", "native-pool"):
                with self.subTest(mode=mode):
                    nosec = mode == "unconfigured"
                    partitioned = mode in ("partitions", "native-pool")
                    pool = mode == "native-pool"
                    (run/"mode.txt").write_text("native_device=1\n" +
                        ("unconfigured_security=1\n" if nosec else
                         "native_partitions=1\n" if partitioned else "") +
                        ("native_pool=1\n" if pool else ""))
                    identity = "security-unconfigured=1" if nosec else "uid=77 gid=88"
                    lines = ["RESULT native zdb actual caller "+identity]
                    path = "/AROS/ata.device/0" + ("/partition/0" if partitioned else "")
                    for i in range(1 if nosec else 3):
                        denied = nosec or i == 1
                        lines.append(f"RESULT native zdb device pass={i} exit={int(denied)}")
                        denial = "cannot admit native devices: Permission denied" if pool else f"cannot open '{path}': Permission denied"
                        report = [denial] if denied else expected
                        lines.extend(f"ZDBDEV[{i}] {x}" for x in report)
                    lines.append("RESULT native zdb device PASS " +
                                 ("no-security=1" if nosec else "root-denied-root=1"))
                    def check(content):
                        (run/"serial.log").write_text("\n".join(content))
                        return subprocess.run([sys.executable,str(verifier),str(run)],
                            capture_output=True).returncode
                    self.assertEqual(check(lines), 0)
                    for damaged in (lines[1:], lines[:-1], lines+[lines[1]],
                                    lines+["[PF-DBG] failure"],
                                    [x.replace("Permission denied", "No such file") for x in lines]):
                        self.assertNotEqual(check(damaged), 0)


if __name__ == "__main__":
    unittest.main()
