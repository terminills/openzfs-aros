#!/usr/bin/env python3
"""Build the AFS companion entirely from retained source, never cached objects."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'upstream', 'sdk', 'cc', 'output-parent'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    build, upstream, sdk, cc, parent = (p.resolve(strict=True) for p in
        (args.build, args.upstream, args.sdk, args.cc, args.output_parent))
    if parent.stat().st_dev == Path('/').stat().st_dev:
        raise ValueError('Explicit non-system artifact filesystem required')
    if shutil.disk_usage(parent).free < 100 * 2**30:
        raise ValueError('100 GiB artifact reserve required')
    repo = Path(__file__).resolve().parents[2]
    genbase = build / 'bin/pc-x86_64-smp/gen'
    generated = genbase / 'rom/filesys/afs/afs'
    out = Path(tempfile.mkdtemp(prefix='afs-source.', dir=parent))
    print(out, flush=True)
    source = out / 'source'
    source.mkdir()
    for path in (upstream / 'rom/filesys/afs').iterdir():
        if path.is_file() and path.suffix in ('.c', '.h', '.conf'):
            shutil.copy2(path, source / path.name)
    shutil.copytree(generated / 'include', out / 'generated/include')
    for name in ('afs_start.c', 'afs_end.c'):
        shutil.copy2(generated / name, out / 'generated' / name)
    (out / 'generated/include/afs_deflibdefs.h').write_text(
        '#define LC_LIBDEFS_FILE "afs_libdefs.h"\n')
    for name in ('LICENSE', 'LICENSE.Author', 'LICENSE.GPL', 'LICENSE.LGPL', 'ACKNOWLEDGEMENTS'):
        if (upstream / name).is_file():
            shutil.copy2(upstream / name, source / name)
    patches = ('afs-single-die-reply.patch', 'afs-rename-pinned-link.patch',
               'afs-inhibit-root-flush.patch')
    for name in patches:
        shutil.copy2(repo / 'aros/platform' / name, out / name)
        with (out / name).open('rb') as patch:
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p0', '-d', str(source)],
                           stdin=patch, check=True)
    shutil.copy2(__file__, out / Path(__file__).name)
    (out / 'provenance.json').write_text(json.dumps({
        'upstream': str(upstream), 'configured_build': str(build),
        'upstream_head': subprocess.check_output(['git', '-C', str(upstream),
            'rev-parse', 'HEAD'], text=True).strip(),
        'note': 'Retained source hashes are authoritative; HEAD is not a clean-tree claim.',
        'patches': list(patches)
    }, indent=2) + '\n')
    (out / 'build-options.txt').write_text(
        'full_afs=1\nrename_pinned_link=1\nfinal_root_flush=1\ninhibit_trace=0\n')
    recorder = repo / 'aros/engine/record-source-build.py'
    base = [str(cc), '--sysroot', str(sdk), '-m64', '-mcmodel=large',
            '-mno-red-zone', '-mno-ms-bitfields']
    flags = ['-iquote', str(source), '-O2', '-fno-asynchronous-unwind-tables',
             '-fno-omit-frame-pointer', '-Wno-pointer-sign', '-Wno-parentheses',
             '-DAROS_BUILD_TYPE=AROS_BUILD_TYPE_PERSONAL', '-DUSE_EXEC_DEBUG', '-DDEBUG=0',
             '-D__INTUITION_NOLIBBASE__', '-I' + str(out / 'generated/include'),
             '-include', str(out / 'generated/include/afs_deflibdefs.h')]
    # Preserve handler start/end ordering, while explicitly excluding the Unix
    # support implementation. Every selected object is compiled afresh below.
    names = ['afs_start'] + [p.stem for p in sorted(source.glob('*.c'))
                            if p.name != 'os_unix_support.c'] + ['afs_end']
    if set(names) != {p.stem for p in generated.glob('*.o')}:
        raise ValueError('Source and configured AFS object inventories differ')
    (out / 'objects').mkdir()
    objects = []
    with (out / 'build.log').open('w') as log:
        def run(command):
            subprocess.run([sys.executable, str(recorder), 'run', str(out), *command],
                           cwd=out, stdout=log, stderr=subprocess.STDOUT, check=True)
        for name in names:
            folder = out / 'generated' if name in ('afs_start', 'afs_end') else source
            label = ('generated/' if folder.name == 'generated' else 'rom/filesys/afs/') + name + '.c'
            obj = out / 'objects' / (name + '.o')
            run(base + flags + ['-D__SRCFILENAME__="' + label + '"',
                '-c', str(folder / (name + '.c')), '-o', str(obj)])
            objects.append(str(obj))
        run(base + ['-nostartfiles', '-Wl,-Map,' + str(out / 'afs.map'), *objects,
            '-o', str(out / 'afs-handler'), '-L' + str(genbase / 'lib'),
            '-static', '-Wl,--start-group', '-ldebug', '-larossupport', '-Wl,--end-group'])
    subprocess.run([sys.executable, str(recorder), 'seal', str(out),
                    'afs-handler', 'afs.map'], cwd=out, check=True)
    recipe = json.loads((out / 'rebuild.json').read_text())
    for name in recipe['external_inputs']:
        if Path(name).resolve().is_relative_to(generated):
            raise ValueError('Unretained AFS generated source dependency: ' + name)
    print('PASS retained full-source AFS candidate: ' + str(out), flush=True)


if __name__ == '__main__':
    main()
