#!/usr/bin/env python3
"""Emit a diskless-guest startup for public environment API characterization."""

CASES = ("ordinary", "null", "explicit", "empty", "nested", "mutate", "failed")


def startup():
    lines = ["FailAt 21", "Stack 262144", "MakeDir RAM:T RAM:ENV",
             "Assign T: RAM:T", "Assign ENV: RAM:ENV",
             "Mount DEVS:DOSDrivers/SER", 'Echo "fixture" NOLINE >ENV:OZ_SDK_GLOBAL']
    for variant in ("Plain", "Environ"):
        cases = CASES + (("alias", "replace") if variant == "Environ" else ())
        for mode in cases:
            prefix = f"RESULT environment shell variant={variant} mode={mode}"
            lines.extend([f"C:EnvironmentMatrix{variant} {mode} >NIL:", "If WARN",
                          f'Echo "{prefix} exit=FAIL" >SER:', "Else",
                          f'Echo "{prefix} exit=PASS" >SER:', "EndIf"])
    lines.extend(["Delete ENV:OZ_SDK_GLOBAL",
                  'Echo "RESULT environment matrix DONE cases=16" >SER:'])
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    print(startup(), end="")
