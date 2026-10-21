#!/usr/bin/env python3
"""Generate native client version constants from the upstream META file."""
import pathlib
import re
import sys


def header(source):
    values = {}
    for line in source.splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        key, sep, value = line.partition(':')
        if not sep:
            raise ValueError('META entry has no colon')
        key, value = key.strip(), value.strip()
        if key in values:
            raise ValueError('duplicate META field: ' + key)
        values[key] = value
    for key in ('Name', 'Version'):
        if not values.get(key):
            raise ValueError('missing META field: ' + key)
    selected = {key: values.get(key, '') for key in ('Name', 'Version', 'Release')}
    for key, value in selected.items():
        if value and not re.fullmatch(r'[A-Za-z0-9_.+~-]+', value):
            raise ValueError('invalid version token: ' + key)
    alias = selected['Name'] + '-' + selected['Version']
    if selected['Release']:
        alias += '-' + selected['Release']
    lines = ['/* Generated from upstream META. Do not edit. */',
             '#ifndef OPENZFS_AROS_METADATA_H', '#define OPENZFS_AROS_METADATA_H']
    for key, value in selected.items():
        lines.append(f'#define ZFS_META_{key.upper()} "{value}"')
    lines += [f'#define ZFS_META_ALIAS "{alias}"', '#endif', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit('usage: generate-metadata.py META output.h')
    try:
        generated = header(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8'))
        pathlib.Path(sys.argv[2]).write_text(generated, encoding='utf-8')
    except (OSError, ValueError) as error:
        sys.exit(str(error))
