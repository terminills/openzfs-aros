#!/usr/bin/env python3
"""New private pool-image fixture and read-only outside-extent verification."""
import argparse
import hashlib
import struct
import subprocess

SIZE, START, LENGTH = 512 * 1024 * 1024, 4097 * 512, 256 * 1024 * 1024
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("action", choices=("create", "outside", "extract"))
p.add_argument("image")
p.add_argument("output", nargs="?")
p.add_argument("--table", choices=("mbr", "gpt"), default="mbr")
a = p.parse_args()
if a.action == "create":
    mbr = bytearray(512)
    struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\xfe\xff\xff", 0xBF,
                     b"\xfe\xff\xff", START // 512, LENGTH // 512)
    mbr[510:512] = b"\x55\xaa"
    with open(a.image, "xb") as f:
        f.truncate(SIZE)
        if a.table == "mbr":
            f.write(mbr)
        # GPT's final 33 sectors hold its backup table/header. Keep the final
        # sentinel outside those sectors; the outside hash covers them too.
        last_guard = SIZE - (34 if a.table == "gpt" else 1) * 512
        for offset, pattern in ((START-512, 0xA5), (START+LENGTH, 0x5A), (last_guard, 0x3C)):
            f.seek(offset)
            f.write(bytes([pattern]) * 512)
    if a.table == "gpt":
        # Use the real GPT tooling, never an ersatz on-disk table implementation.
        subprocess.run(["sgdisk", "--clear", "--set-alignment=1",
                        f"--new=1:{START//512}:{(START+LENGTH)//512-1}",
                        "--typecode=1:BF01", "--change-name=1:OpenZFS-Pool",
                        a.image], check=True)
        subprocess.run(["sgdisk", "--print", "--verify", a.image], check=True)
else:
    with open(a.image, "rb") as f:
        if f.seek(0, 2) != SIZE:
            p.error("fixture must be exactly 512 MiB")
        if a.action == "outside":
            digest = hashlib.sha256()
            for start, length in ((0, START), (START+LENGTH, SIZE-START-LENGTH)):
                f.seek(start)
                while length:
                    block = f.read(min(length, 1024 * 1024))
                    if not block:
                        raise IOError("short fixture read")
                    digest.update(block)
                    length -= len(block)
            print(digest.hexdigest())
        else:
            if not a.output:
                p.error("extract requires a new output file")
            f.seek(START)
            with open(a.output, "xb") as out:
                remaining = LENGTH
                while remaining:
                    block = f.read(min(remaining, 1024 * 1024))
                    if not block:
                        raise IOError("short partition read")
                    out.write(block)
                    remaining -= len(block)
