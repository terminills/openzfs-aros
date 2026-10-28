#!/usr/bin/env python3
"""Rebuild all stdcio objects from retained sources, without cached OS objects.

Generated ABI headers and SDK archives remain explicit, hash-bound inputs.
This builds a private candidate; runtime qualification is still required.
"""
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
    for name in ('upstream', 'generated', 'sdk', 'cc', 'stdio-source', 'output-parent'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    upstream, gen, sdk, cc, fixed, parent = (p.resolve(strict=True) for p in
        (args.upstream, args.generated, args.sdk, args.cc, args.stdio_source, args.output_parent))
    if shutil.disk_usage(parent).free < 100 * 2**30:
        raise ValueError('100 GiB free reserve required')
    out = Path(tempfile.mkdtemp(prefix='stdcio-source.', dir=parent))
    print(out, flush=True)
    source = out / 'source'
    for name in ('compiler/crt/stdc', 'compiler/crt/common', 'compiler/fmtprintf'):
        shutil.copytree(upstream / name, source / name)
    (source / 'rom/dos').mkdir(parents=True)
    shutil.copy2(upstream / 'rom/dos/dos_fhflags.h', source / 'rom/dos/dos_fhflags.h')
    shutil.copy2(fixed, source / 'compiler/crt/stdc/__stdio.c')
    shutil.copy2(upstream / 'compiler/libinit/libentry.c', source / 'libentry.c')
    for name in ('LICENSE', 'LICENSE.Author', 'LICENSE.GPL', 'LICENSE.LGPL', 'ACKNOWLEDGEMENTS'):
        if (upstream / name).is_file():
            shutil.copy2(upstream / name, source / name)
    shutil.copytree(gen / 'include', out / 'generated/include')
    (out / 'generated/include/stdcio_deflibdefs.h').write_text(
        '#define LC_LIBDEFS_FILE "stdcio_libdefs.h"\n')
    for path in gen.glob('*.c'):
        shutil.copy2(path, out / 'generated' / path.name)
    shutil.copy2(gen / '.map', out / 'input.map')
    shutil.copy2(__file__, out / Path(__file__).name)
    (out / 'provenance.json').write_text(json.dumps(dict(
        upstream=str(upstream), generated=str(gen), stdio_source=str(fixed),
        upstream_head=subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip(),
        note='Source contents retained; HEAD is context, not a clean-tree claim.'), indent=2) + '\n')
    base = [str(cc), '--sysroot=' + str(sdk), '-B' + str(sdk / 'lib') + '/',
            '-m64', '-mcmodel=large', '-mno-red-zone', '-mno-ms-bitfields']
    flags = ['-O2', '-noposixc', '-fno-builtin', '-DSTDC_IO', '-D_GNU_SOURCE',
             '-D__ISO_C_VISIBLE=2011', '-Dlint', '-D__FBSDID(x)=',
             '-I' + str(out / 'generated/include'), '-I' + str(sdk / 'include/aros/stdc'),
             '-iquote', str(source / 'compiler/crt/stdc'),
             '-iquote', str(source / 'compiler/crt/common'),
             '-iquote', str(source / 'compiler/fmtprintf'),
             '-include', str(out / 'generated/include/stdcio_deflibdefs.h')]
    recorder = Path(__file__).with_name('record-source-build.py')
    objects, archives = [], []
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
                if original.parent == gen:
                    src = out / 'generated' / original.name
                else:
                    src = source / original.relative_to(upstream)
                options = flags
            if not src.is_file():
                raise ValueError(f'Missing retained source for {old}: {src}')
            obj = out / 'objects' / (str(len(objects)) + '-' + old.name)
            obj.parent.mkdir(exist_ok=True)
            run(base + options + ['-c', str(src), '-o', str(obj)])
            objects.append(str(obj))
        if len(objects) < 20:
            raise ValueError('Incomplete stdcio object inventory')
        run(base + ['-nostartfiles', '-nostdlib', '-Wl,-x', '-Wl,-Map,' + str(out / 'stdcio.map'),
                    *objects, '-Wl,--start-group', *archives, '-Wl,--end-group', '-o', str(out / 'stdcio.library')])
    subprocess.run([sys.executable, str(recorder), 'seal', str(out), 'stdcio.library', 'stdcio.map'], cwd=out, check=True)
    recipe = json.loads((out / 'rebuild.json').read_text())
    for name in recipe['external_inputs']:
        if Path(name).suffix == '.o' or Path(name).is_relative_to(upstream) or Path(name).is_relative_to(gen):
            raise ValueError('Unretained private dependency: ' + name)
    print('PASS retained stdcio candidate: ' + str(out), flush=True)


if __name__ == '__main__':
    main()
