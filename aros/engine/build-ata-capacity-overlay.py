#!/usr/bin/env python3
"""Build a private capacity-only ATA overlay; never edit the AROS checkout.

Uses the selected build's other objects, so this is not a clean full-driver
source rebuild. All consumed dependencies and the resulting package are hashed.
"""
import argparse
import hashlib
import io
import lzma
from pathlib import Path
import runpy
import shlex
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'source', 'cc', 'iso', 'output-parent'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    build, source, cc, iso, parent = (p.resolve(strict=True) for p in
        (args.build, args.source, args.cc, args.iso, args.output_parent))
    if shutil.disk_usage(parent).free < 100 * 2**30:
        raise ValueError('100 GiB artifact reserve required')
    out = Path(tempfile.mkdtemp(prefix='ata-capacity.', dir=parent))
    print(out, flush=True)
    repo = Path(__file__).resolve().parents[2]
    gen = build / 'bin/pc-x86_64-smp/gen/rom/devs/ata/ata'
    sdk = build / 'bin/pc-x86_64-smp/AROS/Developer'
    stage = out / 'rom/devs/ata'
    shutil.copytree(source / 'rom/devs/ata', stage)
    patch = repo / 'aros/engine/ata-capacity.patch'
    shutil.copy2(patch, out / patch.name)
    shutil.copy2(__file__, out / Path(__file__).name)
    inputs = [cc, iso, patch, gen / '.map']
    with (out / 'build.log').open('w') as log:
        def run(command):
            command = list(map(str, command))
            log.write(shlex.join(command) + '\n'); log.flush()
            subprocess.run(command, cwd=out, stdout=log, stderr=subprocess.STDOUT, check=True)
        run(['patch', '--fuzz=0', '-p1', '-d', out, '-i', patch])
        common = [cc, '--sysroot', sdk, '-m64', '-mcmodel=large',
                  '-mno-red-zone', '-mno-ms-bitfields', '-mgeneral-regs-only']
        run(common + ['-O2', '-fno-asynchronous-unwind-tables', '-fno-omit-frame-pointer',
            '-Wno-pointer-sign', '-DAROS_USE_LOGRES', '-DAROS_BUILD_TYPE=AROS_BUILD_TYPE_PERSONAL',
            '-DUSE_EXEC_DEBUG', '-D__OOP_NOMETHODBASES__', '-D__OOP_NOATTRBASES__',
            '-include', gen / 'include/ata_deflibdefs.h', '-I' + str(stage),
            '-I' + str(gen / 'include'), '-I' + str(gen), '-MD', '-MF', out / 'scsiemu.d',
            '-c', stage / 'scsiemu.c', '-o', out / 'scsiemu.o'])
        objects = [Path(line[5:]) for line in (gen / '.map').read_text().splitlines()
                   if line.startswith('LOAD ') and line.endswith('.o')]
        old = gen / 'scsiemu.o'
        if objects.count(old) != 1 or objects[0].name != 'libentry.o' or objects[-1].name != 'ata_end.o':
            raise ValueError('Unexpected ATA object inventory')
        if any(not p.is_file() or not p.is_relative_to(build) for p in objects):
            raise ValueError('ATA objects outside selected build')
        inputs += objects
        run(common + ['-nostartfiles', '-static', '-Wl,-Map,' + str(out / 'ata.map'),
                      *[out / 'scsiemu.o' if p == old else p for p in objects],
                      '-o', out / 'ata.device'])
        inputs += [Path(name) for name in shlex.split(
            (out / 'scsiemu.d').read_text().replace('\\\n', '').split(':', 1)[1])]
        inputs += [Path(line[5:]) for line in (out / 'ata.map').read_text().splitlines()
                   if line.startswith('LOAD ') and line.endswith('.a')]
        base = out / 'baseline.pkg.xz'
        run(['xorriso', '-osirrox', 'on', '-indev', iso, '-extract',
             '/boot/pc-smp/aros-bsp.pkg.xz', base])
        pkgtool = source / 'tools/package/pkg'
        api = runpy.run_path(str(pkgtool))
        def unpack(path):
            raw = lzma.decompress(path.read_bytes()); stream = io.BytesIO(raw)
            if api['readHeader'](stream) != 1:
                raise ValueError('Invalid package header')
            entries = []
            while stream.tell() < len(raw):
                name, data = api['readFile'](stream)
                name = name.rstrip(b'\0').decode('iso-8859-1')
                if not name or Path(name).name != name or name in dict(entries):
                    raise ValueError('Invalid package member')
                entries.append((name, data))
            return entries
        entries = unpack(base)
        if sum(name == 'ata.device' for name, _ in entries) != 1:
            raise ValueError('Expected one ATA module')
        package = out / 'package'; package.mkdir()
        for name, data in entries:
            (package / name).write_bytes((out / name).read_bytes() if name == 'ata.device' else data)
        run(['python3', pkgtool, 'c', out / 'aros-bsp.pkg', package, *[n for n, _ in entries]])
        with (out / 'aros-bsp.pkg.xz').open('wb') as target:
            subprocess.run(['xz', '--check=crc32', '--x86', '-9e', '-c', str(out / 'aros-bsp.pkg')],
                           stdout=target, stderr=log, check=True)
        updated = unpack(out / 'aros-bsp.pkg.xz')
        if ([n for n, _ in entries] != [n for n, _ in updated] or
            [n for (n, a), (_, b) in zip(entries, updated) if a != b] != ['ata.device']):
            raise ValueError('Non-ATA package content changed')
        inputs += [pkgtool, out / 'ata.device', out / 'aros-bsp.pkg.xz']
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    (out / 'inputs.sha256').write_text(''.join(digest(p) + '  ' + str(p) + '\n'
                                             for p in dict.fromkeys(inputs)))
    print('PASS only ATA module changed; all other module bytes/order preserved: ' + str(out), flush=True)


if __name__ == '__main__':
    main()
