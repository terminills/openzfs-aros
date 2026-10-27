#!/usr/bin/env python3
"""Cut only the runner's private VM at a recovery storage boundary.

This is an emulator interruption, not physical cache/power-loss evidence.
The production OpenZFS engine is not instrumented or replaced.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('pid', type=int)
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--event', choices=('pwritev_done','flush_to_disk'), default='flush_to_disk')
    parser.add_argument('--clear-ready', action='store_true',
                        help='wait for the CLEAR client input barrier before arming')
    args = parser.parse_args()
    run = args.run.resolve(strict=True)
    if not run.name.startswith('native-import-run.') or not 60 <= args.timeout <= 14400:
        raise ValueError('invalid private run or deadline')
    expected_image = str(run / 'test.iso').encode()

    def verify_process():
        command = Path(f'/proc/{args.pid}/cmdline').read_bytes().split(b'\0')
        if (not command or b'qemu-system-' not in command[0] or
                not any(expected_image in part for part in command)):
            raise RuntimeError('PID is not this run\'s QEMU process')

    verify_process()
    deadline = time.monotonic() + args.timeout
    while not (run / 'qmp').exists():
        verify_process()
        if time.monotonic() >= deadline:
            raise TimeoutError('no QMP socket')
        time.sleep(.1)
    with (run / 'recovery-cut.jsonl').open('x') as log:
        def record(kind, value):
            log.write(json.dumps({'time': time.time(), 'kind': kind, 'value': value}) + '\n')
            log.flush()
            os.fsync(log.fileno())

        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(10)
            sock.connect(str(run / 'qmp'))
            stream = sock.makefile('rwb', buffering=0)
            record('greeting', json.loads(stream.readline()))

            def call(name, arguments=None):
                stream.write((json.dumps({'execute': name, 'arguments': arguments or {}}) + '\n').encode())
                while True:
                    message = json.loads(stream.readline())
                    record(name, message)
                    if 'error' in message:
                        raise RuntimeError(message)
                    if 'return' in message:
                        return message['return']

            call('qmp_capabilities')
            status = call('query-status')
            if status['running']:
                raise RuntimeError('guest must start paused before the breakpoint is armed')
            ready = ('READY native CLEAR commit boundary: waiting for armed host Return'
                     if args.clear_ready else 'READY native writable recovery request')
            if args.clear_ready:
                call('cont')
                while ready not in (run / 'serial.log').read_text(errors='replace'):
                    verify_process()
                    serial = (run / 'serial.log').read_text(errors='replace')
                    if 'RESULT native damaged rewind test ' in serial:
                        raise RuntimeError('guest finished before CLEAR input barrier')
                    if time.monotonic() >= deadline:
                        raise TimeoutError('CLEAR input barrier not reached')
                    time.sleep(.1)
                record('clear_input_barrier', {'ready': ready})
            result = call('human-monitor-command', {'command-line': f'qemu-io ozcut0 "break {args.event} recovery-cut"'})
            if result.strip():
                raise RuntimeError(f'breakpoint setup failed: {result}')
            if args.clear_ready:
                call('human-monitor-command', {'command-line': 'sendkey ret'})
            else:
                call('cont')
            print(f'Recovery cut breakpoint armed on member 0 {args.event}', flush=True)
            marker = "blkdebug: Suspended request 'recovery-cut'"
            while marker not in (run / 'qemu.log').read_text(errors='replace'):
                verify_process()
                serial = (run / 'serial.log').read_text(errors='replace')
                if 'RESULT native damaged rewind test ' in serial:
                    raise RuntimeError('guest finished without reaching the required write boundary')
                if time.monotonic() >= deadline:
                    raise TimeoutError('write breakpoint not reached; not a crash diagnosis')
                time.sleep(.1)
            serial = (run / 'serial.log').read_text(errors='replace')
            if ready not in serial:
                raise RuntimeError('write occurred before the writable recovery request')
            if 'RESULT native damaged rewind commit PASS' in serial:
                raise RuntimeError('cut was too late: full recovery already completed')
            if args.clear_ready and 'RESULT native damaged clear rewind PASS' in serial:
                raise RuntimeError('cut was too late: CLEAR already returned')
            # QMP stop may drain block I/O and wait forever on our deliberate
            # breakpoint. Do not request it, stats, shutdown or a flush here.
            record('boundary', {'member': 0, 'event': args.event, 'returned_to_guest': False})
            verify_process()
            # No guest shutdown, export, device drain or breakpoint resume.
            os.kill(args.pid, signal.SIGKILL)
            record('signal', {'pid': args.pid, 'signal': 'SIGKILL'})
            print(f'CUT: private VM at member 0 {args.event}, before request return', flush=True)


if __name__ == '__main__':
    main()
