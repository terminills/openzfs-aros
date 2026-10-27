#!/usr/bin/env python3
"""Embed the qualification member in two disposable noncontiguous MBR slots."""
import shutil
import subprocess
import sys
from pathlib import Path

source = Path(sys.argv[1]).resolve(strict=True)
destination = Path(sys.argv[2])
second = Path(sys.argv[3]).resolve(strict=True) if len(sys.argv) == 4 else source
for member in (source, second):
    if not member.is_file() or member.stat().st_size != 64 * 1024 * 1024:
        raise SystemExit("expected regular 64-MiB qualification members")
if destination.exists() or destination.is_symlink():
    raise SystemExit("destination already exists")
maker = Path(__file__).resolve().parents[1] / "platform/make-partition-fixture.py"
subprocess.run([sys.executable, str(maker), "mbr", str(destination)], check=True)
# The generator exclusively creates this new 256-MiB image with slots 0/2.
with destination.open("r+b") as disk:
    for sector, path in ((4097, source), (139265, second)):
        disk.seek(sector * 512)
        with path.open("rb") as member:
            shutil.copyfileobj(member, disk)
print("MBR fixture: slots=0,2 sectors=4097,139265 extent_bytes=67108864")
