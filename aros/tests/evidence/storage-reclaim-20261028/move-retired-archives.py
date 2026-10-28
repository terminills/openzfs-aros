#!/usr/bin/env python3
"""Relocate two explicit retired archive directories, retaining path aliases."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

mount = Path('/run/media/terminills/Amiwest')
assert subprocess.check_output(['findmnt', '-n', '-o', 'UUID', '--target', str(mount)], text=True).strip() == '05930f9c-5f05-476d-81eb-205389db82ad'
assert shutil.disk_usage(mount).free > 100 * 1024**3
destroot = mount / 'openzfs-retired-archives-20261028'
destroot.mkdir(exist_ok=True)
log = Path(__file__).with_name('moved-retired-archives.jsonl')

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(4 * 1024**2), b''):
            h.update(block)
    return h.hexdigest()

for name in ('openzfs-old-objects.3tItza0x', 'openzfs-old-services.cDQ9sJs4'):
    src = Path('/mnt/short-term') / name
    assert src.is_dir() and not src.is_symlink()
    files = sorted(src.iterdir())
    assert files and all(p.is_file() and not p.is_symlink() and
                         (p.name.endswith('.gz') or p.name in ('archive.py', 'run.log', 'manifest.jsonl'))
                         for p in files)
    # An open archive or cwd is a reason to stop, never to kill its user.
    for proc in Path('/proc').glob('[0-9]*'):
        try:
            targets = [proc / 'cwd', *list((proc / 'fd').iterdir())]
            for target in targets:
                try:
                    value = os.readlink(target)
                    assert value != str(src) and not value.startswith(str(src) + '/'), value
                except (FileNotFoundError, PermissionError):
                    pass
        except (FileNotFoundError, PermissionError):
            pass
    dst = destroot / name
    dst.mkdir()  # No overwrite or ambiguous retry.
    allocated = 0
    for p in files:
        before = p.stat()
        expected = digest(p)
        q = dst / p.name
        shutil.copy2(p, q)
        with q.open('rb') as copied:
            os.fsync(copied.fileno())
        assert digest(q) == expected and digest(p) == expected
        after = p.stat()
        assert (before.st_ino, before.st_size, before.st_mtime_ns) == (after.st_ino, after.st_size, after.st_mtime_ns)
        record = dict(source=str(p), archive=str(q), sha256=expected, size=before.st_size, reclaimed=before.st_blocks*512)
        with log.open('a') as out:
            out.write(json.dumps(record) + '\n')
            out.flush()
            os.fsync(out.fileno())
        p.unlink()
        allocated += before.st_blocks*512
    src.rmdir()  # Refuses if any unexpected file appeared.
    src.symlink_to(dst, target_is_directory=True)
    print(json.dumps(dict(source=str(src), archive=str(dst), files=len(files), reclaimed=allocated)), flush=True)
