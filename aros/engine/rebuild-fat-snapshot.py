#!/usr/bin/env python3
"""Replay a trusted native source snapshot; matching SDK/toolchain required.

Checksums detect drift, not malicious provenance. Review downloaded recipes
before executing them. No shared source tree or cached FAT objects are used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_snapshot(root):
    paths = []
    for line in (root / 'retained.sha256').read_text().splitlines():
        expected, name = line.split('  ', 1)
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Invalid retained path')
        path = root / relative
        if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()) or digest(path) != expected:
            raise ValueError(f'Retained input changed: {name}')
        paths.append(relative)
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--output-parent', required=True, type=Path)
    args = parser.parse_args()
    root = args.snapshot.resolve(strict=True)
    paths = verify_snapshot(root)
    if Path('rebuild.json') not in paths:
        raise ValueError('Unverified rebuild recipe')
    recipe = json.loads((root / 'rebuild.json').read_text())
    if recipe['format'] != 1:
        raise ValueError('Unsupported recipe')
    artifact = recipe.get('artifact', 'fat.handler')
    if Path(artifact).name != artifact or artifact in ('', '.', '..'):
        raise ValueError('Invalid output artifact')
    for name, expected in recipe['external_inputs'].items():
        if digest(Path(name)) != expected:
            raise ValueError(f'External dependency changed: {name}')
    parent = args.output_parent.resolve(strict=True)
    if shutil.disk_usage(parent).free < 100 * 2**30:
        raise ValueError('100 GiB artifact filesystem reserve required')
    output = Path(tempfile.mkdtemp(prefix='fat-replay.', dir=parent))
    (output / 'objects').mkdir()
    for relative in paths:
        # Explicitly omit the binary and prior dependency/provenance outputs.
        if relative.suffix not in ('.c', '.h'):
            continue
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / relative, target)
    with (output / 'build.log').open('w') as log:
        for command in recipe['commands']:
            if command[0] != recipe['compiler']:
                raise ValueError('Unexpected executable in recipe')
            command = [arg.replace(recipe['snapshot'], str(output)) for arg in command]
            if '-c' in command:
                # Preserve recorded __FILE__ strings when the original build
                # used absolute paths, without requiring that directory.
                command.append('-ffile-prefix-map=' + str(output) + '=' + recipe['snapshot'])
            log.write(json.dumps(command) + '\n')
            log.flush()
            subprocess.run(command, cwd=output, stdout=log, stderr=subprocess.STDOUT, check=True)
    actual = digest(output / artifact)
    (output / 'binary.sha256').write_text(actual + '  ' + artifact + '\n')
    if actual != recipe['expected_binary']:
        raise ValueError(f'Rebuilt bytes differ; retain evidence at {output}')
    print(f'PASS byte-identical {artifact} rebuild without source checkout: {output}')


if __name__ == '__main__':
    main()
