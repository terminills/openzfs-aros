"""Remove only completed retired native-import overlays; retain evidence and hashes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess as sp
import time

root = Path('/mnt/short-term/openzfs-old-hosted-20261026/test-runs')
assert sp.check_output(['findmnt', '-no', 'UUID', '/mnt/short-term'], text=True).strip() == '18df445b-e065-44d2-b4ec-d98c69af396a'
opened = sp.run(['lsof', '+D', str(root)], capture_output=True, text=True)
assert opened.returncode == 1 and not opened.stdout, 'Old archive has open files or ownership cannot be checked'
paths = []
for run in sorted(root.glob('native-import-run.*')):
    marker = run / 'completion.log'
    if run.is_symlink() or not marker.is_file() or marker.read_text().strip() != 'OZ-NATIVE-COMPLETION-v1 PASS line=0':
        continue
    for name in ('disk0.qcow2', 'disk1.qcow2'):
        p = run / name
        if p.is_file() and not p.is_symlink() and time.time() - p.stat().st_mtime > 72*3600:
            paths.append(p)
print('Eligible overlays:', len(paths), flush=True)
saved = 0
for p in paths:
    assert p.parent.parent == root and p.name in ('disk0.qcow2', 'disk1.qcow2')
    before = p.stat()
    info = json.loads(sp.check_output(['qemu-img', 'info', '--output=json', str(p)], text=True))
    assert info['format'] == 'qcow2' and not info.get('dirty-flag', False)
    if info.get('backing-filename-format') != 'raw' or str(root) in info.get('full-backing-filename', ''):
        print('SKIP uncertain backing relationship: '+str(p), flush=True)
        continue
    with p.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    now = p.stat()
    assert (before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) == (now.st_ino, now.st_size, now.st_mtime_ns, now.st_ctime_ns)
    holder = sp.run(['fuser', str(p)], capture_output=True, text=True)
    assert holder.returncode == 1 and not holder.stdout, 'Overlay in use'
    record = dict(path=str(p), sha256=digest, allocated_bytes=before.st_blocks*512,
                  completion='OZ-NATIVE-COMPLETION-v1 PASS line=0', backing=info.get('full-backing-filename'),
                  action='delete disposable overlay; underlying raw disk and all logs/screenshots retained',
                  recoverable=False)
    with (root.parent / 'pruned-successful-overlays-20261028.jsonl').open('a') as out:
        out.write(json.dumps(record)+'\n');out.flush();os.fsync(out.fileno())
    p.unlink()
    saved += before.st_blocks*512
    print(json.dumps({'removed':str(p),'total_reclaimed_bytes':saved}), flush=True)
print('COMPLETE reclaimed_bytes='+str(saved), flush=True)
