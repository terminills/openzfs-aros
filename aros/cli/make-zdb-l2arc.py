#!/usr/bin/env python3
"""Create synthetic, empty L2ARC decoding fixtures; never use real devices.

Uses upstream libnvpair for XDR labels. These are reader fixtures, not evidence
of cache population, log-block rebuild, or live persistent L2ARC operation.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import sys


def prepare(library, destination):
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
    for name, endian, magic in (("little", "<", 0x5a46534341434845),
                                ("big", ">", 0x5a46534341434845),
                                ("missing", "<", 0)):
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
    (destination / "fixture.json").write_text(json.dumps({
        "kind": "synthetic-empty-l2arc-reader", "size": length,
        "header_offset": header_offset, "pool_guid": 0x11223344,
        "scope": "header decoding only; no cache log records or live cache"}, indent=2) + "\n")


if __name__ == "__main__":
    prepare(sys.argv[1], sys.argv[2])
