import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path('/home/terminills/Documents/GitHub/openzfs-aros/build-aros')
OUT = Path('/mnt/short-term/openzfs-old-binaries.d98mZikg')
NAMES = {'openzfs.library', 'zpool', 'zfs', 'OpenZFSStartupMount', 'OpenZFSStartup', 'OpenZFSDefaultQuotaTest'}

def digest(stream):
    h = hashlib.sha256()
    for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
        h.update(block)
    return h.hexdigest()

def identity(st):
    return st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns

assert subprocess.check_output(['findmnt', '-no', 'UUID', '-T', str(OUT)], text=True).strip() == '18df445b-e065-44d2-b4ec-d98c69af396a'
paths = sorted(p for p in ROOT.rglob('*') if p.name in NAMES and not p.is_symlink() and p.is_file())
tracked = subprocess.check_output(['git', '-C', str(ROOT.parent), 'ls-files', '-z']).split(b'\0')
assert all(os.fsencode(str(p.relative_to(ROOT.parent))) not in tracked for p in paths)
count = freed = 0
with (OUT / 'manifest.jsonl').open('a') as manifest:
    for p in paths:
        if shutil.disk_usage(OUT).free < 100 * 1024**3:
            raise RuntimeError('External free-space floor')
        if subprocess.run(['fuser', '-s', str(p)]).returncode != 1:
            raise RuntimeError(f'Active file or failed check: {p}')
        before = p.stat()
        with p.open('rb') as f:
            sha = digest(f)
        dest = OUT / (sha + '.gz')
        if not dest.exists():
            tmp = OUT / (sha + '.partial')
            with p.open('rb') as src, gzip.open(tmp, 'wb', compresslevel=1) as dst:
                shutil.copyfileobj(src, dst, 4 * 1024 * 1024)
            tmp.rename(dest)
        with gzip.open(dest, 'rb') as f:
            assert digest(f) == sha
        with dest.open('rb') as f:
            os.fsync(f.fileno())
        dfd = os.open(OUT, os.O_DIRECTORY)
        os.fsync(dfd)
        os.close(dfd)
        assert identity(before) == identity(p.stat())
        if subprocess.run(['fuser', '-s', str(p)]).returncode != 1:
            raise RuntimeError(f'File became active: {p}')
        manifest.write(json.dumps({'source':str(p), 'archive':str(dest), 'sha256':sha, 'bytes':before.st_size, 'allocated':before.st_blocks*512, 'mode':before.st_mode, 'mtime_ns':before.st_mtime_ns})+'\n')
        manifest.flush()
        os.fsync(manifest.fileno())
        p.unlink()
        count += 1
        freed += before.st_blocks*512
        if count % 25 == 0:
            print(f'archived={count}/{len(paths)} freed_bytes={freed}', flush=True)
print(f'COMPLETE files={count} freed_bytes={freed}', flush=True)
