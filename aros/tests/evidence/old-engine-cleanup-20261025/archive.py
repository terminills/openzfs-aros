import gzip, hashlib, json, os, pathlib, shutil, subprocess

root = pathlib.Path('/home/terminills/Documents/GitHub/openzfs-aros/build-aros')
out = pathlib.Path('/mnt/short-term/openzfs-old-objects.3tItza0x')
def digest(stream):
    h = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b''):
        h.update(block)
    return h.hexdigest()

count = freed = 0
with (out / 'manifest.jsonl').open('a') as manifest:
    for path in sorted(root.rglob('full-engine.o')):
        if path.is_symlink() or not path.is_file():
            continue
        if shutil.disk_usage(out).free < 100 * 1024**3:
            raise RuntimeError('External free-space floor reached')
        if subprocess.run(['fuser', '-s', str(path)]).returncode != 1:
            raise RuntimeError(f'Open file or failed usage check: {path}')
        before = path.stat()
        with path.open('rb') as stream:
            sha = digest(stream)
        archive = out / (sha + '.o.gz')
        if not archive.exists():
            tmp = archive.with_suffix('.tmp')
            with path.open('rb') as src, gzip.open(tmp, 'wb', compresslevel=1) as dst:
                shutil.copyfileobj(src, dst, 1024 * 1024)
            tmp.rename(archive)
        with gzip.open(archive, 'rb') as stream:
            assert digest(stream) == sha, str(path)
        with archive.open('rb') as stream:
            os.fsync(stream.fileno())
        after = path.stat()
        assert (before.st_ino, before.st_size, before.st_mtime_ns) == (after.st_ino, after.st_size, after.st_mtime_ns)
        if subprocess.run(['fuser', '-s', str(path)]).returncode != 1:
            raise RuntimeError(f'File became active: {path}')
        manifest.write(json.dumps({'source': str(path), 'archive': str(archive), 'sha256': sha, 'bytes': before.st_size, 'allocated': before.st_blocks * 512}) + '\n')
        manifest.flush()
        os.fsync(manifest.fileno())
        path.unlink()
        count += 1
        freed += before.st_blocks * 512
        print(f'archived={count} freed_bytes={freed} source={path}', flush=True)
print(f'COMPLETE files={count} freed_bytes={freed}', flush=True)
