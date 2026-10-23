#!/usr/bin/env python3
"""Operate only an explicitly named private OpenZFS QEMU evidence directory."""
import argparse
import json
import os
from pathlib import Path
import socket
import time

p = argparse.ArgumentParser()
p.add_argument('run', type=Path)
p.add_argument('action', choices=['capture', 'click', 'doubleclick', 'move', 'relative', 'press', 'leftdown', 'leftup', 'rightdown', 'rightup', 'type', 'key', 'status', 'quit'])
p.add_argument('args', nargs='*')
p.add_argument('--phase', choices=['Create', 'Read'], default='Create')
a = p.parse_args()
run = a.run.resolve()
root = Path(__file__).resolve().parents[2] / 'build-aros'
external = os.environ.get('AROS_ARTIFACT_ROOT')
if external:
    root = Path(external).resolve(strict=True)
    if not root.is_dir() or root.stat().st_dev == Path(__file__).resolve().stat().st_dev:
        p.error('external artifact root must be an existing separate filesystem')
if run.parent != root or not run.name.startswith(('native-gui-run.', 'native-import-run.', 'startup-cold.', 'file-corruption-run.')):
    p.error('requires a private native-gui-run, native-import-run or startup-cold directory')
s = socket.socket(socket.AF_UNIX)
s.settimeout(10)
endpoint = ('qmp' if run.name.startswith('native-import-run.') else 'qmp.sock')
if run.name.startswith('startup-cold.'):
    endpoint = a.phase + '.qmp'
s.connect(str(run / endpoint))
f = s.makefile('rwb', buffering=0)
json.loads(f.readline())

def cmd(name, args=None):
    request = {'execute': name}
    if args is not None:
        request['arguments'] = args
    f.write((json.dumps(request) + '\n').encode())
    while True:
        reply = json.loads(f.readline())
        if 'event' in reply:
            continue
        if 'error' in reply:
            raise RuntimeError(reply)
        return reply.get('return')

cmd('qmp_capabilities')
if a.action == 'capture':
    name = a.args[0]
    if Path(name).name != name or not name.endswith('.png'):
        p.error('capture requires a plain PNG filename')
    # PPM is available with QEMU's basic graphics support; PNG encoding is
    # optional. Preserve the original framebuffer capture as provenance.
    from PIL import Image
    raw = run / (name[:-4] + '.ppm')
    cmd('screendump', {'filename': str(raw), 'format': 'ppm'})
    with Image.open(raw) as framebuffer:
        framebuffer.save(run / name)
elif a.action == 'press':
    # Click at the current PS/2 pointer without injecting tablet coordinates.
    for down in (True, False):
        cmd('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
        time.sleep(.1)
elif a.action == 'relative':
    x, y = map(int, a.args)
    if abs(x) > 4096 or abs(y) > 4096:
        p.error('relative movement limited to 4096 per axis')
    cmd('input-send-event', {'events': [
        {'type': 'rel', 'data': {'axis': 'x', 'value': x}},
        {'type': 'rel', 'data': {'axis': 'y', 'value': y}}]})
elif a.action in ['click', 'doubleclick', 'move', 'leftdown', 'leftup', 'rightdown', 'rightup']:
    x, y, width, height = map(int, a.args)
    if not (0 <= x < width and 0 <= y < height):
        p.error('click outside supplied framebuffer dimensions')
    cmd('input-send-event', {'events': [
        {'type': 'abs', 'data': {'axis': 'x', 'value': x * 32767 // (width - 1)}},
        {'type': 'abs', 'data': {'axis': 'y', 'value': y * 32767 // (height - 1)}}]})
    time.sleep(.1)
    if a.action in ['leftdown', 'leftup', 'rightdown', 'rightup']:
        button = 'right' if a.action.startswith('right') else 'left'
        cmd('input-send-event', {'events': [{'type': 'btn', 'data': {'button': button, 'down': a.action.endswith('down')}}]})
    for _ in range(2 if a.action == 'doubleclick' else (1 if a.action == 'click' else 0)):
        for down in [True, False]:
            cmd('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
            time.sleep(.06)
elif a.action in ['type', 'key']:
    def key(code):
        # Deliver modifiers before the base key, rather than depending on
        # guest ordering of an HMP chord generated in one batch.
        keys = code.split('-')
        for item in keys:
            cmd('input-send-event', {'events': [{'type': 'key', 'data': {
                'down': True, 'key': {'type': 'qcode', 'data': item}}}]})
            time.sleep(.08)
        for item in reversed(keys):
            cmd('input-send-event', {'events': [{'type': 'key', 'data': {
                'down': False, 'key': {'type': 'qcode', 'data': item}}}]})
            time.sleep(.08)
    if a.action == 'key':
        key(a.args[0])
    else:
        mapping = {'/': 'slash', '_': 'shift-minus', '-': 'minus', ':': 'shift-semicolon',
                   '.': 'dot', ',': 'comma', '@': 'shift-2', '#': 'shift-3', '=': 'equal', ' ': 'spc', '\n': 'ret'}
        for ch in a.args[0]:
            if ch in mapping:
                key(mapping[ch])
            elif ch.isascii() and ch.isalnum():
                key(('shift-' if ch.isupper() else '') + ch.lower())
            else:
                raise ValueError('Unsupported input character ' + repr(ch))
elif a.action == 'status':
    print(json.dumps(cmd('query-status')))
elif a.action == 'quit':
    cmd('quit')
with (run / 'ui-actions.jsonl').open('a') as log:
    log.write(json.dumps({'time': time.time(), 'action': a.action, 'args': a.args}) + '\n')
s.close()
