#!/usr/bin/env bash
# Check machine evidence; screenshots still require visual review.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
run=$(realpath -- "${1:?private native GUI evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == native-gui-run.* ]]
[[ $(<"$run/scenario.txt") == create ]]
pid=$(<"$run/qemu.pid")
[[ $pid =~ ^[0-9]+$ ]]
if kill -0 "$pid" 2>/dev/null;then echo 'Guest is still alive' >&2;exit 1;fi
rg -a -q '^pool.create: authenticated worker started' "$run/serial.log"
rg -a -q '^Authenticated pool creation returned errno=0' "$run/serial.log"
rg -a -q '^GUI create MOUNTED volume=OZCREATED file=CreatedByManager.txt' "$run/serial.log"
rg -a -q '^GUI create exact DOS file readback PASS' "$run/serial.log"
rg -a -q '^Authenticated dataset unmount returned errno=0' "$run/serial.log"
[[ $(rg -a -c '^Authenticated pool export returned errno=0' "$run/serial.log") == 2 ]]
rg -a -q '^RESULT GUI pool creation PASS' "$run/serial.log"
if rg -a -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
if rg -a -q '^RESULT GUI dRAID pool creation PASS parity=1 files=3 distributed-spares=1 exact-DOS-bytes=1 export=1' "$run/serial.log";then
    echo "PASS: GUI-created dRAID, exact native DOS readback, clean unmount/export."
else
    echo "PASS: GUI-created mirror, exact native DOS readback, clean unmount/export."
fi
echo "Review screenshots separately. RAM-backed pool: NOT cold-persistence proof."
