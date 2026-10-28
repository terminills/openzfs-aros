#!/usr/bin/env python3
"""Build a retained POSIX snapshot variant with pre-descriptor stream flushing."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--output-parent', required=True, type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('snapshot', here / 'rebuild-fat-snapshot.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    root = args.snapshot.resolve(strict=True)
    paths = helper.verify_snapshot(root)
    if Path('rebuild.json') not in paths:
        raise ValueError('Unverified rebuild recipe')
    recipe = json.loads((root / 'rebuild.json').read_text())
    if recipe['format'] != 1 or recipe['artifact'] != 'posixc.library':
        raise ValueError('Expected a retained POSIX snapshot')
    for name, expected in recipe['external_inputs'].items():
        if helper.digest(Path(name)) != expected:
            raise ValueError(f'External dependency changed: {name}')
    parent = args.output_parent.resolve(strict=True)
    if shutil.disk_usage(parent).free < 100 * 2**30:
        raise ValueError('100 GiB artifact reserve required')
    out = Path(tempfile.mkdtemp(prefix='posix-flush-source.', dir=parent))
    print(out, flush=True)
    helper.copy_sources(root, out, paths)
    for name in ('LICENSE', 'LICENSE.Author', 'LICENSE.GPL', 'LICENSE.LGPL', 'ACKNOWLEDGEMENTS'):
        if (root / 'source' / name).is_file():
            shutil.copy2(root / 'source' / name, out / 'source' / name)
    (out / 'objects').mkdir()
    shutil.copy2(here.parent / 'cli/posix-stream-end.c', out / 'source/posix-stream-end.c')
    shutil.copy2(__file__, out / Path(__file__).name)
    (out / 'base-snapshot.sha256').write_text(
        helper.digest(root / 'retained.sha256') + '  ' + str(root / 'retained.sha256') + '\n')
    recorder = here / 'record-source-build.py'
    hook = out / 'objects/posix-stream-end.o'
    inserted = False
    with (out / 'build.log').open('w') as log:
        def run(command):
            if command[0] != recipe['compiler']:
                raise ValueError('Unexpected executable in recipe')
            subprocess.run([sys.executable, str(recorder), 'run', str(out), *command],
                cwd=out, stdout=log, stderr=subprocess.STDOUT, check=True)
        for original in recipe['commands']:
            command = [arg.replace(recipe['snapshot'], str(out)) for arg in original]
            if '-c' in command:
                source = command[command.index('-c') + 1]
                if source.endswith('/posixc/__stdio.c'):
                    extra = command.copy()
                    extra[extra.index('-c') + 1] = str(out / 'source/posix-stream-end.c')
                    extra[extra.index('-o') + 1] = str(hook)
                    if '-MF' in extra:
                        extra[extra.index('-MF') + 1] = str(hook.with_suffix('.d'))
                    run(extra)
                    inserted = True
            else:
                if not inserted:
                    raise ValueError('No matching POSIX compilation context')
                command.insert(command.index('-o'), str(hook))
            run(command)
    subprocess.run([sys.executable, str(recorder), 'seal', str(out),
        'posixc.library', 'posixc.map'], cwd=out, check=True)
    print(f'PASS retained POSIX stream-flush build: {out}', flush=True)


if __name__ == '__main__':
    main()
