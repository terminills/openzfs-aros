#!/usr/bin/env python3
"""Remove one named module from a private test package, preserving other records."""
import argparse
import lzma
import pathlib
import struct


def records(raw):
    if len(raw) < 8 or raw[:4] != b'PKG\x01':
        raise ValueError('invalid package magic/version')
    if struct.unpack_from('!I', raw, 4)[0] != len(raw):
        raise ValueError('invalid declared size')
    offset = 8
    seen = set()
    result = []
    while offset < len(raw):
        start = offset
        if len(raw) - offset < 4:
            raise ValueError('truncated name length')
        length = struct.unpack_from('!I', raw, offset)[0]
        offset += 4
        if length < 1 or length + 5 > len(raw) - offset:
            raise ValueError('invalid name length')
        name_bytes = raw[offset:offset + length]
        if raw[offset + length] != 0:
            raise ValueError('unterminated name')
        name = name_bytes.rstrip(b'\0').decode('iso-8859-1')
        if not name or '\0' in name or '/' in name or '\\' in name or name in ('.', '..') or name in seen:
            raise ValueError('invalid or duplicate module name')
        seen.add(name)
        offset += length + 1
        size = struct.unpack_from('!I', raw, offset)[0]
        offset += 4
        if size > len(raw) - offset:
            raise ValueError('truncated module')
        offset += size
        result.append((name, raw[start:offset]))
    return result


def omit(raw, module):
    before = records(raw)
    if sum(name == module for name, _ in before) != 1:
        raise ValueError('expected exactly one selected module')
    expected = [(name, record) for name, record in before if name != module]
    body = b''.join(record for _, record in expected)
    after = b'PKG\x01' + struct.pack('!I', 8 + len(body)) + body
    if records(after) != expected:
        raise ValueError('retained module records changed')
    return after


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('destination', type=pathlib.Path)
    parser.add_argument('module')
    args = parser.parse_args()
    raw = omit(lzma.decompress(args.source.read_bytes()), args.module)
    packed = lzma.compress(raw, check=lzma.CHECK_CRC32)
    if lzma.decompress(packed) != raw:
        raise ValueError('compression verification failed')
    with args.destination.open('xb') as output:
        output.write(packed)
    print(f'Removed {args.module}; retained {len(records(raw))} exact module records')


if __name__ == '__main__':
    main()
