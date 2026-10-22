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


def verify(original, candidate):
    if [n for n, _ in original] != [n for n, _ in candidate]:
        raise ValueError('boot module order or membership changed')
    changed = [name for (name, a), (_, b) in zip(original, candidate) if a != b]
    if changed != ['dos.library']:
        raise ValueError(f'expected DOS-only replacement, got {changed!r}')


def main():
    iso, candidate, tool = map(pathlib.Path, sys.argv[1:])
    api = runpy.run_path(str(tool))
    with tempfile.TemporaryDirectory(prefix='openzfs-dos-package-') as temporary:
        original = pathlib.Path(temporary, 'aros-base.pkg.xz')
        subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(iso),
                        '-extract', '/boot/aros-base.pkg.xz', str(original)],
                       check=True, stdout=subprocess.DEVNULL)
        verify(entries(original, api), entries(candidate, api))
    print('PASS DOS-only package replacement; all other boot module bytes/order preserved')


if __name__ == '__main__':
    main()
