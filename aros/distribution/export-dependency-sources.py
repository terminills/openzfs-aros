#!/usr/bin/env python3
"""Export verified retained dependency sources, not a complete SDK/toolchain."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import tempfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def manifest(path):
    entries = {}
    for line in path.read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64}) [ *](.+)', line)
        if not match:
            raise ValueError(f'Malformed checksum record: {path}')
        name = Path(match[2])
        if not name.is_absolute() or name in entries:
            raise ValueError(f'Nonabsolute or duplicate manifest input: {name}')
        if not name.is_file() or digest(name) != match[1]:
            raise ValueError(f'Changed dependency input: {name}')
        entries[name] = match[1]
    if not entries:
        raise ValueError(f'Empty manifest: {path}')
    return entries


def environment(path):
    result = {}
    for line in path.read_text().splitlines():
        words = shlex.split(line)
        if len(words) != 2 or words[0] != 'export' or '=' not in words[1]:
            raise ValueError('Expected literal exported paths, not shell commands')
        name, value = words[1].split('=', 1)
        if not re.fullmatch(r'AROS_[A-Z_]+', name) or name in result:
            raise ValueError('Invalid or duplicate dependency variable')
        result[name] = Path(value)
    return result


def copy_tree(source, destination, checked):
    files = sorted(p for p in source.rglob('*') if '.git' not in p.relative_to(source).parts)
    count = 0
    for path in files:
        if path.is_symlink():
            raise ValueError(f'Source symlink requires explicit handling: {path}')
        if not path.is_file():
            continue
        expected = checked.get(path)
        if expected is None or digest(path) != expected:
            raise ValueError(f'Unrecorded or changed source: {path}')
        target = destination / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        with path.open('rb') as src, target.open('xb') as dst:
            shutil.copyfileobj(src, dst)
        if digest(target) != expected or digest(path) != expected:
            raise ValueError(f'Source changed during export: {path}')
        count += 1
    if not count:
        raise ValueError(f'Empty source tree: {source}')
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dependencies', type=Path)
    parser.add_argument('--output-parent', type=Path, required=True)
    args = parser.parse_args()
    root = args.dependencies.resolve(strict=True)
    parent = args.output_parent.resolve(strict=True)
    outputs = manifest(root / 'outputs.sha256')
    for name in ('dependencies.env', 'dependencies.mk'):
        if root / name not in outputs:
            raise ValueError(f'Unbound dependency metadata: {name}')
    env = environment(root / 'dependencies.env')
    for name in ('AROS_PRIVATE_PTHREAD', 'AROS_CRYPTO_ARCHIVE', 'AROS_NVPAIR_ARCHIVE'):
        if env[name] not in outputs or not env[name].is_relative_to(root):
            raise ValueError(f'Unbound dependency archive: {name}')
    pthread = env['AROS_PRIVATE_PTHREAD'].parent
    crypto = env['AROS_CRYPTO_ARCHIVE'].parents[2]
    crypto_source = env['AROS_CRYPTO_SOURCE']
    trees = [('Pthread', env['AROS_PTHREAD_INCLUDE'], pthread / 'source.sha256'),
             ('Crypto', crypto_source, crypto / 'source.sha256')]
    checked = {}
    for label, source, record in trees:
        if not source.is_relative_to(root) or not record.is_relative_to(root):
            raise ValueError(f'Source outside selected dependency build: {label}')
        checked[label] = manifest(record)
    archive = env['AROS_TIRPC_ARCHIVE']
    if not archive.is_relative_to(root) or digest(archive) != 'b47d3ac19d3549e54a05d0019a6c400674da716123858cfdb6d3bdd70a66c702':
        raise ValueError('libtirpc archive differs from the pinned upstream source')
    if not (crypto_source / 'LICENSE').is_file():
        raise ValueError('Crypto source license missing')
    if shutil.disk_usage(parent).free < 100 * 1024**3:
        raise ValueError('Output filesystem has less than 100 GiB free')
    out = Path(tempfile.mkdtemp(prefix='dependency-sources.', dir=parent))
    counts = {}
    for label, source, record in trees:
        counts[label] = copy_tree(source, out / label, checked[label])
        shutil.copy2(record, out / (label + '-original.sha256'))
    # Retain the exact complete upstream XDR archive, including its licenses.
    shutil.copy2(archive, out / archive.name)
    if digest(out / archive.name) != digest(archive):
        raise ValueError('libtirpc copy mismatch')
    for name in ('outputs.sha256', 'dependencies.env', 'dependencies.mk', 'host-tools.sha256'):
        shutil.copy2(root / name, out / name)
    for label, build, names in (
        ('Pthread', pthread, ('build-private-pthread.sh', 'inputs.txt', 'compiler.txt', 'source-inventory.txt')),
        ('Crypto', crypto, ('build-crypto.sh', 'inputs.txt', 'compiler.txt', 'cxx.txt', 'go.txt')),
        ('CryptoPreparation', crypto_source.parent, ('boringssl-aros.patch', 'patch.sha256', 'upstream-revision.txt')),
    ):
        target = out / 'Provenance' / label
        target.mkdir(parents=True)
        for name in names:
            shutil.copy2(build / name, target / name)
    (out / 'scope.json').write_text(json.dumps({
        'format': 1, 'dependency_root': str(root), 'source_files': counts,
        'archive_sha256': {str(p): h for p, h in outputs.items() if p.suffix == '.a'},
        'external_requirements': 'Matching AROS SDK, compiler, host tools and OpenZFS source checkout; recorded build scripts are provenance, not a standalone bootstrap.',
        'runtime_claim': 'No new compilation or runtime validation performed by export.'
    }, indent=2) + '\n')
    (out / 'SHA256SUMS').write_text(''.join(
        f'{digest(p)}  {p.relative_to(out)}\n' for p in sorted(out.rglob('*')) if p.is_file()))
    print(out)


if __name__ == '__main__':
    main()
