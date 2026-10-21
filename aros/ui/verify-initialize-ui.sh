#!/usr/bin/env bash
# Verify the saved native GUI status response, then inspect exported images.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
run=$(realpath -- "${1:?private native GUI evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == native-gui-run.* ]]
scenario=$(<"$run/scenario.txt")
[[ $scenario == export || $scenario == initialize-readonly ]]
pid=$(<"$run/qemu.pid")
[[ $pid =~ ^[0-9]+$ ]]
if kill -0 "$pid" 2>/dev/null;then echo 'Guest is still alive' >&2;exit 1;fi
command -v fuser >/dev/null
if fuser "$run/disk0.raw" "$run/disk1.raw" >"$run/initialize-open-users.log" 2>&1;then
    echo 'Disk images have active users' >&2;exit 1
fi
if [[ $scenario == export ]];then
    rg -a -q '^pool.initialize: authenticated worker started' "$run/serial.log"
fi
rg -a -q '^pool.status: OK' "$run/serial.log"
rg -a -q '^pool.export: authenticated worker started' "$run/serial.log"
rg -a -q '^GUI authenticated export absent pool PASS' "$run/serial.log"
rg -a -q '^RESULT authenticated native GUI PASS' "$run/serial.log"
if rg -a -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
if [[ $scenario == export ]];then
    for member in 0 1;do
        rg -a -q "^initialize \\| /AROS/ata.device/$member \\| COMPLETE \\| guid=[0-9]+ errors=0 " "$run/serial.log"
    done
    [[ $(rg -a -c '^  parent=mirror-.* used=[1-9][0-9]* .* total=[1-9][0-9]* progress=1000/1000' "$run/serial.log") == 2 ]]
else
    for member in 0 1;do
        rg -a -q "^initialize \\| /AROS/ata.device/$member \\| SUSPENDED \\| guid=[0-9]+ stored-only=readonly counters=unavailable " "$run/serial.log"
    done
    [[ $(rg -a -c '^  parent=mirror-.* runtime progress unavailable \(stored state above\)' "$run/serial.log") == 2 ]]
    sha256sum -c "$run/disks-before.sha256"
fi
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/initialize-before.sha256"
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
"$repo/build-control/zdb" -e -p "$run/disk0.raw" -p "$run/disk1.raw" -bcc qemu_mirror >"$run/initialize-zdb-check.log" 2>&1
rg -q 'No leaks' "$run/initialize-zdb-check.log"
sha256sum -c "$run/initialize-before.sha256"
echo "PASS: saved native GUI initialization state ($scenario), exported pool and offline zdb integrity; inspect screenshots separately: $run"
