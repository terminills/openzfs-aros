#!/usr/bin/env python3
"""Capture the named AROS X11 window, never a frontmost-screen rectangle."""
import argparse
import os
import struct
import subprocess
from pathlib import Path
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("--display", default=os.environ.get("DISPLAY", ":10.0"))
args = parser.parse_args()
env = dict(os.environ, DISPLAY=args.display)
ids = subprocess.check_output(["xdotool", "search", "--name", "^AROS$"], env=env, text=True).split()
if len(ids) != 1:
    raise SystemExit(f"Expected exactly one AROS window on {args.display}, got {ids}")
raw = subprocess.check_output(["xwd", "-silent", "-id", ids[0]], env=env)
header = struct.unpack(">25I", raw[:100])
if not (header[1] == 7 and header[2] == 2 and header[3] == 24 and header[11] == 32
        and header[14:17] == (0xff0000, 0xff00, 0xff)):
    raise SystemExit(f"Unsupported XWD format; do not guess pixels: {header}")
start = header[0] + header[19] * 12
image = Image.frombytes("RGB", (header[4], header[5]), raw[start:],
                        "raw", "BGRX" if header[7] == 0 else "XRGB", header[12])
image.save(args.output)
print(f"Captured AROS window {ids[0]} on {args.display}: {args.output}")
