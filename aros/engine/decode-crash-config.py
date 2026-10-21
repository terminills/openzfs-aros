#!/usr/bin/env python3
"""Decode one bounded native crash-fixture config; never accept loose hex."""
import argparse
import re

MAX_CONFIG = 1024 * 1024
MAX_CAPTURE = 64 * 1024 * 1024
NOTIFY = re.compile(
    rb"[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| "
    rb"\[security.library\] secTaskNotify: [^\r\n]*\r?\n"
)


def decode(capture):
    if len(capture) > MAX_CAPTURE:
        raise ValueError("capture exceeds limit")
    lines = NOTIFY.sub(b"", capture).splitlines()
    starts = [i for i, line in enumerate(lines) if line.startswith(b"SLOG_CONFIG_BEGIN=")]
    ends = [i for i, line in enumerate(lines) if line.startswith(b"SLOG_CONFIG_END=")]
    if len(starts) != 1 or len(ends) != 1 or starts[0] >= ends[0]:
        raise ValueError("one complete config block required")
    start, end = starts[0], ends[0]
    match = re.fullmatch(rb"SLOG_CONFIG_BEGIN=([1-9][0-9]{0,6})", lines[start])
    if not match or lines[end] != b"SLOG_CONFIG_END=0":
        raise ValueError("invalid config delimiters")
    expected = int(match[1])
    if expected > MAX_CONFIG:
        raise ValueError("config exceeds limit")
    chunks = []
    for line in lines[start + 1:end]:
        match = re.fullmatch(rb"SLOG_CONFIG ((?:[0-9a-f]{2}){1,32})", line)
        if not match:
            raise ValueError("malformed config hex record")
        chunks.append(bytes.fromhex(match[1].decode("ascii")))
    payload = b"".join(chunks)
    if len(payload) != expected:
        raise ValueError("config length mismatch")
    return payload


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    parser.add_argument("output")
    args = parser.parse_args()
    with open(args.capture, "rb") as source:
        payload = decode(source.read(MAX_CAPTURE + 1))
    with open(args.output, "xb") as destination:
        destination.write(payload)
