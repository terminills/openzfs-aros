#!/usr/bin/env python3
"""Rebuild stdcio (default) or stdc from retained sources, without cached objects.

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
    for name in ('upstream', 'generated', 'sdk', 'cc', 'output-parent'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--stdio-source', type=Path)
    parser.add_argument('--asm-header', type=Path,
                        help='matching generated aros/x86_64/asm.h (required for stdc)')
    parser.add_argument('--library', choices=('stdcio', 'stdc'), default='stdcio')
    args = parser.parse_args()
    if args.library == 'stdcio' and args.stdio_source is None:
        parser.error('--stdio-source is required for stdcio')
    if args.library == 'stdc' and args.stdio_source is not None:
        parser.error('--stdio-source applies only to stdcio')
    if args.library == 'stdc' and args.asm_header is None:
        parser.error('--asm-header is required for stdc')
    library = args.library
    upstream, gen, sdk, cc, parent = (p.resolve(strict=True) for p in
        (args.upstream, args.generated, args.sdk, args.cc, args.output_parent))
    fixed = args.stdio_source.resolve(strict=True) if args.stdio_source else None
    if shutil.disk_usage(parent).free < 100 * 2**30:
        raise ValueError('100 GiB free reserve required')
    out = Path(tempfile.mkdtemp(prefix=library + '-source.', dir=parent))
    print(out, flush=True)
    source = out / 'source'
    for name in ('compiler/crt/stdc', 'compiler/crt/common', 'compiler/fmtprintf'):
        shutil.copytree(upstream / name, source / name)
    (source / 'rom/dos').mkdir(parents=True)
    shutil.copy2(upstream / 'rom/dos/dos_fhflags.h', source / 'rom/dos/dos_fhflags.h')
    if fixed:
        shutil.copy2(fixed, source / 'compiler/crt/stdc/__stdio.c')
    if library == 'stdc':
        shutil.copytree(upstream / 'arch/x86_64-all/stdc', source / 'arch/x86_64-all/stdc')
        shutil.copytree(gen.parent / 'defaults', out / 'generated/defaults')
    shutil.copy2(upstream / 'compiler/libinit/libentry.c', source / 'libentry.c')
    for name in ('LICENSE', 'LICENSE.Author', 'LICENSE.GPL', 'LICENSE.LGPL', 'ACKNOWLEDGEMENTS'):
        if (upstream / name).is_file():
            shutil.copy2(upstream / name, source / name)
    shutil.copytree(gen / 'include', out / 'generated/include')
    if library == 'stdc':
        asm = out / 'generated/include/aros/x86_64/asm.h'
        asm.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.asm_header.resolve(strict=True), asm)
    (out / 'generated/include' / (library + '_deflibdefs.h')).write_text(
        '#define LC_LIBDEFS_FILE "' + library + '_libdefs.h"\n')
    for path in gen.glob('*.c'):
        shutil.copy2(path, out / 'generated' / path.name)
    shutil.copy2(gen / '.map', out / 'input.map')
    shutil.copy2(__file__, out / Path(__file__).name)
    (out / 'provenance.json').write_text(json.dumps(dict(
        upstream=str(upstream), generated=str(gen), library=library,
        stdio_source=str(fixed) if fixed else None,
        upstream_head=subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip(),
        note='Source contents retained; HEAD is context, not a clean-tree claim.'), indent=2) + '\n')
    base = [str(cc), '--sysroot=' + str(sdk), '-B' + str(sdk / 'lib') + '/',
            '-m64', '-mcmodel=large', '-mno-red-zone', '-mno-ms-bitfields']
    flags = ['-O2', '-noposixc', '-fno-builtin', '-D_GNU_SOURCE',
             '-D__ISO_C_VISIBLE=2011', '-Dlint', '-D__FBSDID(x)=',
             '-I' + str(out / 'generated/include'), '-I' + str(sdk / 'include/aros/stdc'),
             '-iquote', str(source / 'compiler/crt/stdc'),
             '-iquote', str(source / 'compiler/crt/common'),
             '-iquote', str(source / 'compiler/fmtprintf'),
             '-include', str(out / 'generated/include' / (library + '_deflibdefs.h'))]
    if library == 'stdcio':
        flags += ['-DSTDC_IO']
    else:
        flags += ['-fno-math-errno', '-DSTDC_LOCALE_DEFAULT="' +
                  str(out / 'generated/defaults/en_GB_ISO8859-1.c') + '"',
                  '-I' + str(source / 'compiler/crt/stdc/math'),
                  '-I' + str(source / 'compiler/crt/stdc/math/ld80')]
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
            elif library == 'stdc' and old.parent == gen / 'arch' and not old.with_suffix('.d').exists():
                # The OS build does not emit dependencies for these assembly
                # units. Resolve only the explicitly selected x86-64 sources.
                candidates = list((source / 'arch/x86_64-all/stdc').rglob(old.stem + '.s'))
                if len(candidates) != 1:
                    raise ValueError(f'Ambiguous or missing assembly source: {old}')
                src = candidates[0]
                options = ['-x', 'assembler-with-cpp', '-I' + str(out / 'generated/include'),
                           '-I' + str(sdk / 'include')]
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
        if len(objects) < (400 if library == 'stdc' else 20):
            raise ValueError('Incomplete ' + library + ' object inventory')
        run(base + ['-nostartfiles', '-nostdlib', '-Wl,-x', '-Wl,-Map,' + str(out / (library + '.map')),
                    *objects, '-Wl,--start-group', *archives, '-Wl,--end-group', '-o', str(out / (library + '.library'))])
    subprocess.run([sys.executable, str(recorder), 'seal', str(out), library + '.library', library + '.map'], cwd=out, check=True)
    recipe = json.loads((out / 'rebuild.json').read_text())
    for name in recipe['external_inputs']:
        if Path(name).suffix == '.o' or Path(name).is_relative_to(upstream) or Path(name).is_relative_to(gen):
            raise ValueError('Unretained private dependency: ' + name)
    print('PASS retained ' + library + ' candidate: ' + str(out), flush=True)


if __name__ == '__main__':
    main()
