#!/usr/bin/env python3
"""Verify native root access, unprivileged denial and root reopening."""
import re
import sys
from pathlib import Path

run = Path(sys.argv[1])
serial = (run / "serial.log").read_text().splitlines()
expected = (run / "host-labels.log").read_text().splitlines()
partitioned = "native_partitions=1" in (run / "mode.txt").read_text().splitlines()
unconfigured = "unconfigured_security=1" in (run / "mode.txt").read_text().splitlines()
missing = "missing_security=1" in (run / "mode.txt").read_text().splitlines()
if missing and unconfigured:
    raise SystemExit("FAIL contradictory security modes")
denial_only = unconfigured or missing
native_pool = "native_pool=1" in (run / "mode.txt").read_text().splitlines()
if missing and native_pool:
    raise SystemExit("FAIL unsupported missing-security pool mode")
denied_path = "/AROS/ata.device/0" + ("/partition/0" if partitioned else "")
if not expected or not any("name: 'topo_source'" in x for x in expected):
    raise SystemExit("FAIL missing host labels")
identity = "security-missing=1" if missing else "security-unconfigured=1" if unconfigured else "uid=77 gid=88"
if sum(bool(re.search(r"(?:^| \| )RESULT native zdb actual caller " + identity + "$", x)) for x in serial) != 1:
    raise SystemExit("FAIL unverified debugger caller identity")
identities = [x for x in serial if re.search(r"(?:^| \| )RESULT native zdb actual caller (?:uid=|security-)", x)]
if len(identities) != 1:
    raise SystemExit("FAIL ambiguous debugger caller identity")
passes = [int(m.group(1)) for x in serial if
          (m := re.search(r"(?:^| \| )RESULT native zdb device pass=(\d+) exit=", x))]
if passes != list(range(1 if denial_only else 3)):
    raise SystemExit("FAIL unexpected device pass sequence")
if any(int(m.group(1)) not in passes for x in serial if
       (m := re.search(r"(?:^| \| )ZDBDEV\[(\d+)\] ", x))):
    raise SystemExit("FAIL report from unexpected device pass")
for index in range(1 if denial_only else 3):
    report = [m.group(1) for x in serial if (m := re.search(rf"(?:^| \| )ZDBDEV\[{index}\] (.*)$", x))]
    exits = [m.group(1) for x in serial if (m := re.search(rf"(?:^| \| )RESULT native zdb device pass={index} exit=(-?\d+)$", x))]
    if exits != ["1" if denial_only or index == 1 else "0"]:
        raise SystemExit("FAIL device exit status")
    if denial_only or index == 1:
        prefix = "cannot admit native devices: " if native_pool else f"cannot open '{denied_path}': "
        if len(report) != 1 or not report[0].startswith(prefix):
            raise SystemExit("FAIL missing permission denial")
        if "Permission denied" not in report[0]:
            raise SystemExit("FAIL unexpected denial reason")
    elif report != expected:
        raise SystemExit("FAIL native device labels differ from host")
completion = "missing-security=1" if missing else "no-security=1" if unconfigured else "root-denied-root=1"
if sum(bool(re.search(r"(?:^| \| )RESULT native zdb device PASS " + completion + "$", x)) for x in serial) != 1:
    raise SystemExit("FAIL completion marker")
if any(any(w in x for w in ("RESULT native zdb labels FAIL", "[PF-DBG]", "ASSERT at")) for x in serial):
    raise SystemExit("FAIL native failure marker")
print("PASS native EACCES with missing security library" if missing else
      "PASS native EACCES with unconfigured security" if unconfigured else
      "PASS native pool admission EACCES and member-zero label reopening" if native_pool else
      "PASS native " + ("partition slots 0/2" if partitioned else "whole device") +
      " labels match host before/after unprivileged EACCES denial")
