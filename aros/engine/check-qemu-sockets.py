#!/usr/bin/env python3
"""Check Linux AF_UNIX path bounds before allocating QEMU test images."""
import os
from pathlib import Path
import sys

NAMES = ('monitor', 'qmp.sock', 'c-monitor', 'r-monitor', 'n-monitor')


def check(directory):
    for name in NAMES:
        path = Path(directory) / name
        if len(os.fsencode(path)) >= 108:
            raise ValueError(f'QEMU socket path exceeds 107 bytes: {path}; choose a shorter artifact root')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: check-qemu-sockets.py RUN_DIRECTORY')
    check(sys.argv[1])
