#!/usr/bin/env python3
"""Generate the pipe2 import from a verified matching POSIX source snapshot."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile


def vector(text, form):
    patterns = {
        'start': r'AROS_GM_STACKCALL\(pipe2,\s*PosixC,\s*(\d+)\)',
        'stub': r'AROS_GM_LIBFUNCSTUB\(pipe2,\s*PosixCBase,\s*(\d+)\)',
    }
    values = re.findall(patterns[form], text)
    if len(values) != 1 or int(values[0]) < 5:
        raise ValueError('Missing, duplicate or invalid pipe2 ABI vector')
    return int(values[0])


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def matching_vector(runtime, stub, generated):
    expected = vector(runtime, 'start')
    actual = vector(stub, 'stub')
    regenerated = vector(generated, 'start')
    if actual != expected or regenerated != expected:
        raise ValueError(f'pipe2 ABI mismatch: runtime={expected}, stub={actual}, regenerated={regenerated}')
    return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    for name in ('library', 'cc', 'sdk', 'genmodule', 'output-parent'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('stage_mmake', repo/'aros/distribution/stage-mmake.py')
    stage = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(stage)
    snapshot = args.snapshot.resolve(strict=True)
    library = args.library.resolve(strict=True)
    stage.require_posix_snapshot(snapshot, library)
    start = snapshot/'generated/posixc_start.c'
    config = snapshot/'source/compiler/crt/posixc/posixc.conf'
    expected = vector(start.read_text(), 'start')
    parent = args.output_parent.resolve(strict=True)
    subprocess.run(['bash', '-c', 'source "$1"; oz_storage_preflight "$2"',
                    'storage-check', str(repo/'aros/engine/storage-preflight.sh'), str(parent)], check=True)
    inputs = {str(p.resolve(strict=True)): digest(p.resolve(strict=True))
              for p in (library, config, start, args.genmodule, args.cc)}
    out = Path(tempfile.mkdtemp(prefix='posix-import.', dir=parent))
    (out/'stubs').mkdir()
    print(f'Evidence: {out}', flush=True)
    commands = [
        [str(args.genmodule), '-c', str(config), '-d', str(out), '-l', str(out/'stubs'),
         'writefiles', 'posixc', 'library'],
    ]
    with (out/'build.log').open('w') as log:
        subprocess.run(commands[0], stdout=log, stderr=subprocess.STDOUT, check=True)
        stub = out/'stubs/posixc_pipe2_stub.c'
        actual = matching_vector(start.read_text(), stub.read_text(),
                                 (out/'posixc_start.c').read_text())
        command = [str(args.cc), '--sysroot='+str(args.sdk), '-B'+str(args.sdk/'lib')+'/',
                   '-c', str(stub), '-o', str(out/'pipe2.o')]
        commands.append(command)
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    for path, expected_hash in inputs.items():
        if digest(Path(path)) != expected_hash:
            raise ValueError('Input changed during import build: ' + path)
    (out/'provenance.json').write_text(json.dumps(dict(
        symbol='pipe2', vector=actual, selected_library=str(library),
        inputs=inputs, commands=commands, runtime_tested=False), indent=2)+'\n')
    with (out/'outputs.sha256').open('w') as stream:
        for path in (stub, out/'pipe2.o', out/'provenance.json'):
            stream.write(f'{digest(path)}  {path.name if path.parent == out else "stubs/"+path.name}\n')
    print(f'PASS pipe2 import vector={actual}; no runtime or SDK modified: {out}')


if __name__ == '__main__':
    main()
