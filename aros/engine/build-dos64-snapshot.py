#!/usr/bin/env python3
"""Build a private DOS64 source snapshot; no cached module objects are used."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('upstream', 'generated', 'sdk', 'cc', 'output-parent'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    upstream, gen, sdk, cc, parent = (p.resolve(strict=True) for p in
        (args.upstream, args.generated, args.sdk, args.cc, args.output_parent))
    if shutil.disk_usage(parent).free < 100 * 2**30:
        raise ValueError('100 GiB artifact reserve required')
    out = Path(tempfile.mkdtemp(prefix='dos64-source.', dir=parent))
    print(out, flush=True)
    source = out / 'source'
    shutil.copytree(upstream / 'rom/dos64', source / 'rom/dos64')
    (source / 'rom/dos').mkdir(parents=True)
    shutil.copy2(upstream / 'rom/dos/dos_fhflags.h', source / 'rom/dos/dos_fhflags.h')
    shutil.copy2(upstream / 'compiler/libinit/libentry.c', source / 'libentry.c')
    for name in ('LICENSE', 'LICENSE.Author', 'LICENSE.GPL', 'LICENSE.LGPL', 'ACKNOWLEDGEMENTS'):
        if (upstream / name).is_file():
            shutil.copy2(upstream / name, source / name)
    shutil.copytree(gen / 'include', out / 'generated/include')
    (out / 'generated/include/dos64_deflibdefs.h').write_text(
        '#define LC_LIBDEFS_FILE "dos64_libdefs.h"\n')
    for path in gen.glob('*.c'):
        shutil.copy2(path, out / 'generated' / path.name)
    shutil.copy2(gen / '.map', out / 'input.map')
    shutil.copy2(__file__, out / Path(__file__).name)
    (out / 'provenance.json').write_text(json.dumps(dict(
        upstream=str(upstream), generated=str(gen),
        upstream_head=subprocess.check_output(['git', '-C', str(upstream),
            'rev-parse', 'HEAD'], text=True).strip(),
        note='Source contents retained; HEAD is context, not a clean-tree claim.'), indent=2) + '\n')
    recorder = Path(__file__).with_name('record-source-build.py')
    base = [str(cc), '--sysroot=' + str(sdk), '-B' + str(sdk / 'lib') + '/',
            '-m64', '-mcmodel=large', '-mno-red-zone', '-mno-ms-bitfields']
    flags = ['-O2', '-fno-builtin', '-DNO_INLINE_STDARG', '-DUSE_EXEC_DEBUG',
             '-I' + str(source / 'rom/dos'), '-I' + str(source / 'rom/dos64'),
             '-I' + str(out / 'generated/include'),
             '-include', str(out / 'generated/include/dos64_deflibdefs.h')]
    objects, archives, compiled = [], [], set()
    with (out / 'build.log').open('w') as log:
        def run(command):
            subprocess.run([sys.executable, str(recorder), 'run', str(out), *command],
                           cwd=out, stdout=log, stderr=subprocess.STDOUT, check=True)
        for line in (gen / '.map').read_text().splitlines():
            if not line.startswith('LOAD '):
                continue
            old = Path(line[5:])
            if old.suffix == '.a':
                archives.append(str(old.resolve(strict=True)))
                continue
            if old.suffix != '.o':
                continue
            if old.name == 'libentry.o':
                src, options = source / 'libentry.c', ['-O2']
            else:
                deps = shlex.split(old.with_suffix('.d').read_text().replace('\\\n', '').split(':', 1)[1])
                original = Path(deps[0]).resolve(strict=True)
                src = out / 'generated' / original.name if original.parent == gen else source / original.relative_to(upstream)
                options = flags
            if not src.is_file() or src in compiled:
                raise ValueError(f'Missing or duplicate source: {src}')
            compiled.add(src)
            obj = out / 'objects' / old.name
            obj.parent.mkdir(exist_ok=True)
            run(base + options + ['-c', str(src), '-o', str(obj)])
            objects.append(str(obj))
        expected = set((source / 'rom/dos64').glob('*.c')) | {
            source / 'libentry.c', out / 'generated/dos64_start.c', out / 'generated/dos64_end.c'}
        if compiled != expected:
            raise ValueError(f'Incomplete DOS64 source inventory: missing={expected-compiled}, extra={compiled-expected}')
        run(base + ['-nostartfiles', '-nostdlib', '-Wl,-x', '-Wl,-Map,' + str(out / 'dos64.map'),
                    *objects, '-Wl,--start-group', *archives, '-Wl,--end-group', '-o', str(out / 'dos64.library')])
    subprocess.run([sys.executable, str(recorder), 'seal', str(out), 'dos64.library', 'dos64.map'],
                   cwd=out, check=True)
    recipe = json.loads((out / 'rebuild.json').read_text())
    for name in recipe['external_inputs']:
        if Path(name).suffix == '.o' or Path(name).is_relative_to(upstream) or Path(name).is_relative_to(gen):
            raise ValueError('Unretained source/object dependency: ' + name)
    print('PASS retained DOS64 candidate: ' + str(out), flush=True)


if __name__ == '__main__':
    main()
