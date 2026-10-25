#!/usr/bin/env python3
"""Rebuild a private alpha18 posixc module with retained sources and recipe.

Uses the selected private MetaMake sources, generated ABI headers and object
ordering. SDK archives remain hash-bound external prerequisites. This is a
qualification candidate, not a replacement for runtime testing.
"""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def localize_libdefs(include):
    """Use the retained ABI header, never the generating build's absolute path."""
    if not (include / 'posixc_libdefs.h').is_file():
        raise ValueError('Missing retained posixc ABI definitions')
    (include / 'posixc_deflibdefs.h').write_text(
        '#define LC_LIBDEFS_FILE "posixc_libdefs.h"\n')


def check_private_dependencies(snapshot, build):
    recipe = json.loads((snapshot / 'rebuild.json').read_text())
    for name in recipe['external_inputs']:
        if Path(name).resolve().is_relative_to(build.resolve()):
            raise ValueError(f'Unretained private build dependency: {name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--output-parent', type=Path, required=True)
    args = parser.parse_args()
    build, upstream, sdk, cc, parent = (p.resolve(strict=True) for p in
        (args.build, args.upstream, args.sdk, args.cc, args.output_parent))
    if shutil.disk_usage(parent).free < 100 * 2**30:
        raise ValueError('100 GiB artifact filesystem reserve required')
    out = Path(tempfile.mkdtemp(prefix='posix-source.', dir=parent))
    print(out, flush=True)
    gen = build / 'gen/compiler/crt/posixc/posixc'
    source = out / 'source'
    for name in ('posixc', 'stdc', 'common'):
        shutil.copytree(build / 'source/compiler/crt' / name,
                        source / 'compiler/crt' / name)
    shutil.copytree(build / 'source/arch/x86_64-all/posixc', source / 'arch')
    shutil.copytree(build / 'platform-include', out / 'platform-include')
    shutil.copytree(gen / 'include', out / 'generated/include')
    localize_libdefs(out / 'generated/include')
    shutil.copytree(build / 'include/aros', out / 'generated/include/aros')
    for path in gen.glob('*.c'):
        shutil.copy2(path, out / 'generated' / path.name)
    for name in ('LICENSE', 'LICENSE.Author', 'LICENSE.GPL', 'LICENSE.LGPL', 'ACKNOWLEDGEMENTS'):
        if (upstream / name).is_file():
            shutil.copy2(upstream / name, source / name)
    shutil.copy2(upstream / 'compiler/libinit/libentry.c', source / 'libentry.c')
    shutil.copy2(gen / '.map', out / 'input.map')
    shutil.copy2(__file__, out / Path(__file__).name)
    (out / 'provenance.json').write_text(json.dumps({
        'private_build': str(build), 'upstream': str(upstream),
        'upstream_head': subprocess.check_output(['git', '-C', str(upstream),
            'rev-parse', 'HEAD'], text=True).strip(),
        'note': 'Private source snapshot; upstream HEAD is context, not a clean-tree claim.'
    }, indent=2) + '\n')
    recorder = Path(__file__).with_name('record-source-build.py')
    base = [str(cc), '--sysroot', str(sdk), '-m64', '-mcmodel=large',
            '-mno-red-zone', '-mno-ms-bitfields']
    flags = ['-O2', '-fno-asynchronous-unwind-tables', '-fno-omit-frame-pointer',
             '-Wno-pointer-sign', '-Wno-parentheses', '-Wno-nonnull-compare', '-fmerge-constants',
             '-DAROS_BUILD_TYPE=AROS_BUILD_TYPE_PERSONAL', '-DAROS_USE_LOGRES',
             '-DAROS_POSIXC_BUILD', '-D__TIMER_NOLIBBASE__', '-D_GNU_SOURCE', '-D_LARGEFILE64_SOURCE',
             '-D__POSIXC_NOLIBBASE__']
    for directory in ('posixc', 'stdc', 'common'):
        flags += ['-iquote', str(source / 'compiler/crt' / directory)]
    flags += ['-I' + str(source / 'compiler/crt/posixc/include/aros/posixc'),
              '-I' + str(out / 'platform-include'), '-I' + str(out / 'generated/include'),
              '-include', str(out / 'generated/include/posixc_deflibdefs.h')]
    objects = []
    with (out / 'build.log').open('w') as log:
        def run(command):
            subprocess.run([sys.executable, str(recorder), 'run', str(out), *command],
                           cwd=out, stdout=log, stderr=subprocess.STDOUT, check=True)
        for line in (gen / '.map').read_text().splitlines():
            if not line.startswith('LOAD ') or not line.endswith('.o'):
                continue
            old = Path(line[5:])
            if old.name == 'libentry.o':
                src = source / 'libentry.c'
                compile_flags = ['-O2']
            elif old.parent.name == 'arch':
                src = source / 'arch' / (old.stem + '.s')
                compile_flags = ['-x', 'assembler-with-cpp', '-I' + str(out / 'generated/include'),
                                 '-I' + str(sdk / 'include')]
            else:
                dependency = old.with_suffix('.d').read_text().replace('\\\n', '')
                if ':' in dependency:
                    deps = shlex.split(dependency.split(':', 1)[1])
                    original = Path(deps[0]).resolve(strict=True)
                else:
                    original = (build / 'source/compiler/crt/posixc' / (old.stem + '.c')).resolve(strict=True)
                if original.parent == gen:
                    src = out / 'generated' / original.name
                    label = 'generated/' + original.name
                else:
                    relative = original.resolve().relative_to((build / 'source/compiler/crt/posixc').resolve())
                    src = source / 'compiler/crt/posixc' / relative
                    label = 'compiler/crt/posixc/' + str(relative)
                compile_flags = flags + ['-D__SRCFILENAME__="' + label + '"']
            if not src.is_file():
                raise ValueError(f'Missing source for {old}: {src}')
            obj = out / 'objects' / (str(len(objects)) + '-' + old.name)
            obj.parent.mkdir(exist_ok=True)
            run(base + compile_flags + ['-c', str(src), '-o', str(obj)])
            objects.append(str(obj))
        if len(objects) < 100:
            raise ValueError('Incomplete posixc object inventory')
        # Assembly objects without STT_FILE acquire absolute input filenames
        # during linking. Discard nonessential locals for relocatable output;
        # relocation-required symbols remain, as in the stripped normal build.
        run(base + ['-nostartfiles', '-noposixc', '-Wl,-x', '-Wl,-Map,' + str(out / 'posixc.map'),
                    *objects, '-Wl,--start-group', '-lstdc_rel', '-lstdcio_rel',
                    '-Wl,--end-group', '-o', str(out / 'posixc.library')])
    subprocess.run([sys.executable, str(recorder), 'seal', str(out),
                    'posixc.library', 'posixc.map'], cwd=out, check=True)
    check_private_dependencies(out, build)
    print(f'PASS retained posixc candidate: {out}', flush=True)


if __name__ == '__main__':
    main()
