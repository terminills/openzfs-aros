#!/usr/bin/env python3
"""Hash compiler-discovered engine inputs; never execute dependency-file text."""
import argparse
import hashlib
import json
import os
import pathlib


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def dependency_words(text):
    # GCC's first rule is the object prerequisite list. -MP's later empty
    # header rules must not be mistaken for additional input filenames.
    lines = text.replace('\\\n', '').splitlines()
    if not lines:
        raise ValueError('Empty dependency file')
    line = lines[0]
    words, word, escaped, separator = [], '', False, False
    for char in line:
        if escaped:
            word += char
            escaped = False
        elif char == '\\':
            escaped = True
        elif char == ':' and not separator:
            if not word:
                raise ValueError('Missing dependency target')
            words.append(word.replace('$$', '$'))
            word, separator = '', True
        elif char.isspace():
            if word:
                words.append(word.replace('$$', '$'))
                word = ''
        else:
            word += char
    if escaped or not separator:
        raise ValueError('Malformed dependency rule')
    if word:
        words.append(word.replace('$$', '$'))
    if len(words) < 2:
        raise ValueError('Empty compiler dependency list')
    return words[0], words[1:]


def record(source, build, expected):
    source, build = source.resolve(strict=True), build.resolve(strict=True)
    depfiles = sorted((build / 'objects').rglob('*.o.d'))
    if len(depfiles) != expected or expected < 1:
        raise ValueError('Compiler dependency count differs from engine inventory')
    sources = (build / 'sources.txt').read_text().splitlines()
    if len(set(sources)) != expected:
        raise ValueError('Invalid engine source inventory')
    for name in sources:
        if not name or pathlib.Path(name).is_absolute() or '..' in pathlib.Path(name).parts:
            raise ValueError('Unsafe source inventory path')
    wanted = {build / 'objects' / (name + '.o.d') for name in sources}
    if set(depfiles) != wanted:
        raise ValueError('Compiler dependency paths differ from engine inventory')
    inputs, objects = set(), []
    for depfile in depfiles:
        target, dependencies = dependency_words(depfile.read_text())
        obj = pathlib.Path(target)
        if not obj.is_absolute():
            obj = source / obj
        obj = obj.resolve(strict=True)
        if obj != depfile.with_suffix('').resolve(strict=True):
            raise ValueError('Dependency target does not match object')
        objects.append({'path': str(obj), 'sha256': sha256(obj)})
        inputs.add(depfile.resolve(strict=True))
        for name in dependencies:
            path = pathlib.Path(name)
            if not path.is_absolute():
                path = source / path
            inputs.add(pathlib.Path(os.path.abspath(path)))
    for name in ('META', 'aros/engine/build-system.mk', 'aros/engine/mmakefile.src',
                 'aros/engine/tunables.ld', 'aros/engine/record-build-inputs.py',
                 'lib/libzpool/Makefile.am', 'lib/libicp/Makefile.am',
                 'lib/libavl/Makefile.am', 'lib/libunicode/Makefile.am',
                 'lib/libzstd/Makefile.am'):
        inputs.add((source / name).resolve(strict=True))
    return {'format': 1, 'source_root': str(source), 'build_root': str(build),
            'object_count': expected, 'objects': objects,
            'linked_object': {'path': str(build / 'full-engine.o'),
                              'sha256': sha256(build / 'full-engine.o')},
            'inputs': [{'path': str(path), 'sha256': sha256(path)}
                       for path in sorted(inputs)]}


def verify(manifest):
    if (manifest.get('format') != 1 or not manifest.get('inputs') or
            manifest.get('object_count', 0) < 1 or
            manifest['object_count'] != len(manifest['objects']) or
            len({entry['path'] for entry in manifest['objects']}) != manifest['object_count']):
        raise ValueError('Invalid build input manifest')
    for entry in manifest['inputs'] + manifest['objects'] + [manifest['linked_object']]:
        if sha256(pathlib.Path(entry['path'])) != entry['sha256']:
            raise ValueError('Changed build input or object: ' + entry['path'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=pathlib.Path)
    parser.add_argument('--build-root', type=pathlib.Path)
    parser.add_argument('--expected-count', type=int)
    parser.add_argument('--verify', type=pathlib.Path)
    args = parser.parse_args()
    if args.verify:
        verify(json.loads(args.verify.read_text()))
        print('PASS compiler inputs and engine objects unchanged')
    else:
        if args.source_root is None or args.build_root is None or args.expected_count is None:
            parser.error('recording requires source-root, build-root and expected-count')
        result = record(args.source_root, args.build_root, args.expected_count)
        verify(result)
        output = args.build_root / 'compiler-inputs.json'
        temporary = output.with_suffix('.tmp')
        temporary.write_text(json.dumps(result, indent=2) + '\n')
        temporary.replace(output)
        print(f'PASS recorded {len(result["inputs"])} compiler/build inputs for {result["object_count"]} objects')


if __name__ == '__main__':
    main()
