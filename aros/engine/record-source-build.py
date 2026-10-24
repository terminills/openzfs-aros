#!/usr/bin/env python3
"""Record native compiler commands and seal a trusted private source snapshot."""
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    operation, directory, *args = sys.argv[1:]
    root = Path(directory).resolve(strict=True)
    history = root / 'build-commands.jsonl'
    if operation == 'run':
        if '-c' in args:
            obj = Path(args[args.index('-o') + 1])
            args += ['-MD', '-MF', str(obj.with_suffix('.d'))]
        with history.open('a') as log:
            log.write(json.dumps(args) + '\n')
        subprocess.run(args, check=True)
    elif operation == 'seal':
        artifact, mapname = args
        if Path(artifact).name != artifact or Path(mapname).name != mapname:
            raise ValueError('Artifact/map must be local basenames')
        commands = [json.loads(line) for line in history.read_text().splitlines()]
        compiler = commands[0][0]
        if any(command[0] != compiler for command in commands):
            raise ValueError('Mixed compilers')
        external = {Path(compiler).absolute()}
        for dep in root.rglob('*.d'):
            for name in shlex.split(dep.read_text().replace('\\\n', '').split(':', 1)[1]):
                path = Path(name).resolve(strict=True)
                if not path.is_relative_to(root):
                    external.add(path)
        for line in (root / mapname).read_text().splitlines():
            if line.startswith('LOAD '):
                path = Path(line[5:]).resolve(strict=True)
                if not path.is_relative_to(root):
                    external.add(path)
        recipe = {'format': 1, 'artifact': artifact, 'snapshot': str(root),
                  'compiler': compiler, 'commands': commands,
                  'external_inputs': {str(p): digest(p) for p in sorted(external)},
                  'expected_binary': digest(root / artifact)}
        (root / 'rebuild.json').write_text(json.dumps(recipe, indent=2) + '\n')
        helper = Path(__file__).with_name('rebuild-fat-snapshot.py')
        shutil.copy2(helper, root / helper.name)
        shutil.copy2(__file__, root / Path(__file__).name)
        retained = sorted(p for p in root.rglob('*') if p.is_file()
                          and p != root / 'retained.sha256'
                          and p.suffix not in ('.o', '.map', '.log')
                          and not p.is_relative_to(root / 'package'))
        (root / 'retained.sha256').write_text(''.join(
            digest(p) + '  ' + str(p.relative_to(root)) + '\n' for p in retained))
    else:
        raise ValueError('Expected run or seal')


if __name__ == '__main__':
    main()
