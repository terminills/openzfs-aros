#!/usr/bin/env python3
"""Compare both native label reports with the independent host decoder."""
import re
import sys
from pathlib import Path

serial = Path(sys.argv[1]).read_text().splitlines()
expected = Path(sys.argv[2]).read_text().splitlines()
if not expected or not any("name: 'topo_source'" in line for line in expected):
    raise SystemExit("FAIL missing expected host pool")
if not any("labels = 0 1 2 3 " in line for line in expected):
    raise SystemExit("FAIL incomplete host labels")
for line in serial:
    if any(token in line for token in (
        "RESULT native zdb labels FAIL", "[PF-DBG]", "ASSERT at",
        "went out of stack limits",
    )):
        raise SystemExit("FAIL native failure marker")
for run in range(2):
    report = []
    for line in serial:
        match = re.search(r"(?:^| \| )ZDB\[" + str(run) + r"\] (.*)$", line)
        if match:
            report.append(match.group(1))
    if report != expected:
        raise SystemExit(f"FAIL run {run} differs from host labels")
    marker = rf"(?:^| \| )RESULT native zdb labels run={run} exit=0$"
    if sum(bool(re.search(marker, line)) for line in serial) != 1:
        raise SystemExit(f"FAIL run {run} missing/duplicate zero-exit marker")
marker = r"(?:^| \| )RESULT native zdb labels PASS runs=2 storage=readonly-iso$"
if sum(bool(re.search(marker, line)) for line in serial) != 1:
    raise SystemExit("FAIL missing/duplicate final marker")
print(f"PASS native labels match host exactly: runs=2 lines={len(expected)}")
