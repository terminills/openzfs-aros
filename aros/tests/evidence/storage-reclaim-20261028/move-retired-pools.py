#!/usr/bin/env python3
"""Copy and verify one exact quarantined tree, then preserve its path as a link."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

src = Path('/mnt/short-term/openzfs-old-tree-cleanup-20261026.wWc5h2up')
mount = Path('/run/media/terminills/Amiwest')
dst = mount / 'openzfs-retired-archives-20261028' / src.name
assert subprocess.check_output(['findmnt', '-n', '-o', 'UUID', '--target', str(mount)], text=True).strip() == '05930f9c-5f05-476d-81eb-205389db82ad'
assert shutil.disk_usage(mount).free > 150 * 1024**3
assert src.is_dir() and not src.is_symlink()

def check_idle():
    for proc in Path('/proc').glob('[0-9]*'):
        try:
            refs = [proc / 'cwd', *list((proc / 'fd').iterdir())]
            for ref in refs:
                try:
                    target = os.readlink(ref)
                    assert target != str(src) and not target.startswith(str(src) + '/'), target
                except (FileNotFoundError, PermissionError):
                    pass
        except (FileNotFoundError, PermissionError):
            pass

def digest(p):
    h = hashlib.sha256()
    with p.open('rb') as f:
        for chunk in iter(lambda: f.read(8 * 1024**2), b''):
            h.update(chunk)
    return h.hexdigest()

check_idle()
directories = sorted(src.glob('admission-service-run.*'))
assert directories and all(p.is_dir() and not p.is_symlink() for p in directories)
paths = sorted(p for directory in directories for p in [directory, *directory.rglob('*')])
assert all(not p.is_symlink() and (p.is_file() or p.is_dir()) for p in paths)
files = [p for p in paths if p.is_file()]
assert all(p.name.endswith(('.raw', '.raw.gz')) or p.name in ('archive.py', 'run.log', 'manifest.jsonl') for p in files)
identity = {p: (p.stat().st_ino, p.stat().st_size, p.stat().st_mtime_ns) for p in files}
dst.mkdir(exist_ok=True)
# Resume the verified-scope copy; leave the 3-TiB sparse AHCI capacity fixture alone.
for directory in directories:
    subprocess.run(['rsync', '-aS', '--', str(directory), str(dst) + '/'], check=True)
records = []
for index, p in enumerate(files):
    q = dst / p.relative_to(src)
    h = digest(p)
    assert digest(q) == h, str(p)
    st = p.stat()
    assert identity[p] == (st.st_ino, st.st_size, st.st_mtime_ns), str(p)
    records.append(dict(source=str(p), archive=str(q), sha256=h, size=st.st_size, reclaimed=st.st_blocks*512))
    if index % 50 == 0:
        print(f'Verified {index + 1}/{len(files)} files', flush=True)
subprocess.run(['sync', '-f', str(dst)], check=True)
check_idle()
assert sorted(p for directory in directories for p in [directory, *directory.rglob('*')]) == paths
for p in files:
    st = p.stat()
    assert identity[p] == (st.st_ino, st.st_size, st.st_mtime_ns), str(p)
log = Path(__file__).with_name('moved-retired-pools.jsonl')
with log.open('x') as out:
    for record in records:
        out.write(json.dumps(record) + '\n')
    out.flush()
    os.fsync(out.fileno())
# Only verified, enumerated files are removed; unexpected directory contents fail.
for p in files:
    p.unlink()
for p in sorted((p for p in paths if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
    p.rmdir()
for directory in directories:
    directory.symlink_to(dst / directory.name, target_is_directory=True)
print(json.dumps(dict(files=len(files), reclaimed=sum(r['reclaimed'] for r in records), archive=str(dst))), flush=True)
