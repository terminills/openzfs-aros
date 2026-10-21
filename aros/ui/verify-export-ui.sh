#!/usr/bin/env bash
# Native GUI export evidence, followed by independent read-only disk inspection.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
run=$(realpath -- "${1:?private native GUI evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == native-gui-run.* ]]
[[ $(<"$run/scenario.txt") == export ]]
pid=$(<"$run/qemu.pid")
[[ $pid =~ ^[0-9]+$ ]]
if kill -0 "$pid" 2>/dev/null;then echo 'Guest is still alive' >&2;exit 1;fi
command -v fuser >/dev/null
if fuser "$run/disk0.raw" "$run/disk1.raw" >"$run/export-open-users.log" 2>&1;then
    echo 'Disk images have active users' >&2;exit 1
fi
rg -a -q '^pool.export: authenticated worker started' "$run/serial.log"
rg -a -q '^Authenticated pool export returned errno=0' "$run/serial.log"
rg -a -q '^GUI authenticated export absent pool PASS' "$run/serial.log"
rg -a -q '^RESULT authenticated native GUI PASS' "$run/serial.log"
if rg -a -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/export-before.sha256"
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
"$repo/build-control/zdb" -e -p "$run/disk0.raw" -p "$run/disk1.raw" -bcc qemu_mirror >"$run/export-zdb-check.log" 2>&1
rg -q 'No leaks' "$run/export-zdb-check.log"
sha256sum -c "$run/export-before.sha256"
echo "PASS: native manager authenticated export and offline zdb integrity; inspect screenshots separately: $run"
