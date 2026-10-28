#!/usr/bin/env python3
"""Create synthetic L2ARC decoding fixtures; never use real devices.

Uses upstream libnvpair for XDR labels. These are reader fixtures, not evidence
of cache population, log-block rebuild, or live persistent L2ARC operation.
--populated adds one uncompressed log with two entries in each byte order,
plus a damaged-checksum copy.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import sys


def fletcher4(data):
    # Native checksum used by the x86-64 upstream log reader, before byte swap.
    a = b = c = d = 0
    mask = (1 << 64) - 1
    for (word,) in struct.iter_unpack("<I", data):
        a = (a + word) & mask
        b = (b + a) & mask
        c = (c + b) & mask
        d = (d + c) & mask
    return a, b, c, d


def log_fixture(out, endian, corrupt, header_offset, length):
    """One uncompressed log with two known entries; not live ARC contents."""
    size = 65536
    address = header_offset + size
    prop = 127 | (127 << 16) | (2 << 32) | (7 << 40)
    block = bytearray(size)
    struct.pack_into(endian + "Q", block, 0, 0x4c4f47424c4b4844)
    for i in range(2):
        entry_prop = 7 | (7 << 16) | (2 << 32) | (1 << 48) | (1 << 57)
        struct.pack_into(endian + "8Q", block, 128 + i * 64,
                         8, 32 + i * 8, 42 + i, entry_prop,
                         header_offset + 512 + i * 4096, 0, 0, 0)
    checksum = fletcher4(block)
    if corrupt:
        block[128] ^= 1
    words = [0] * 64
    words[:9] = [0x5a46534341434845, 1, 0x11223344, 0x22334455, 2,
                 header_offset + 512, 1, header_offset + 512, length - 512*1024]
    words[9:17] = [address, 8192, header_offset + 512, prop, *checksum]
    words[25:27] = [size, 1]
    out.seek(header_offset)
    out.write(struct.pack(endian + "64Q", *words))
    out.seek(address)
    out.write(block)


def prepare(library, destination, populated=False):
    lib = C.CDLL(str(Path(library).resolve(strict=True)))
    lib.nvlist_alloc.argtypes = [C.POINTER(C.c_void_p), C.c_uint, C.c_int]
    lib.nvlist_add_uint64.argtypes = [C.c_void_p, C.c_char_p, C.c_uint64]
    lib.nvlist_add_string.argtypes = [C.c_void_p, C.c_char_p, C.c_char_p]
    lib.nvlist_pack.argtypes = [C.c_void_p, C.POINTER(C.c_void_p), C.POINTER(C.c_size_t), C.c_int, C.c_int]
    lib.nvlist_free.argtypes = [C.c_void_p]
    lib.fnvlist_pack_free.argtypes = [C.c_void_p, C.c_size_t]
    nvl = C.c_void_p()
    assert lib.nvlist_alloc(C.byref(nvl), 1, 0) == 0
    packed = C.c_void_p()
    size = C.c_size_t()
    try:
        for key, value in ((b"state", 4), (b"guid", 0x22334455),
                           (b"pool_guid", 0x11223344), (b"version", 5000), (b"txg", 1)):
            assert lib.nvlist_add_uint64(nvl, key, value) == 0
        assert lib.nvlist_add_string(nvl, b"name", b"l2arc_fixture") == 0
        assert lib.nvlist_pack(nvl, C.byref(packed), C.byref(size), 1, 0) == 0
        config = C.string_at(packed, size.value)
    finally:
        if packed.value:
            lib.fnvlist_pack_free(packed, size.value)
        lib.nvlist_free(nvl)
    destination = Path(destination)
    destination.mkdir(exist_ok=False)
    length = 64 * 1024 * 1024
    label_size = 256 * 1024
    header_offset = 4 * 1024 * 1024  # Two labels plus the 3.5 MiB boot area.
    offsets = (0, label_size, length - 2 * label_size, length - label_size)
    fixtures = [("little", "<", 0x5a46534341434845),
                                ("big", ">", 0x5a46534341434845),
                                ("missing", "<", 0)]
    if populated:
        fixtures += [(name, endian, 0x5a46534341434845) for name, endian in
                     (("log-little", "<"), ("log-big", ">"), ("log-bad", "<"))]
    for name, endian, magic in fixtures:
        with (destination / name).open("xb") as out:
            out.truncate(length)
            for offset in offsets:
                block = bytearray(112 * 1024)
                block[:len(config)] = config
                struct.pack_into("<5Q", block, len(block)-40,
                                 0x210da7ab10c7a11, offset + 16*1024, 0, 0, 0)
                checksum = struct.unpack(">4Q", hashlib.sha256(block).digest())
                struct.pack_into("<4Q", block, len(block)-32, *checksum)
                out.seek(offset + 16*1024)
                out.write(block)
            words = [0] * 64
            words[:9] = [magic, 1, 0x11223344, 0x22334455, 0,
                         header_offset + 512, 0, header_offset + 512, length-512*1024]
            out.seek(header_offset)
            out.write(struct.pack(endian + "64Q", *words))
            if name.startswith("log-"):
                log_fixture(out, endian, name == "log-bad", header_offset, length)
    (destination / "fixture.json").write_text(json.dumps({
        "kind": "synthetic-log-l2arc-reader" if populated else "synthetic-empty-l2arc-reader", "size": length,
        "header_offset": header_offset, "pool_guid": 0x11223344,
        "scope": "synthetic log decoding and checksum rejection; no live cache" if populated else
                 "header decoding only; no cache log records or live cache"}, indent=2) + "\n")


if __name__ == "__main__":
    prepare(sys.argv[1], sys.argv[2], "--populated" in sys.argv[3:])
