#!/usr/bin/env python3
"""Test admission: test-qmp-ui-root.py SHORT_EXTERNAL_ROOT SYSTEM_DIRECTORY.

Uses a fake endpoint, never a guest. Keep the external root short enough for
Unix-domain socket paths. Both directories must already exist and be writable.
"""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading

helper = Path(__file__).with_name('qmp-ui.py').resolve()
external = Path(sys.argv[1]).resolve(strict=True)
system_root = Path(sys.argv[2]).resolve(strict=True)
assert external.stat().st_dev != Path('/').stat().st_dev
assert system_root.stat().st_dev == Path('/').stat().st_dev
assert helper.stat().st_dev == external.stat().st_dev, 'run this regression from an external checkout'

def invoke(root, run):
    return subprocess.run([sys.executable, str(helper), str(run), 'status'],
                          env=dict(os.environ, AROS_ARTIFACT_ROOT=str(root)),
                          text=True, capture_output=True, timeout=15)

with tempfile.TemporaryDirectory(prefix='qmp-admission.', dir=external) as directory:
    root = Path(directory)
    run = root / 'native-gui-run.fixture'
    run.mkdir()
    server = socket.socket(socket.AF_UNIX)
    server.bind(str(run / 'qmp.sock'))
    server.listen(1)
    server.settimeout(15)
    calls, errors = [], []

    def serve():
        try:
            with server.accept()[0] as connection:
                connection.settimeout(15)
                stream = connection.makefile('rwb', buffering=0)
                stream.write(b'{"QMP":{}}\n')
                for name in ('qmp_capabilities', 'query-status'):
                    request = json.loads(stream.readline())
                    calls.append(request['execute'])
                    assert request['execute'] == name
                    response = {} if name == 'qmp_capabilities' else {'status':'running'}
                    stream.write((json.dumps({'return':response})+'\n').encode())
        except Exception as error:
            errors.append(str(error))

    worker = threading.Thread(target=serve)
    worker.start()
    result = invoke(root, run)
    worker.join(16)
    server.close()
    assert not worker.is_alive() and not errors, errors
    assert result.returncode == 0, result.stderr
    assert json.loads(result.stdout) == {'status':'running'}
    assert calls == ['qmp_capabilities','query-status']
    assert json.loads((run/'ui-actions.jsonl').read_text())['action'] == 'status'

    for rejected in (root/'unrelated', run/'native-gui-run.nested'):
        rejected.mkdir()
        result = invoke(root, rejected)
        assert result.returncode == 2 and 'requires a private' in result.stderr
    with tempfile.TemporaryDirectory(prefix='qmp-system.', dir=system_root) as directory:
        system = Path(directory)
        assert system.stat().st_dev == Path('/').stat().st_dev
        result = invoke(system, system/'native-gui-run.fixture')
        assert result.returncode == 2 and 'non-system filesystem' in result.stderr
print('PASS external-root=1 external-checkout=1 unrelated-denied=1 nested-denied=1 system-denied=1 fake-qmp-only=1')
