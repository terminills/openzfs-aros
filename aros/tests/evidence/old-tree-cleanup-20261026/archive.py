import hashlib, json, os, pathlib, subprocess, time, re

root = pathlib.Path('/home/terminills/Documents/GitHub/openzfs-aros')
base = root / 'build-aros'
dest = pathlib.Path(__file__).resolve().parent
assert subprocess.check_output(['findmnt','-no','UUID','-T',str(dest)], text=True).strip() == '18df445b-e065-44d2-b4ec-d98c69af396a'
assert dest.stat().st_dev != root.stat().st_dev
assert os.statvfs(dest).f_bavail * os.statvfs(dest).f_frsize > 100 * 1024**3
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(8*1024**2), b''):
            h.update(block)
    return h.hexdigest()
def unused(path):
    result = subprocess.run(['fuser',str(path)], capture_output=True)
    return result.returncode == 1 and not result.stderr.strip()
total = 0
count = 0
with (dest/'manifest.jsonl').open('x') as manifest:
    for run in sorted(base.iterdir()):
        if total >= 128*1024**3:
            break
        if run.is_symlink() or not run.is_dir():
            continue
        logs = sorted(run.glob('*serial.log'))
        passing = [p for p in logs if re.search(r'RESULT [^\r\n]*PASS', p.read_text(errors='replace'))]
        if not passing:
            continue
        evidence = passing[0]
        description = 'Archived historical test media; PASS marker is retention eligibility, not new qualification.'
        for src in sorted(run.glob('*.raw')):
            if src.is_symlink() or not src.is_file():
                continue
            before = src.stat()
            # Sparse capacity fixtures can be terabytes logically but save
            # only kilobytes. Do not spend hours hashing their empty ranges.
            if before.st_size > 8*1024**3 or before.st_blocks*512 < 1024**2:
                continue
            if time.time()-before.st_mtime < 72*3600 or not unused(src):
                continue
            tracked = subprocess.check_output(['git','-C',str(root),'ls-files','--',str(src.relative_to(root))])
            if tracked:
                continue
            target = dest/run.name/src.name
            target.parent.mkdir(exist_ok=True)
            assert not target.exists()
            subprocess.run(['cp','--sparse=always','--preserve=mode,timestamps','--no-clobber',str(src),str(target)],check=True)
            expected = digest(src)
            assert digest(target) == expected
            with target.open('rb') as copied:
                os.fsync(copied.fileno())
            now = src.stat()
            assert (now.st_ino,now.st_size,now.st_mtime_ns,now.st_ctime_ns) == (before.st_ino,before.st_size,before.st_mtime_ns,before.st_ctime_ns)
            assert unused(src)
            record = dict(source=str(src),archive=str(target),sha256=expected,size=before.st_size,reclaimed=before.st_blocks*512,evidence=str(evidence),description=description)
            manifest.write(json.dumps(record)+'\n')
            manifest.flush()
            os.fsync(manifest.fileno())
            link = src.with_name(src.name+'.archive-link')
            assert not link.exists() and not link.is_symlink()
            link.symlink_to(target)
            os.replace(link,src)
            total += before.st_blocks*512
            count += 1
            print(json.dumps(dict(files=count,reclaimed=total,last=str(src))),flush=True)
    print(json.dumps(dict(done=True,files=count,reclaimed=total)),flush=True)
