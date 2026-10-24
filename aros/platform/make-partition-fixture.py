#!/usr/bin/env python3
"""Create a NEW disposable MBR image, including deliberately invalid layouts.

No existing file/device can be opened: exclusive creation is mandatory.
These images are test inputs, never a partitioning utility.
"""
import argparse
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("mode", choices=("mbr", "mbr-three", "overlap", "out-of-bounds"))
parser.add_argument("output")
args = parser.parse_args()
entries = [(4097, 131072), (139265, 131072)]
slots = (0, 2)
if args.mode == "mbr-three":
    entries.append((274433, 131072))
    slots = (0, 1, 2)
if args.mode == "overlap":
    entries[1] = (135168, 131072)  # One shared sector is already invalid.
elif args.mode == "out-of-bounds":
    entries[1] = (524287, 2)  # Exactly one sector beyond the image.
mbr = bytearray(512)
for slot, (start, count) in zip(slots, entries):
    # Noncontiguous slots also test native PT_POSITION preservation.
    struct.pack_into("<B3sB3sII", mbr, 446 + slot * 16,
                     0, b"\xfe\xff\xff", 0xBF, b"\xfe\xff\xff", start, count)
mbr[510:512] = b"\x55\xaa"
with open(args.output, "xb") as image:
    image.truncate(256 * 1024 * 1024)
    image.write(mbr)
