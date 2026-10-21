#!/usr/bin/env python3
"""Throttle writes only in a caller-selected private MMP fixture."""
import json
import os
from pathlib import Path
import socket
import sys

run = Path(sys.argv[1]).resolve(strict=True)
release = len(sys.argv) == 3 and sys.argv[2] == 'release'
root = Path(os.environ['AROS_ARTIFACT_ROOT']).resolve(strict=True)
if run.parent != root or not run.name.startswith('mmp-contend.'):
    raise SystemExit('Not a private MMP fixture')
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
    sock.settimeout(20)
    sock.connect(str(run / 'stall.qmp'))
    stream = sock.makefile('rwb', buffering=0)
    print(stream.readline().decode().strip(), flush=True)
    def command(name, arguments=None):
        request = {'execute': name}
        if arguments is not None:
            request['arguments'] = arguments
        print(json.dumps(request), flush=True)
        stream.write((json.dumps(request) + '\n').encode())
        while True:
            line = stream.readline()
            if not line:
                raise RuntimeError('QMP closed')
            reply = json.loads(line)
            print(json.dumps(reply), flush=True)
            if 'error' in reply:
                raise RuntimeError(reply)
            if 'return' in reply:
                return reply['return']
    command('qmp_capabilities')
    for index in (0, 1):
        command('block_set_io_throttle', {
            'id': f'mmpdisk{index}', 'bps': 0, 'bps_rd': 0,
            'bps_wr': 0 if release else 64,
            'iops': 0, 'iops_rd': 0, 'iops_wr': 0})
    devices = command('query-block')
    for index in (0, 1):
        matches = [d for d in devices if d.get('qdev') == f'mmpdisk{index}']
        if len(matches) != 1 or matches[0]['inserted']['bps_wr'] != (0 if release else 64):
            raise RuntimeError('Throttle readback mismatch')
    print('WRITE THROTTLES RELEASED' if release else 'WRITE THROTTLES ARMED 64 B/s', flush=True)
