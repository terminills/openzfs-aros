#!/usr/bin/env bash
# Offline checks only. Screenshots must also be reviewed independently.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(realpath -- "${1:?private native GUI evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == native-gui-run.* ]]
scenario=$(<"$run/scenario.txt")
[[ $scenario == create-partition || $scenario == create-partition-cold ]]
pid=$(<"$run/qemu.pid")
[[ $pid =~ ^[0-9]+$ ]]
if kill -0 "$pid" 2>/dev/null;then echo 'Guest is still alive' >&2;exit 1;fi
if fuser "$run/create-partition.qcow2" >/dev/null 2>&1;then
    echo 'Partition overlay is in use' >&2;exit 1
fi
if [[ $scenario == create-partition ]];then
    rg -a -q '^Authenticated pool creation returned errno=0' "$run/serial.log"
    rg -a -q '^RESULT GUI native partition pool creation PASS' "$run/serial.log"
else
    rg -a -q '^RESULT GUI-created native pool cold import PASS exact-bytes=1 icon=1 readonly-denied=1 export=1' "$run/serial.log"
    sha256sum -c "$run/cold-source.sha256"
    sha256sum -c "$run/cold-overlay.sha256"
fi
rg -a -q '^GUI create exact DOS file readback PASS' "$run/serial.log"
rg -a -q '^Authenticated dataset unmount returned errno=0' "$run/serial.log"
rg -a -q '^Authenticated pool export returned errno=0' "$run/serial.log"
if rg -a -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
sha256sum -c "$run/partition-before.sha256"
sha256sum -c "$run/disks-before.sha256"
verify=$(mktemp -d "$run/offline.XXXXXXXX")
qemu-img convert -O raw "$run/create-partition.qcow2" "$verify/result.raw"
[[ $(stat -c %s "$verify/result.raw") == 268435456 ]]
cmp -n $((2048*512)) "$run/create-partition.raw" "$verify/result.raw"
cmp -i $((256*1024*1024-33*512)) "$run/create-partition.raw" "$verify/result.raw"
sgdisk --verify "$verify/result.raw" >"$verify/gpt.log" 2>&1
rg -q 'No problems found' "$verify/gpt.log"
dd if="$verify/result.raw" of="$verify/member.raw" bs=512 skip=2048 count=522207 conv=excl status=none
LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
    build-control/zdb -e -p "$verify/member.raw" -bcc aros_gui_created >"$verify/zdb.log" 2>&1
rg -q 'No leaks' "$verify/zdb.log"
sha256sum "$verify/result.raw" "$verify/member.raw" >"$verify/results.sha256"
echo "PASS: $scenario, DOS readback, export, GPT exterior and independent zdb. Evidence: $verify"
echo 'Screenshots require separate review; this is not physical durability proof.'
