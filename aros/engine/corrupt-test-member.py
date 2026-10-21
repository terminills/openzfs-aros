#!/usr/bin/env python3
"""Copy a regular test member, then flip one byte in a verified data block.

Never edits the source or accepts devices. Offset is a physical byte offset,
including the ZFS label area (and any partition prefix). The caller must derive
it from the actual allocated block pointer; this helper does not guess layout.
"""
import argparse
import hashlib
import json
import os
import stat


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("offset", type=lambda value: int(value, 0))
    parser.add_argument("block_sha256")
    args = parser.parse_args()
    with os.fdopen(os.open(args.source, os.O_RDONLY | os.O_NOFOLLOW), "rb") as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError("source must be a regular disposable test image")
        if args.offset % 512 or not 4 * 1024**2 <= args.offset <= before.st_size - 4 * 1024**2 - 4096:
            raise ValueError("unaligned offset or offset inside protected label/boundary area")
        block = os.pread(source.fileno(), 4096, args.offset)
        if len(block) != 4096 or hashlib.sha256(block).hexdigest() != args.block_sha256:
            raise ValueError("data block does not match the independently verified checksum")
        # Exclusive creation also refuses existing symlinks and existing images.
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
        with os.fdopen(os.open(args.destination, flags, 0o600), "wb") as destination:
            original = hashlib.sha256()
            while chunk := source.read(1024 * 1024):
                original.update(chunk)
                destination.write(chunk)
            destination.flush()
            after = os.fstat(source.fileno())
            if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
                raise RuntimeError("source changed during copy; destination is NOT a valid fixture")
            destination.seek(args.offset)
            destination.write(bytes([block[0] ^ 1]))
            destination.flush()
            os.fsync(destination.fileno())
        print(json.dumps({"source": os.path.abspath(args.source),
                          "destination": os.path.abspath(args.destination),
                          "source_sha256": original.hexdigest(),
                          "offset": args.offset, "length": 1,
                          "before": block[0], "after": block[0] ^ 1,
                          "block_sha256": args.block_sha256}, sort_keys=True))


if __name__ == "__main__":
    main()
