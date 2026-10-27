#!/usr/bin/env python3
"""Require exact host/native label output and successful post-error reloads."""
import re
import sys
from pathlib import Path


def verify(run):
    run = Path(run)
    serial = (run / "serial.log").read_text().splitlines()
    if any(any(word in line for word in
               ("RESULT native zdb labels FAIL", "[PF-DBG]", "ASSERT at",
                "went out of stack limits")) for line in serial):
        raise ValueError("native failure marker")
    for index, name, status in ((0, "one-bad", 1), (1, "labels", 0),
                                (2, "all-bad", 2), (3, "labels", 0)):
        expected = (run / f"host-{name}.log").read_text().splitlines()
        if not expected:
            raise ValueError("empty host report")
        if name != "labels" and (run / f"host-{name}.status").read_text().strip() != str(status):
            raise ValueError("unexpected host status")
        if name == "one-bad" and not any("failed to unpack label 0" in x for x in expected):
            raise ValueError("missing damaged-label diagnostic")
        if name == "all-bad" and expected != [f"failed to unpack label {i}" for i in range(4)]:
            raise ValueError("missing four-label rejection")
        reports = [m.group(1) for line in serial
                   if (m := re.search(rf"(?:^| \| )ZDBBAD\[{index}\] (.*)$", line))]
        exits = [m.group(1) for line in serial
                 if (m := re.search(rf"(?:^| \| )RESULT native zdb damaged pass={index} exit=(-?\d+)$", line))]
        if reports != expected or exits != [str(status)]:
            raise ValueError(f"pass {index}: report/status mismatch")
    for marker in ("RESULT native zdb damaged PASS cases=4",
                   "RESULT native zdb labels PASS runs=2 storage=readonly-iso"):
        if sum(bool(re.search(r"(?:^| \| )" + re.escape(marker) + "$", line)) for line in serial) != 1:
            raise ValueError("missing/duplicate completion")


if __name__ == "__main__":
    verify(sys.argv[1])
    print("PASS native damaged labels match host; status=1,0,2,0; healthy reload after each damaged input")
