#!/usr/bin/env python3
"""Archive only obsolete native service copies in named old test runs."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path('/home/terminills/Documents/GitHub/openzfs-aros/build-aros')
RECORD = Path(__file__).resolve().parent
ARCHIVE = RECORD / 'service-executables'
ARCHIVE.mkdir(exist_ok=True)

def digest(stream):
    return hashlib.file_digest(stream, 'sha256').hexdigest()

def active(path):
    result = subprocess.run(['fuser', '-s', str(path)])
    if result.returncode not in (0, 1):
        raise RuntimeError(f'Cannot check holders: {path}')
    return result.returncode == 0

if ROOT.is_symlink() or ROOT.stat().st_dev == RECORD.stat().st_dev:
    raise RuntimeError('Expected literal old root tree and separate archive disk')
if shutil.disk_usage(RECORD).free < 100 * 2**30:
    raise RuntimeError('Archive filesystem reserve below 100 GiB')
total = 0
count = 0
with (RECORD / 'archived-services.jsonl').open('a') as log:
    for run in sorted(ROOT.iterdir()):
        if (run.is_symlink() or not run.is_dir() or not re.fullmatch(
                r'(native-import|admission-service|public-service|native-gui)-run\.[A-Za-z0-9]+', run.name)):
            continue
        path = run / 'OpenZFSServiceNative'
        if path.is_symlink() or not path.is_file() or active(path):
            continue
        # Require retained source/input provenance; never archive a bare binary
        # then erase the only link to its experiment.
        if not (run / 'inputs.sha256').is_file() or not (
                (run / 'source.txt').is_file() or (run / 'source-head.txt').is_file()):
            continue
        before = path.stat()
        if before.st_nlink != 1:
            continue
        with path.open('rb') as src:
            sha = digest(src)
        archive = ARCHIVE / (sha + '.OpenZFSServiceNative.gz')
        if not archive.exists():
            temporary = archive.with_suffix('.partial')
            with path.open('rb') as src, temporary.open('xb') as dst:
                with gzip.GzipFile(filename='', mode='wb', fileobj=dst, mtime=0,
                                   compresslevel=1) as packed:
                    shutil.copyfileobj(src, packed)
                dst.flush()
                os.fsync(dst.fileno())
            temporary.rename(archive)
        with gzip.open(archive, 'rb') as restored:
            if digest(restored) != sha:
                raise RuntimeError(f'Archive round-trip mismatch: {path}')
        after = path.stat()
        if ((before.st_ino, before.st_size, before.st_mtime_ns) !=
                (after.st_ino, after.st_size, after.st_mtime_ns) or active(path)):
            raise RuntimeError(f'Input changed or became active: {path}')
        record = dict(path=str(path), archive=str(archive), sha256=sha,
                      bytes=before.st_size, allocated_bytes=before.st_blocks * 512,
                      mode=before.st_mode & 0o777, action='archive verified; remove loose copy')
        log.write(json.dumps(record) + '\n')
        log.flush()
        os.fsync(log.fileno())
        path.unlink()
        total += before.st_blocks * 512
        count += 1
        print(f'Archived {path} ({count}, {total / 2**30:.2f} GiB removed)', flush=True)
print(f'COMPLETE count={count} allocated_bytes={total}', flush=True)
