#!/usr/bin/env python3
"""Build a private AHCI capacity correction without modifying AROS trees."""
import hashlib
import io
import lzma
import os
import pathlib
import runpy
import shlex
import shutil
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
release = pathlib.Path('/opt/aros-workspace/aros-amiwest-nightly-release')
source = pathlib.Path('/opt/aros-workspace/AROS-amiwest-nightly-src')
toolchain = pathlib.Path('/opt/aros-workspace/aros-crosstools')
sdk = release / 'bin/pc-x86_64-smp/AROS/Developer'
gen = release / 'bin/pc-x86_64-smp/gen/rom/devs/ahci/ahci'
base = repo / 'build-aros/ata-capacity.vtSNuh2f'
out = pathlib.Path(tempfile.mkdtemp(prefix='ahci-capacity.', dir=repo / 'build-aros'))
print('Evidence:', out, flush=True)
stage = out / 'rom/devs/ahci'
stage.mkdir(parents=True)
units = ('ahci_io', 'ahci_scsi', 'ahci')
patches = [repo / 'aros/patches' / name for name in
           ('ahci-exact-capacity.patch', 'ahci-scsi-capacity.patch',
            'ahci-query-lifetime.patch', 'ahci-update-flush.patch',
            'ahci-inquiry.patch')]
if os.environ.get('AROS_AHCI_TIMEOUT_DIAGNOSTIC') == '1':
    patches.append(repo / 'aros/patches/ahci-timeout-diagnostic.patch')
if os.environ.get('AROS_AHCI_COMPLETION_ACK') == '1':
    patches.append(repo / 'aros/patches/ahci-completion-ack.patch')
for unit in units:
    shutil.copy2(source / 'rom/devs/ahci' / (unit + '.c'), stage)
inputs = [pathlib.Path(__file__).resolve(), *patches,
          gen / '.map', base / 'aros-bsp.pkg.xz']
# Private device structures depend on release feature macros. Retain the
# configuration and original dependency closure, not just the edited C file.
inputs += [release / 'bin/pc-x86_64-smp/gen/config' / name
           for name in ('target.cfg', 'compiler.cfg')]
for unit in units:
    dependencies = (gen / (unit + '.d')).read_text().replace('\\\n', ' ')
    for name in shlex.split(dependencies.split(':', 1)[1]):
        path = pathlib.Path(name)
        assert path.is_file() and any(path.is_relative_to(root)
            for root in (source, release, toolchain)), str(path)
        inputs.append(path)

def run(args):
    print(shlex.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=repo, check=True)

for patch in patches:
    run(['patch', '--fuzz=0', '-p1', '-d', out, '-i', patch])
common = ['/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc',
          '--sysroot', sdk, '-m64', '-mcmodel=large', '-mno-red-zone', '-mno-ms-bitfields',
          '-mgeneral-regs-only']
for unit in units:
    run(common + ['-O2', '-fno-asynchronous-unwind-tables', '-fno-omit-frame-pointer',
    '-Wno-pointer-sign', '-DDEBUG=0', '-DAROS_USE_LOGRES',
    '-DAROS_BUILD_TYPE=AROS_BUILD_TYPE_PERSONAL', '-D__BSD_VISIBLE', '-D__OOP_NOLIBBASE__',
    '-D__OOP_NOATTRBASES__', '-D__OOP_NOMETHODBASES__',
    '-include', gen / 'include/ahci_deflibdefs.h',
    '-I' + str(source / 'rom/devs/ahci'), '-I' + str(gen / 'include'),
    '-I' + str(gen), '-c', stage / (unit + '.c'), '-o', out / (unit + '.o')])
objects = [pathlib.Path(line[5:]) for line in (gen / '.map').read_text().splitlines()
           if line.startswith('LOAD ') and line.endswith('.o')]
replacements = {gen / (unit + '.o'): out / (unit + '.o') for unit in units}
assert all(objects.count(old) == 1 for old in replacements)
assert objects[0].name == 'libentry.o'
assert objects[-1].name == 'ahci_end.o'
assert all(p.is_file() and p.is_relative_to(release) for p in objects)
inputs.extend(objects)
objects = [replacements.get(p, p) for p in objects]
run(common + ['-nostartfiles', '-Wl,-Map', '-Xlinker', out / 'ahci.map',
    *objects, '-o', out / 'ahci.device', '-static',
    '-Wl,--start-group', '-lhiddstubs', '-Wl,--end-group'])
inputs += [pathlib.Path(line[5:]) for line in (out / 'ahci.map').read_text().splitlines()
           if line.startswith('LOAD ') and line.endswith('.a')]

api = runpy.run_path(str(source / 'tools/package/pkg'))
def unpack(path):
    raw = lzma.decompress(path.read_bytes()); stream = io.BytesIO(raw)
    assert api['readHeader'](stream) == 1
    entries = []
    while stream.tell() < len(raw):
        name, data = api['readFile'](stream)
        name = name.rstrip(b'\0').decode('iso-8859-1')
        assert name and pathlib.Path(name).name == name
        assert name not in [n for n, _ in entries]
        entries.append((name, data))
    return entries

entries = unpack(base / 'aros-bsp.pkg.xz')
assert sum(n == 'ahci.device' for n, _ in entries) == 1
package = out / 'package'; package.mkdir()
for name, data in entries:
    (package / name).write_bytes((out / name).read_bytes() if name == 'ahci.device' else data)
run(['python3', source / 'tools/package/pkg', 'c', out / 'aros-bsp.pkg',
     package, *[n for n, _ in entries]])
with (out / 'aros-bsp.pkg.xz').open('wb') as stream:
    subprocess.run(['xz', '--check=crc32', '--x86', '-9e', '-c', str(out / 'aros-bsp.pkg')],
                   stdout=stream, check=True)
updated = unpack(out / 'aros-bsp.pkg.xz')
assert [n for n, _ in entries] == [n for n, _ in updated]
assert [n for (n, a), (_, b) in zip(entries, updated) if a != b] == ['ahci.device']
for name in ('ata.device', 'security.library'):
    (out / name).write_bytes(dict(updated)[name])
inputs += [out / 'ahci.device', out / 'aros-bsp.pkg.xz']
(out / 'inputs.sha256').write_text(''.join(
    hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + str(p) + '\n' for p in dict.fromkeys(inputs)))
print('PASS: only ahci.device changed; package module order preserved.', flush=True)
print('Overlay:', out, flush=True)
