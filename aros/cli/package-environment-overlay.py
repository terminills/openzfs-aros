#!/usr/bin/env python3
"""Repack the native boot module archive, preserving order and all other bytes."""
import hashlib
import io
import lzma
import os
import pathlib
import runpy
import struct
import subprocess
import sys

source = pathlib.Path(os.environ.get('AROS_PACKAGE_TOOL', '/opt/aros-workspace/AROS/tools/package/pkg'))
api = runpy.run_path(str(source))
base, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
member = sys.argv[3] if len(sys.argv) > 3 else 'dos.library'
stem = sys.argv[4] if len(sys.argv) > 4 else 'aros-base'
assert pathlib.Path(member).name == member and member not in ('', '.', '..')
assert pathlib.Path(stem).name == stem and stem not in ('', '.', '..')

def unpack(path):
    raw = lzma.decompress(path.read_bytes())
    stream = io.BytesIO(raw)
    header = api['readHeader'](stream)
    # ADT's validated reader now returns (version, declared size); older
    # tools returned only the version. Validate the on-disk size either way.
    version = header[0] if isinstance(header, tuple) else header
    if version != 1 or len(raw) < 8 or struct.unpack('!I', raw[4:8])[0] != len(raw):
        raise ValueError('Invalid package version or declared size')
    entries = []
    while stream.tell() < len(raw):
        name, data = api['readFile'](stream)
        name = name.rstrip(b'\0').decode('iso-8859-1')
        assert name and pathlib.Path(name).name == name
        assert name not in [n for n, _ in entries]
        entries.append((name, data))
    return entries

entries = unpack(base)
assert sum(name == member for name, _ in entries) == 1
package = out / 'package'
package.mkdir()
for name, data in entries:
    (package / name).write_bytes((out / name).read_bytes() if name == member else data)
subprocess.run(['python3', str(source), 'c', str(out / (stem + '.pkg')),
    str(package), *[name for name, _ in entries]], check=True)
with (out / (stem + '.pkg.xz')).open('wb') as dest:
    subprocess.run(['xz', '--check=crc32', '--x86', '-9e', '-c', str(out / (stem + '.pkg'))],
        stdout=dest, check=True)
after = unpack(out / (stem + '.pkg.xz'))
assert [n for n, _ in entries] == [n for n, _ in after]
assert [n for (n, a), (_, b) in zip(entries, after) if a != b] == [member]
(out / 'package.sha256').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()
    + '  ' + str(p) + '\n' for p in [source, base, out / member, out / (stem + '.pkg.xz')]))
