#!/usr/bin/env python3
"""Check AROS SDK link prerequisites without executing a target binary."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--sdk', type=pathlib.Path, required=True)
    parser.add_argument('--output-parent', type=pathlib.Path, required=True)
    parser.add_argument('--profile', choices=('engine', 'cli'), default='cli')
    args = parser.parse_args()
    sdk = args.sdk.resolve(strict=True)
    source = pathlib.Path(__file__).with_name('sdk-link-probe.c').resolve()
    if not (sdk / 'include/exec/types.h').is_file() or not (sdk / 'lib').is_dir():
        parser.error('Expected an explicit AROS Developer SDK directory')
    parent = args.output_parent.resolve(strict=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix='sdk-links.', dir=parent))
    print('SDK prerequisite evidence: ' + str(output), flush=True)
    results = []
    for name in (('pwrite',) if args.profile == 'engine' else ('pwrite', 'pipe2')):
        command = [args.cc, '--sysroot=' + str(sdk), '-B' + str(sdk / 'lib') + '/',
                   '-nix', '-O0', '-DOZ_PROBE_' + name.upper(), str(source),
                   '-o', str(output / name)]
        with (output / (name + '.log')).open('wb') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        results.append({'symbol': name, 'command': command, 'status': result.returncode})
        print(('PASS' if result.returncode == 0 else 'FAIL') + ': SDK links ' + name, flush=True)
    record = {'sdk': str(sdk), 'profile': args.profile, 'probe_sha256': digest(source),
              'results': results, 'runtime_verified': False}
    (output / 'results.json').write_text(json.dumps(record, indent=2) + '\n')
    if any(item['status'] for item in results):
        print('Missing SDK prerequisite; inspect per-symbol logs. No fallback or runtime modification performed.')
        return 1
    print('Link prerequisites only: matching-runtime semantics still require native testing.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
