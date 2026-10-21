#!/usr/bin/env python3
"""Build a private security module and compose it with the tested ATA overlay.

Never run make in or write to the source/release trees. Preserve module order
and all other package bytes. Each invocation has its own evidence directory.
"""
import hashlib
import io
import lzma
import pathlib
import runpy
import shlex
import shutil
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
release = pathlib.Path('/opt/aros-workspace/aros-amiwest-nightly-release')
source = pathlib.Path('/opt/aros-workspace/AROS-amiwest-nightly-src')
cc = '/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc'
sdk = release / 'bin/pc-x86_64-smp/AROS/Developer'
gen = release / 'bin/pc-x86_64-smp/gen/rom/security/security'
root = repo / 'build-aros/security-overlay'
root.mkdir(exist_ok=True)
out = pathlib.Path(tempfile.mkdtemp(prefix='run.', dir=root))
print('Evidence:', out, flush=True)
stage = out / 'rom/security'
stage.mkdir(parents=True)
shutil.copy2(source / 'rom/security/secAccess_Control.c', stage)
patch = repo / 'aros/patches/security-access-volume-lifetime.patch'
inputs = [patch, source / 'rom/security/secAccess_Control.c', gen / '.map']

def run(args):
    print(shlex.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, cwd=repo)

run(['patch', '--fuzz=0', '-p1', '-d', out, '-i', patch])
common = [cc, '--sysroot', sdk, '-m64', '-mcmodel=large', '-mno-red-zone',
          '-mno-ms-bitfields']
run(common + ['-O2', '-fno-asynchronous-unwind-tables', '-fno-omit-frame-pointer',
    '-Wno-pointer-sign', '-Wno-parentheses', '-DDEBUG=0',
    '-I' + str(source / 'rom/security'), '-I' + str(gen), '-I' + str(gen / 'include'),
    '-include', gen / 'include/security_deflibdefs.h', '-c',
    stage / 'secAccess_Control.c', '-o', out / 'secAccess_Control.o'])

# Recover the ordered object inputs, not guessed wildcard contents. Archive
# grouping matches this module's generated rule: static module, libamiga,
# followed by the compiler's default static AROS runtime libraries.
objects = [pathlib.Path(line[5:]) for line in (gen / '.map').read_text().splitlines()
           if line.startswith('LOAD ') and line.endswith('.o')]
old = gen / 'secAccess_Control.o'
assert objects.count(old) == 1 and objects[0].name == 'libentry.o'
assert objects[-1].name == 'security_end.o'
assert all(p.is_file() and p.is_relative_to(release) for p in objects)
inputs.extend(objects)
objects = [out / 'secAccess_Control.o' if p == old else p for p in objects]
run(common + ['-nostartfiles', '-Wl,-Map', '-Xlinker', out / 'security.map',
    *objects, '-o', out / 'security.library', '-static',
    '-Wl,--start-group', '-lamiga', '-Wl,--end-group'])

api = runpy.run_path(str(source / 'tools/package/pkg'))
original = repo / 'build-aros/ata-overlay/aros-bsp.pkg.xz'
inputs.append(original)

def unpack(path):
    raw = lzma.decompress(path.read_bytes())
    stream = io.BytesIO(raw)
    assert api['readHeader'](stream) == 1
    entries = []
    while stream.tell() < len(raw):
        name, data = api['readFile'](stream)
        name = name.rstrip(b'\0').decode('iso-8859-1')
        assert name and pathlib.Path(name).name == name
        assert name not in [n for n, _ in entries]
        entries.append((name, data))
    return entries

entries = unpack(original)
assert sum(n == 'security.library' for n, _ in entries) == 1
package = out / 'package'
package.mkdir()
for name, data in entries:
    (package / name).write_bytes((out / name).read_bytes() if name == 'security.library' else data)
run(['python3', source / 'tools/package/pkg', 'c', out / 'aros-bsp.pkg',
     package, *[n for n, _ in entries]])
with (out / 'aros-bsp.pkg.xz').open('wb') as stream:
    subprocess.run(['xz', '--check=crc32', '--x86', '-9e', '-c', str(out / 'aros-bsp.pkg')],
                   stdout=stream, check=True)
updated = unpack(out / 'aros-bsp.pkg.xz')
assert [n for n, _ in entries] == [n for n, _ in updated]
changed = [n for (n, a), (_, b) in zip(entries, updated) if a != b]
assert changed == ['security.library'], changed
# Reuse the existing harness's composite BSP + on-disk ATA overlay interface.
shutil.copy2(repo / 'build-aros/ata-overlay/ata.device', out / 'ata.device')
assert (out / 'ata.device').read_bytes() == dict(updated)['ata.device']
inputs += [out / 'security.library', out / 'aros-bsp.pkg.xz', out / 'ata.device']
(out / 'inputs.sha256').write_text(''.join(
    hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + str(p) + '\n' for p in inputs))
print('PASS: only security.library changed among', len(entries), 'BSP entries.', flush=True)
print('Overlay:', out, flush=True)
