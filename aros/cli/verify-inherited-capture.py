#!/usr/bin/env python3
# SPDX-License-Identifier: CDDL-1.0
"""Verify complete official AROS inherited-environment capture test logs."""
import argparse
import re
from pathlib import Path


def verify(text, workers):
    if workers < 1:
        raise ValueError('workers must be positive')
    if 'RESULT inherited capture FAIL' in text or 'RESULT inherited capture shell FAIL' in text:
        raise ValueError('guest reported failure')
    pattern = (r'RESULT inherited capture worker=(\d+) run=(\d+) mode=(\d+) '
               r'error=(-?\d+) status=(-?\d+) lines=(\d+)')
    rows = [tuple(map(int, row)) for row in re.findall(pattern, text)]
    expected = {(w, r, m) for w in range(workers) for r in range(16) for m in range(7)}
    if len(rows) != len(expected) or {row[:3] for row in rows} != expected:
        raise ValueError('missing, duplicate, or unexpected case')
    # Published AROS errno constants: EILSEQ=85, EOVERFLOW=84, ENOEXEC=8.
    outcomes = [(0, 23, 0), (0, 23, 3), (0, 23, 2),
                (85, 23, 0), (84, 23, 0), (8, -1, 0), (0, 23, 1)]
    for row in rows:
        if row[3:] != outcomes[row[2]]:
            raise ValueError(f'unexpected case result: {row}')
    markers = [f'RESULT inherited capture BEGIN runs=16 workers={workers}',
               f'RESULT inherited capture PASS runs=16 workers={workers} cases={workers * 112} cleanup=1',
               'RESULT inherited capture shell PASS', 'RESULT inherited capture audit DONE']
    positions = []
    for marker in markers:
        if text.count(marker) != 1:
            raise ValueError(f'missing or repeated marker: {marker}')
        positions.append(text.index(marker))
    if positions != sorted(positions):
        raise ValueError('markers out of order')
    matches = list(re.finditer(pattern, text))
    if matches[0].start() <= positions[0] or matches[-1].end() >= positions[1]:
        raise ValueError('case records outside test lifetime')
    return len(rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--workers', type=int, required=True)
    args = parser.parse_args()
    count = verify(args.log.read_text(errors='replace'), args.workers)
    print(f'PASS: {count} capture cases, {args.workers} workers, cleanup and shell completion; not engine qualification')
