#!/usr/bin/env python3
"""Check every native result; known-defect mode is characterization, not a pass."""
import argparse
import re
from pathlib import Path

CASES = ("ordinary", "null", "explicit", "empty", "nested", "mutate", "failed")


def validate(text, known_defects=False):
    def once(pattern):
        if len(re.findall(pattern, text)) != 1:
            raise ValueError(f"missing, duplicate or unexpected result: {pattern}")

    once(r"RESULT environment matrix DONE cases=16\b")
    shell = re.findall(r"RESULT environment shell variant=(\w+) mode=([\w-]+) exit=(PASS|FAIL)\b", text)
    finals = re.findall(r"RESULT OpenZFS environment matrix (PASS|FAIL) mode=([\w-]+) environ=([01])\b", text)
    if len(shell) != 16 or len(finals) != 16:
        raise ValueError("expected exactly 16 shell exits and 16 final test results")
    expected = []
    bad = int(not known_defects)
    for variant, access in (("Plain", 0), ("Environ", 1)):
        for mode in CASES + (("alias", "replace") if access else ()):
            status = "FAIL" if known_defects and mode not in ("ordinary", "null", "failed") else "PASS"
            expected.append((variant, mode, status))
            final_mode = "nested-final" if mode == "nested" else mode
            if finals.count((status, final_mode, str(access))) != 1:
                raise ValueError(f"unexpected native final: {variant}/{mode}/{status}")
            if mode not in ("ordinary", "failed"):
                for scope in (mode, "nested-final") if mode == "nested" else (mode,):
                    global_ok = 1 if scope == "null" else bad
                    once(rf"RESULT OpenZFS environment matrix scope mode={scope} environ={access} value=1 parent=1 global={global_ok}\b")
        once(rf"RESULT OpenZFS environment matrix nested-first={bad} environ={access}\b")
        once(rf"RESULT OpenZFS environment matrix mutations environ={access} local-coherent=1 override=1 unset-hidden={bad}\b")
        once(rf"RESULT OpenZFS environment matrix failed-exec result=-1 errno=[1-9][0-9]* intact=1 environ={access}\b")
    once(rf"RESULT OpenZFS environment matrix replacement observed={bad}\b")
    if sorted(shell) != sorted(expected):
        raise ValueError("shell exit results disagree with expected case matrix")
    if "exec-return" in text or "mode=fixture" in text:
        raise ValueError("execution/setup failure is not an environment result")
    return ("BASELINE CHARACTERIZED: 6 passing cases, 10 reproduced defects; NOT qualified"
            if known_defects else "PASS: all 16 environment cases and detailed invariants")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--expect-known-defects", action="store_true")
    args = parser.parse_args()
    try:
        print(validate(args.log.read_text(), args.expect_known_defects))
    except ValueError as error:
        parser.exit(1, f"FAIL: {error}\n")
