#!/usr/bin/env python3
"""Private FAT and POSIX fsync objects; never modify installed SDK/runtime."""
import hashlib
import pathlib
import shutil
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
release = pathlib.Path('/opt/aros-workspace/aros-amiwest-nightly-release')
source = pathlib.Path('/opt/aros-workspace/AROS')
baseline = pathlib.Path('/opt/aros-workspace/AROS-amiwest-nightly-src')
sdk = release / 'bin/pc-x86_64-smp/AROS/Developer'
gen = release / 'bin/pc-x86_64-smp/gen/rom/filesys/fat/fat'
cc = '/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc'
out = pathlib.Path(tempfile.mkdtemp(prefix='flush-overlay.', dir=repo / 'build-aros'))
print('Evidence:', out, flush=True)
common = [cc, '--sysroot', str(sdk), '-B' + str(sdk / 'lib') + '/',
          '-m64', '-mcmodel=large', '-mno-red-zone', '-mno-ms-bitfields']
inputs = [gen / '.map']
for name in ['cache', 'packet']:
    src = source / 'rom/filesys/fat' / (name + '.c')
    shutil.copy2(src, out / src.name)
    inputs.append(src)
    subprocess.run(common + ['-O2', '-D__NOLIBBASE__',
        '-I' + str(baseline / 'rom/filesys/fat'), '-I' + str(gen / 'include'),
        '-c', str(out / src.name), '-o', str(out / (name + '.o'))], check=True)
objects = [pathlib.Path(line[5:]) for line in (gen / '.map').read_text().splitlines()
           if line.startswith('LOAD ') and line.endswith('.o')]
assert objects[0].name == 'fat_start.o' and objects[-1].name == 'fat_end.o'
assert all(p.is_relative_to(release) and p.is_file() for p in objects)
inputs.extend(objects)
objects = [out / p.name if p.name in ['cache.o', 'packet.o'] else p for p in objects]
subprocess.run(common + ['-nostartfiles', '-static', '-Wl,-Map,' + str(out / 'fat.map'),
    *map(str, objects), '-Wl,--start-group', '-lfatbpb', '-Wl,--end-group',
    '-o', str(out / 'fat.handler')], check=True)
src = source / 'compiler/crt/posixc/fsync.c'
shutil.copy2(src, out / 'fsync.c')
shutil.copy2(baseline / 'compiler/crt/posixc/__fdesc.h', out / '__fdesc.h')
inputs.extend([src, baseline / 'compiler/crt/posixc/__fdesc.h'])
subprocess.run(common + ['-O2', '-D_GNU_SOURCE', '-c', str(out / 'fsync.c'), '-o', str(out / 'fsync.o')], check=True)
inputs.extend([out / 'fsync.o', out / 'fat.handler'])
posixgen = release / 'bin/pc-x86_64-smp/gen/compiler/crt/posixc/posixc'
loads = [pathlib.Path(line[5:]) for line in (posixgen / '.map').read_text().splitlines()
         if line.startswith('LOAD ')]
objects = [p for p in loads if p.suffix == '.o']
archives = [p for p in loads if p.suffix == '.a']
assert sum(p.name == 'fsync.o' for p in objects) == 1
assert all(p.is_relative_to(release) and p.is_file() for p in objects)
inputs.extend([posixgen / '.map', *objects, *archives])
objects = [out / 'fsync.o' if p.name == 'fsync.o' else p for p in objects]
subprocess.run(common + ['-nostartfiles', '-nostdlib', '-Wl,-Map,' + str(out / 'posixc.map'),
    *map(str, objects), '-Wl,--start-group', *map(str, archives), '-Wl,--end-group',
    '-o', str(out / 'posixc.library')], check=True)
inputs.append(out / 'posixc.library')
test = repo / 'aros/engine/fat-cache-retry-test.c'
inputs.append(test)
subprocess.run(common + ['-O2', '-I' + str(baseline / 'rom/filesys/fat'),
    str(test), str(out / 'cache.o'), '-lamiga', '-o', str(out / 'OpenZFSFATCacheTest')], check=True)
inputs.append(out / 'OpenZFSFATCacheTest')
test = repo / 'aros/engine/fsync-error-test.c'
inputs.append(test)
pthread = repo / 'build-aros/pthread-consistent.s3r1CHla'
inputs.extend([pthread / 'source/pthread.h', pthread / 'libpthread.a'])
subprocess.run(common + ['-O2', '-D_GNU_SOURCE', '-Dfsync=oz_test_fsync',
    '-c', str(out / 'fsync.c'), '-o', str(out / 'fsync-test-body.o')], check=True)
subprocess.run(common + ['-O2', '-D_GNU_SOURCE', '-I' + str(out),
    '-I' + str(pthread / 'source'), str(test), str(out / 'fsync-test-body.o'),
    str(pthread / 'libpthread.a'), '-o', str(out / 'OpenZFSFsyncErrorTest')], check=True)
inputs.append(out / 'OpenZFSFsyncErrorTest')
(out / 'inputs.sha256').write_text(''.join(
    hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + str(p) + '\n' for p in inputs))
print('Overlay:', out, flush=True)
