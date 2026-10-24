#!/usr/bin/env python3
"""Verify a private DOS-only overlay against the actual selected boot ISO."""
import io
import lzma
import pathlib
import runpy
import subprocess
import sys
import tempfile


def entries(path, api):
    raw = lzma.decompress(path.read_bytes())
    stream = io.BytesIO(raw)
    if api['readHeader'](stream) != 1:
        raise ValueError('invalid package header')
    result = []
    while stream.tell() < len(raw):
        name, data = api['readFile'](stream)
        name = name.rstrip(b'\0').decode('iso-8859-1')
        if not name or pathlib.Path(name).name != name or name in dict(result):
            raise ValueError('invalid or duplicate package member')
        result.append((name, data))
    return result


def verify(original, candidate, member='dos.library'):
    if member not in ('dos.library', 'ahci.device'):
        raise ValueError('unsupported overlay member')
    if [n for n, _ in original] != [n for n, _ in candidate]:
        raise ValueError('boot module order or membership changed')
    changed = [name for (name, a), (_, b) in zip(original, candidate) if a != b]
    if changed != [member]:
        raise ValueError(f'expected {member}-only replacement, got {changed!r}')


def main():
    iso, candidate, tool = map(pathlib.Path, sys.argv[1:4])
    member = sys.argv[4] if len(sys.argv) == 5 else 'dos.library'
    if len(sys.argv) not in (4, 5) or member not in ('dos.library', 'ahci.device'):
        raise ValueError('expected ISO PACKAGE TOOL [dos.library|ahci.device]')
    boot_path = '/boot/aros-base.pkg.xz' if member == 'dos.library' else '/boot/pc-smp/aros-bsp.pkg.xz'
    api = runpy.run_path(str(tool))
    with tempfile.TemporaryDirectory(prefix='openzfs-dos-package-') as temporary:
        original = pathlib.Path(temporary, 'aros-base.pkg.xz')
        subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(iso),
                        '-extract', boot_path, str(original)],
                       check=True, stdout=subprocess.DEVNULL)
        verify(entries(original, api), entries(candidate, api), member)
    print(f'PASS {member}-only package replacement; all other boot module bytes/order preserved')


if __name__ == '__main__':
    main()
