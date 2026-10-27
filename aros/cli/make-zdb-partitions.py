#!/usr/bin/env python3
"""Embed qualification members in disposable noncontiguous MBR/GPT slots."""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source")
parser.add_argument("destination")
parser.add_argument("second", nargs="?")
parser.add_argument("--table", choices=("mbr", "gpt"), default="mbr")
args = parser.parse_args()
source = Path(args.source).resolve(strict=True)
destination = Path(args.destination)
second = Path(args.second).resolve(strict=True) if args.second else source
for member in (source, second):
    if not member.is_file() or member.stat().st_size != 64 * 1024 * 1024:
        raise SystemExit("expected regular 64-MiB qualification members")
if destination.exists() or destination.is_symlink():
    raise SystemExit("destination already exists")
if args.table == "mbr":
    maker = Path(__file__).resolve().parents[1] / "platform/make-partition-fixture.py"
    subprocess.run([sys.executable, str(maker), "mbr", str(destination)], check=True)
else:
    with destination.open("xb") as disk:
        disk.truncate(256 * 1024 * 1024)
    # Use actual GPT tooling, including both table/header CRCs and the backup.
    subprocess.run(["sgdisk", "--clear", "--set-alignment=1",
                    "--new=1:4097:135168", "--typecode=1:BF01", "--change-name=1:OpenZFS-A",
                    "--new=3:139265:270336", "--typecode=3:BF01", "--change-name=3:OpenZFS-B",
                    str(destination)], check=True)
    subprocess.run(["sgdisk", "--print", "--verify", str(destination)], check=True)
# The generator exclusively creates this new 256-MiB image with slots 0/2.
with destination.open("r+b") as disk:
    for sector, path in ((4097, source), (139265, second)):
        disk.seek(sector * 512)
        with path.open("rb") as member:
            shutil.copyfileobj(member, disk)
print(args.table.upper()+" fixture: slots=0,2 sectors=4097,139265 extent_bytes=67108864")
