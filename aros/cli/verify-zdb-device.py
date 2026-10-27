#!/usr/bin/env python3
"""Verify native root access, unprivileged denial and root reopening."""
import re
import sys
from pathlib import Path

run = Path(sys.argv[1])
serial = (run / "serial.log").read_text().splitlines()
expected = (run / "host-labels.log").read_text().splitlines()
if not expected or not any("name: 'topo_source'" in x for x in expected):
    raise SystemExit("FAIL missing host labels")
if sum(bool(re.search(r"(?:^| \| )RESULT native zdb actual caller uid=77 gid=88$", x)) for x in serial) != 1:
    raise SystemExit("FAIL unverified debugger caller identity")
for index in range(3):
    report = [m.group(1) for x in serial if (m := re.search(rf"(?:^| \| )ZDBDEV\[{index}\] (.*)$", x))]
    exits = [m.group(1) for x in serial if (m := re.search(rf"(?:^| \| )RESULT native zdb device pass={index} exit=(-?\d+)$", x))]
    if exits != ["1" if index == 1 else "0"]:
        raise SystemExit("FAIL device exit status")
    if index == 1:
        if len(report) != 1 or not report[0].startswith("cannot open '/AROS/ata.device/0': "):
            raise SystemExit("FAIL missing permission denial")
        if "Permission denied" not in report[0]:
            raise SystemExit("FAIL unexpected denial reason")
    elif report != expected:
        raise SystemExit("FAIL native device labels differ from host")
if sum(bool(re.search(r"(?:^| \| )RESULT native zdb device PASS root-denied-root=1$", x)) for x in serial) != 1:
    raise SystemExit("FAIL completion marker")
if any(any(w in x for w in ("RESULT native zdb labels FAIL", "[PF-DBG]", "ASSERT at")) for x in serial):
    raise SystemExit("FAIL native failure marker")
print("PASS native device labels match host before/after unprivileged EACCES denial")
