#!/usr/bin/env python3
"""Read-only structural digest: size, hole layout and every allocated byte.

Not a logical SHA256: allocating a zero-filled hole deliberately changes this
digest. Unsupported SEEK_DATA/SEEK_HOLE fails rather than skipping data.
Call only with stopped, exclusively owned test images.
"""
import errno
import hashlib
import os
import stat
import struct
import sys


def identity(st):
    return (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns,
            st.st_ctime_ns, st.st_blocks)


def digest(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode):
            raise ValueError("Only regular test files are supported")
        size = before.st_size
        h = hashlib.sha256(b"OpenZFS-sparse-structure-v1\0")
        h.update(struct.pack(">Q", size))
        offset = 0
        while offset < size:
            try:
                start = os.lseek(fd, offset, os.SEEK_DATA)
            except OSError as error:
                if error.errno == errno.ENXIO:
                    break
                raise
            end = min(os.lseek(fd, start, os.SEEK_HOLE), size)
            if not offset <= start < end <= size:
                raise ValueError("Invalid filesystem extent range")
            h.update(struct.pack(">QQ", start, end))
            position = start
            while position < end:
                data = os.pread(fd, min(1024 * 1024, end - position), position)
                if not data:
                    raise ValueError("Unexpected end of allocated extent")
                h.update(data)
                position += len(data)
            offset = end
        if identity(before) != identity(os.fstat(fd)):
            raise ValueError("File changed during digest")
        return h.hexdigest()
    finally:
        os.close(fd)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: sparse-digest.py FILE ...")
    for name in sys.argv[1:]:
        print(f"{digest(name)}  {name}", flush=True)
