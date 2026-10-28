#!/usr/bin/env python3
"""Run the real private host debugger on malformed synthetic L2ARC headers.

Requires a candidate with the entry-count guard, never use the old control
debugger for these oversized fixtures. No host devices or mounted pools.
"""
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys

binary, fixtures, output = map(Path, sys.argv[1:4])
if b"Invalid L2ARC log entry count:" not in binary.read_bytes():
    raise SystemExit("Refusing debugger without entry-count validation")
output.mkdir(exist_ok=False)
for name, endian in (("log-little", "<"), ("log-big", ">")):
    source = fixtures / name
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    for count in (2, 1022, 1023, (1 << 64) - 1):
        path = output / f"{name}-{count}.raw"
        shutil.copyfile(source, path)
        with path.open("r+b") as stream:
            stream.seek(4*1024*1024 + 4*8)
            stream.write(struct.pack(endian + "Q", count))
        for verbosity in ("-l", "-lll"):
            result = subprocess.run([str(binary.resolve()), verbosity, str(path.resolve())],
                                    capture_output=True, timeout=15)
            (output / f"{path.stem}{verbosity}.log").write_bytes(result.stdout + result.stderr)
            expected = 1 if count > 1022 else 0
            if result.returncode != expected:
                raise SystemExit(f"FAIL {name} count={count} {verbosity}: exit={result.returncode}")
            if expected and f"Invalid L2ARC log entry count: {count}".encode() not in result.stdout:
                raise SystemExit("FAIL missing explicit rejection")
            if not expected and b"1 with valid cksum" not in result.stdout:
                raise SystemExit("FAIL valid log was not traversed")
            print(f"PASS {name} entries={count} {verbosity} exit={result.returncode}")
        # Disposable mutated copies only. Keep outputs and hashes, not VMs.
        with (output / "fixtures.sha256").open("a") as manifest:
            manifest.write(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n")
        path.unlink()
    if hashlib.sha256(source.read_bytes()).hexdigest() != before:
        raise SystemExit("FAIL source fixture changed")
print("PASS 16 real host debugger invocations; native runtime still required")
