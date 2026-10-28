#!/usr/bin/env python3
"""Derive a snapshot-local ABI include without rewriting retained evidence.

This does not assert equivalent output. Replay the derived snapshot and require
the original expected binary hash before using it for qualification packaging.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import shutil
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--output-parent', type=Path, required=True)
    args = parser.parse_args()
    root, parent = args.snapshot.resolve(strict=True), args.output_parent.resolve(strict=True)
    spec = importlib.util.spec_from_file_location('replay', Path(__file__).with_name('rebuild-fat-snapshot.py'))
    replay = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(replay)
    paths = replay.verify_snapshot(root)
    recipe = json.loads((root / 'rebuild.json').read_text())
    if recipe.get('artifact') != 'posixc.library' or recipe.get('format') != 1:
        raise ValueError('Expected a verified posixc recipe')
    header = Path('generated/include/posixc_deflibdefs.h')
    libdefs = Path('generated/include/posixc_libdefs.h')
    if not {header, libdefs, Path('rebuild.json'), Path('posixc.library')}.issubset(paths):
        raise ValueError('Required retained inputs missing')
    match = re.fullmatch(r'#define LC_LIBDEFS_FILE "(/[^"\n]+)"\s*', (root / header).read_text())
    if not match:
        raise ValueError('Expected one absolute ABI header forwarding definition')
    original = Path(match.group(1)).resolve(strict=True)
    if (original.name != 'posixc_libdefs.h'
            or recipe['external_inputs'].get(str(original)) != replay.digest(original)
            or replay.digest(original) != replay.digest(root / libdefs)):
        raise ValueError('Retained ABI definitions do not match the recorded external header')
    if replay.digest(root / 'posixc.library') != recipe['expected_binary']:
        raise ValueError('Original snapshot binary differs from recipe')
    if parent.stat().st_dev == Path('/').stat().st_dev or shutil.disk_usage(parent).free < 100 * 2**30:
        raise ValueError('Non-system filesystem with 100 GiB reserve required')
    out = Path(tempfile.mkdtemp(prefix='posix-localized.', dir=parent))
    for relative in paths:
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / relative, target)
    (out / header).write_text('#define LC_LIBDEFS_FILE "posixc_libdefs.h"\n')
    del recipe['external_inputs'][str(original)]
    # Keep the original compile-path identity: replay maps it to its fresh
    # directory and preserves __FILE__ strings with -ffile-prefix-map.
    (out / 'rebuild.json').write_text(json.dumps(recipe, indent=2) + '\n')
    provenance = {'source_snapshot': str(root), 'original_manifest_sha256': replay.digest(root / 'retained.sha256'),
                  'removed_external_header': str(original),
                  'retained_header_sha256': replay.digest(out / libdefs),
                  'expected_binary_unchanged': recipe['expected_binary'],
                  'status': 'Requires byte-identical replay; no new native claim'}
    (out / 'localization.json').write_text(json.dumps(provenance, indent=2) + '\n')
    shutil.copy2(__file__, out / Path(__file__).name)
    names = sorted({*paths, Path('localization.json'), Path(Path(__file__).name)})
    (out / 'retained.sha256').write_text(''.join(
        replay.digest(out / name) + '  ' + str(name) + '\n' for name in names))
    replay.verify_snapshot(out)
    print(out, flush=True)


if __name__ == '__main__':
    main()
