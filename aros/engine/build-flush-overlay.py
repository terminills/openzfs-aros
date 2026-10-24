#!/usr/bin/env python3
"""Private FAT and POSIX fsync objects; never modify installed SDK/runtime."""
import hashlib
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
release = pathlib.Path(os.environ['AROS_RELEASE_BUILD']).resolve(strict=True)
source = pathlib.Path(os.environ['AROS_SOURCE']).resolve(strict=True)
baseline = pathlib.Path(os.environ['AROS_BASELINE_SOURCE']).resolve(strict=True)
sdk = release / 'bin/pc-x86_64-smp/AROS/Developer'
gen = release / 'bin/pc-x86_64-smp/gen/rom/filesys/fat/fat'
cc = os.environ['AROS_CC']
fat_only = os.environ.get('AROS_FAT_ONLY', '0') == '1'
readonly = os.environ.get('AROS_FAT_READONLY_FIX', '0') == '1'
full_fat = os.environ.get('AROS_FAT_FULL_REBUILD', '0') == '1'
clean_flush = os.environ.get('AROS_FAT_CLEAN_FLUSH', '0') == '1'
bpb_bound = os.environ.get('AROS_FAT_BPB_BOUND', '0') == '1'
if bpb_bound and not full_fat:
    raise RuntimeError('BPB volume boundary requires full FAT rebuild')
if clean_flush and not full_fat:
    raise RuntimeError('clean-bit integration requires full FAT rebuild')
if full_fat and not fat_only:
    raise RuntimeError('full FAT rebuild requires AROS_FAT_ONLY=1')
artifact_root = pathlib.Path(os.environ.get('AROS_ARTIFACT_ROOT', repo / 'build-aros')).resolve(strict=True)
if 'AROS_ARTIFACT_ROOT' in os.environ and artifact_root.stat().st_dev == repo.stat().st_dev:
    raise RuntimeError('Explicit artifact root must be on a separate mounted filesystem')
if shutil.disk_usage(artifact_root).free < 100 * 2**30:
    raise RuntimeError('100 GiB artifact filesystem reserve required')
out = pathlib.Path(tempfile.mkdtemp(prefix='flush-overlay.', dir=artifact_root))
print('Evidence:', out, flush=True)
shutil.copy2(__file__, out / 'build-flush-overlay.py')
# Compile against a retained header snapshot, not a mutable shared tree.
header_source = source if full_fat else baseline
fat_headers = out / 'include-fat'
generated_headers = out / 'include-generated'
fat_headers.mkdir()
generated_headers.mkdir()
snapshot_inputs = []
for origin, target in ((header_source / 'rom/filesys/fat', fat_headers), (gen / 'include', generated_headers)):
    for path in sorted(origin.glob('*.h')):
        shutil.copy2(path, target / path.name)
        snapshot_inputs.append(path)
licenses = out / 'Licenses'
licenses.mkdir()
for name in ('LICENSE', 'LICENSE.Author', 'LICENSE.GPL', 'LICENSE.LGPL', 'ACKNOWLEDGEMENTS'):
    path = source / name
    if path.is_file():
        shutil.copy2(path, licenses / name)
        snapshot_inputs.append(path)
if not (licenses / 'LICENSE').is_file():
    raise RuntimeError('AROS source license missing')
provenance = {'source': str(source), 'baseline': str(baseline), 'sdk': str(sdk),
              'compiler': cc, 'full_fat': full_fat,
              'scope': 'FAT sources and local headers; SDK and linked archives remain explicit external dependencies'}
for label, tree in (('source', source), ('baseline', baseline)):
    provenance[label + '_commit'] = subprocess.check_output(
        ['git', '-C', str(tree), 'rev-parse', 'HEAD'], text=True).strip()
    (out / (label + '.patch')).write_bytes(subprocess.check_output(
        ['git', '-C', str(tree), 'diff', 'HEAD', '--', 'rom/filesys/fat', 'compiler/fatbpb']))
(out / 'source-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
common = [cc, '--sysroot', str(sdk), '-B' + str(sdk / 'lib') + '/',
          '-m64', '-mcmodel=large', '-mno-red-zone', '-mno-ms-bitfields']
commands = []
def run_build(command):
    if '-c' in command:
        obj = pathlib.Path(command[command.index('-o') + 1])
        command += ['-MD', '-MF', str(obj.with_suffix('.d'))]
    commands.append(command)
    subprocess.run(command, check=True)

inputs = [gen / '.map', *snapshot_inputs, pathlib.Path(__file__)]
if bpb_bound:
    patch = repo / 'aros/platform/fat-bpb-volume-bound.patch'
    bpb = source / 'compiler/fatbpb/bpb.c'
    volume = source / 'rom/filesys/fat/volume.c'
    inputs.extend([patch, bpb, volume])
    for path in (patch, bpb, volume):
        shutil.copy2(path, out / path.name)
    subprocess.run(['patch', '--batch', '--fuzz=0', '-d', str(out), '-p0',
                    '-i', str(out / patch.name)], check=True)
    run_build(common + ['-O2', '-c', str(out / 'bpb.c'),
                    '-o', str(out / 'bpb.o')])
for name in ['cache', 'packet']:
    src = source / 'rom/filesys/fat' / (name + '.c')
    shutil.copy2(src, out / src.name)
    inputs.append(src)
    if name == 'packet' and readonly:
        patch = repo / 'aros/platform/fat-device-readonly.patch'
        shutil.copy2(patch, out / patch.name)
        inputs.append(patch)
        subprocess.run(['patch', '--batch', '--fuzz=0', '-d', str(out), '-p0',
                        '-i', str(out / patch.name)], check=True)
    if name == 'packet' and clean_flush:
        patch = repo / 'aros/platform/fat-flush-clean.patch'
        shutil.copy2(patch, out / patch.name)
        inputs.append(patch)
        subprocess.run(['patch', '--batch', '--fuzz=0', '-d', str(out), '-p0',
                        '-i', str(out / patch.name)], check=True)
    run_build(common + ['-O2', '-D__NOLIBBASE__',
        '-I' + str(fat_headers), '-I' + str(generated_headers),
        '-c', str(out / src.name), '-o', str(out / (name + '.o'))])
objects = [pathlib.Path(line[5:]) for line in (gen / '.map').read_text().splitlines()
           if line.startswith('LOAD ') and line.endswith('.o')]
assert objects[0].name == 'fat_start.o' and objects[-1].name == 'fat_end.o'
assert all(p.is_relative_to(release) and p.is_file() for p in objects)
inputs.extend(objects)
objects = [out / p.name if p.name in ['cache.o', 'packet.o'] else p for p in objects]
if full_fat:
    # A two-object overlay is not sufficient across FAT source revisions (for
    # example validate.c adds MarkVolumeDirty). Recompile every source in the
    # current module, plus the configured generated startup, against one SDK.
    names = ['cache', 'direntry', 'date', 'disk', 'fat', 'file', 'lock', 'main',
             'names', 'notify', 'ops', 'packet', 'support', 'timer', 'validate', 'volume']
    objects = []
    for name in ['fat_start', *names, 'fat_end']:
        src = gen / (name + '.c') if name in ('fat_start', 'fat_end') else source / 'rom/filesys/fat' / (name + '.c')
        if name not in ('cache', 'packet') and not (bpb_bound and name == 'volume'):
            shutil.copy2(src, out / src.name)
            inputs.append(src)
        obj = out / (name + '.o')
        run_build(common + ['-O2', '-D__NOLIBBASE__',
            '-I' + str(fat_headers), '-I' + str(generated_headers),
            '-c', str(out / src.name), '-o', str(obj)])
        objects.append(obj)
if bpb_bound:
    objects.insert(1, out / 'bpb.o')
run_build(common + ['-nostartfiles', '-static', '-Wl,-Map,' + str(out / 'fat.map'),
    *map(str, objects), '-Wl,--start-group', '-lfatbpb', '-Wl,--end-group',
    '-o', str(out / 'fat.handler')])
if fat_only:
    inputs.extend(sorted(fat_headers.glob('*.h')))
    inputs.extend(sorted(generated_headers.glob('*.h')))
    inputs.extend([sdk / 'lib/libfatbpb.a', out / 'fat.handler'])
    (out / 'inputs.sha256').write_text(''.join(
        hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + str(p) + '\n' for p in inputs))
    (out / 'build-options.txt').write_text(f'fat_only=1\nreadonly_fix={int(readonly)}\nfull_fat={int(full_fat)}\nclean_flush={int(clean_flush)}\nbpb_bound={int(bpb_bound)}\n')
    if full_fat:
        external = {pathlib.Path(cc).resolve(strict=True)}
        for dep in out.glob('*.d'):
            text = dep.read_text().replace('\\\n', '')
            for name in shlex.split(text.split(':', 1)[1]):
                path = pathlib.Path(name).resolve(strict=True)
                if not path.is_relative_to(out):
                    external.add(path)
        for line in (out / 'fat.map').read_text().splitlines():
            if line.startswith('LOAD '):
                path = pathlib.Path(line[5:]).resolve(strict=True)
                if not path.is_relative_to(out):
                    external.add(path)
        record = {'format': 1, 'snapshot': str(out), 'compiler': cc,
                  'commands': commands,
                  'external_inputs': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(external)},
                  'expected_binary': hashlib.sha256((out / 'fat.handler').read_bytes()).hexdigest()}
        (out / 'rebuild.json').write_text(json.dumps(record, indent=2) + '\n')
        shutil.copy2(repo / 'aros/engine/rebuild-fat-snapshot.py', out / 'rebuild-fat-snapshot.py')
    retained = sorted(p for p in out.rglob('*') if p.is_file() and p.suffix not in ('.o', '.map'))
    (out / 'retained.sha256').write_text(''.join(
        hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + str(p.relative_to(out)) + '\n' for p in retained))
    print('Overlay:', out, flush=True)
    raise SystemExit(0)
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
pthread = pathlib.Path(os.environ['AROS_PRIVATE_PTHREAD'])
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
