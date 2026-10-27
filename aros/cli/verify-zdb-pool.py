#!/usr/bin/env python3
"""Decode lossless native reports and compare integrity results, not rounding."""
import re
import sys
from pathlib import Path


def decode(serial, run):
    result = bytearray()
    pattern = rf'(?:^| \| )ZDBDATA\[{run}\] offset=(\d+) bytes=(\d+) ([0-9a-f]+)$'
    for match in re.finditer(pattern, serial, re.M):
        offset, size = map(int, match.group(1, 2))
        data = bytes.fromhex(match.group(3))
        if offset != len(result) or size != len(data) or not 0 < size <= 128:
            raise ValueError('Invalid, missing or reordered report chunk')
        result.extend(data)
        if len(result) > 4 * 1024 * 1024:
            raise ValueError('Report exceeds test bound')
    statuses = re.findall(rf'(?:^| \| )RESULT native zdb pool exit pass={run} status=(-?\d+)$', serial, re.M)
    sizes = re.findall(rf'(?:^| \| )RESULT native zdb pool report pass={run} bytes=(\d+)$', serial, re.M)
    if statuses != ['0'] or sizes != [str(len(result))]:
        raise ValueError('Missing/duplicate successful report marker')
    if not result:
        raise ValueError('Empty report')
    return bytes(result)


def integrity(text, objects=False):
    marker = 'Dataset mos [META]' if objects else 'No leaks (block sum matches space maps exactly)'
    if marker not in text or re.search(
        r'refcount mismatch|MOS object .* leaked|ASSERT at|\[PF-DBG\]|\bFAIL(?:ED)?\b', text
    ):
        raise ValueError('Missing or contradictory integrity verdict')


def compare(blocks, objects, host_blocks, host_objects):
    for text, is_objects in ((blocks, False), (host_blocks, False),
                             (objects, True), (host_objects, True)):
        integrity(text, is_objects)
    counters = r'^\s*(bp count|ganged count|bp logical|bp physical|bp allocated|bp deduped|bp cloned):\s+(\d+)'
    native_counts = re.findall(counters, blocks, re.M)
    host_counts = re.findall(counters, host_blocks, re.M)
    if len(native_counts) != 7 or native_counts != host_counts or int(native_counts[0][1]) <= 0:
        raise ValueError('Block accounting differs or is incomplete')
    datasets = r'^Dataset .+$'
    features = r'^Verified .+ feature refcount of \d+ is correct$'
    # -P makes size columns exact integers. The derived %full column alone
    # can differ by 0.01 between host and AROS printf rounding.
    rows = r'^[ \t]*(\d+)[ \t]+(\d+)[ \t]+(\d+)[ \t]+(\d+)[ \t]+(\d+)[ \t]+(\d+)[ \t]+(\d+)[ \t]+[0-9.]+[ \t]+([^\n]+)$'
    totals = []
    for pattern in (datasets, features, rows):
        native = re.findall(pattern, objects, re.M)
        host = re.findall(pattern, host_objects, re.M)
        if not host or native != host:
            raise ValueError('Dataset, feature or exact object inventory differs')
        totals.append(len(native))
    return totals


def main():
    run = Path(sys.argv[1])
    serial = (run / 'serial.log').read_text()
    if re.search(r'RESULT [^\n]*\bFAIL\b|\[PF-DBG\]|ASSERT at|went out of stack limits', serial):
        raise ValueError('Native execution failed')
    reports = [decode(serial, index) for index in range(2)]
    for name, report in zip(('native-blocks.log', 'native-objects.log'), reports):
        (run / name).write_bytes(report)
    counts = compare(*(report.decode() for report in reports),
                     (run / 'host-blocks.log').read_text(),
                     (run / 'host-objects.log').read_text())
    if len(re.findall(r'(?:^| \| )RESULT native zdb pool PASS block-and-objects=1$', serial, re.M)) != 1:
        raise ValueError('Missing/duplicate final pool marker')
    print('PASS native pool integrity and exact host accounting: '
          f'datasets={counts[0]} features={counts[1]} object_rows={counts[2]}')


if __name__ == '__main__':
    main()
