#!/usr/bin/env python3
"""Require exact host/native synthetic cache decoding, not live-cache proof."""
import re
import sys
from pathlib import Path


def verify(run):
    run = Path(run)
    serial = (run / "serial.log").read_text().splitlines()
    native = "l2arc_header_device=1" in (run / "mode.txt").read_text().splitlines()
    logs = "l2arc_log_iso=1" in (run / "mode.txt").read_text().splitlines()
    bounds = "l2arc_bounds_iso=1" in (run / "mode.txt").read_text().splitlines()
    hosted = "hosted_runtime=1" in (run / "mode.txt").read_text().splitlines()
    storage = "private-hosted" if hosted else "readonly-iso"
    if hosted and native:
        raise ValueError("hosted evidence cannot claim the native ATA fixture")
    names = (("little", "big", "missing") +
             (("log-little", "log-big", "log-bad", "log-little") if logs else ()) +
             (("count-little", "count-big", "little") if bounds else ()) +
             (("little",) if native else ()))
    passes = [int(m.group(1)) for line in serial if (m := re.search(
        r"(?:^| \| )RESULT native zdb l2arc pass=(\d+) exit=", line))]
    if passes != list(range(len(names))):
        raise ValueError("unexpected L2ARC pass sequence")
    if any(int(m.group(1)) >= len(names) for line in serial if
           (m := re.search(r"(?:^| \| )ZDBL2\[(\d+)\] ", line))):
        raise ValueError("unexpected L2ARC report index")
    for index, name in enumerate(names):
        expected = (run / f"host-l2arc-{name}.log").read_text().splitlines()
        required = "L2ARC device header not found" if name == "missing" else "No log blocks to read"
        if name.startswith("log-"):
            required = "Invalid cksum" if name == "log-bad" else "log_blk_count:\t 1 with valid cksum"
        if name.startswith("count-"):
            required = "Invalid L2ARC log entry count: " + ("1023" if name == "count-little" else "18446744073709551615")
        if required not in expected or "    state: 4" not in expected:
            raise ValueError("host did not exercise cache header path")
        actual = [m.group(1) for line in serial
                  if (m := re.search(rf"(?:^| \| )ZDBL2\[{index}\] (.*)$", line))]
        if actual != expected:
            raise ValueError(f"host/native {name} mismatch")
        exits = [m.group(1) for line in serial if (m := re.search(
            rf"(?:^| \| )RESULT native zdb l2arc pass={index} exit=(-?\d+)$", line))]
        if exits != ["1" if name == "log-bad" or name.startswith("count-") else "0"]:
            raise ValueError("missing or failed native invocation")
    if sum(line.endswith("RESULT native zdb l2arc PASS headers=3 storage=" + storage) for line in serial) != 1:
        raise ValueError("missing completion")
    if logs and sum(line.endswith("RESULT native zdb l2arc PASS logs=4 endian=2 corrupt=1 recovery=1") for line in serial) != 1:
        raise ValueError("missing populated-log completion")
    if bounds and sum(line.endswith("RESULT native zdb l2arc PASS bounds=2 recovery=1") for line in serial) != 1:
        raise ValueError("missing entry-bound completion")
    if any(any(x in line for x in ("[PF-DBG]", "ASSERT at", "RESULT native zdb labels FAIL")) for line in serial):
        raise ValueError("native failure")
    if native and (run / "l2arc-device-unchanged.log").read_text().strip() != "Images are identical.":
        raise ValueError("native cache fixture changed")
    print("PASS AROS L2ARC header decoding matches host; ISO endian/missing cases" +
          (", synthetic log endian/corruption/recovery cases" if logs else "") +
          (", malformed entry counts and recovery" if bounds else "") +
          (" and unchanged native ATA device" if native else "; no device test"))


if __name__ == "__main__":
    verify(sys.argv[1])
