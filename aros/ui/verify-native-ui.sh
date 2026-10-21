#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(realpath -- "${1:?native GUI evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == native-gui-run.* ]]
pid=$(<"$run/qemu.pid")
[[ $pid =~ ^[0-9]+$ ]]
if kill -0 "$pid" 2>/dev/null;then echo 'Guest PID is still alive; close/export/quit before verification' >&2;exit 1;fi
perl -0777 -pe 's/[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| \[security.library\] secTaskNotify: [^\r\n]*\r?\n//g' \
    "$run/serial.log" >"$run/checked.log"
if rg -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log" "$run/checked.log";then
    echo 'FAIL: native GUI failure/assertion marker' >&2;exit 1
fi
rg -q '^RESULT authenticated native GUI PASS' "$run/checked.log"
rg -q '^GUI fixture launching real manager as MuFS UID77 GID88' "$run/checked.log"
rg -q '^dataset.create: authenticated worker started' "$run/checked.log"
rg -q '^snapshot.snapshot: authenticated worker started' "$run/checked.log"
scenario=properties
if [[ -f "$run/scenario.txt" ]];then scenario=$(<"$run/scenario.txt");fi
if [[ $scenario == compression || $scenario == recordsize ]];then
    [[ $(rg -c '^dataset.set: authenticated worker started' "$run/checked.log") == 2 ]]
    [[ $(rg -c '^dataset.inherit: authenticated worker started' "$run/checked.log") == 1 ]]
    [[ $(rg -c '^Authenticated ticket [0-9]+:[0-9]+: transport errno=0, release errno=0' "$run/checked.log") == 5 ]]
    rg -q "^GUI $scenario inherited value/source PASS" "$run/checked.log"
    expected=lz4
    if [[ $scenario == recordsize ]];then expected=65536;fi
    rg -q "property.*$scenario.*$expected" "$run/checked.log"
    rg -q '^Upstream property lookup errno=0' "$run/checked.log"
    if rg -q 'parent=.* used=0 available=0 total=0 progress=0/1000' "$run/checked.log";then
        echo 'FAIL: property row contains invented space/progress counters' >&2;exit 1
    fi
elif [[ $scenario == space ]];then
    [[ $(rg -c '^dataset.set: authenticated worker started' "$run/checked.log") == 1 ]]
    [[ $(rg -c '^Authenticated ticket [0-9]+:[0-9]+: transport errno=0, release errno=0' "$run/checked.log") == 3 ]]
    rg -q '^GUI quota value/source PASS' "$run/checked.log"
    rg -q 'property.*quota.*16777216' "$run/checked.log"
    rg -q '^Upstream property lookup errno=0' "$run/checked.log"
    if rg -q 'parent=.* used=0 available=0 total=0 progress=0/1000' "$run/checked.log";then
        echo 'FAIL: property row contains invented space/progress counters' >&2;exit 1
    fi
elif [[ $scenario == bookmark ]];then
    rg -q '^snapshot.bookmark: authenticated worker started' "$run/checked.log"
    [[ $(rg -c '^Authenticated ticket [0-9]+:[0-9]+: transport errno=0, release errno=0' "$run/checked.log") == 3 ]]
    rg -q '^GUI bookmark identity PASS' "$run/checked.log"
    rg -q 'qemu_mirror/delegated-root/gui-created#ui' "$run/checked.log"
    rg -q 'Upstream bookmark enumeration errno=0' "$run/checked.log"
    # Saved response must not invent capacity/progress values for a bookmark.
    if rg -q 'parent=.* used=0 available=0 total=0 progress=0/1000' "$run/checked.log";then
        echo 'FAIL: metadata row contains invented space/progress counters' >&2;exit 1
    fi
elif [[ $scenario == promotion ]];then
    rg -q '^snapshot.clone: authenticated worker started' "$run/checked.log"
    rg -q '^snapshot.promote: authenticated worker started' "$run/checked.log"
    [[ $(rg -c '^Authenticated ticket [0-9]+:[0-9]+: transport errno=0, release errno=0' "$run/checked.log") == 4 ]]
    rg -q '^Authenticated clone promotion returned errno=0' "$run/checked.log"
    rg -q '^GUI promoted origin PASS' "$run/checked.log"
else
[[ $scenario == properties ]]
[[ $(rg -c '^dataset.set: authenticated worker started' "$run/checked.log") == 2 ]]
rg -q '^dataset.inherit: authenticated worker started' "$run/checked.log"
[[ $(rg -c '^Authenticated ticket [0-9]+:[0-9]+: transport errno=0, release errno=0' "$run/checked.log") == 5 ]]
rg -q '^Authenticated user property inherit returned errno=0' "$run/checked.log"
rg -q '^GUI inherited property value/source PASS' "$run/checked.log"
fi
rg -q '^Upstream pool export returned errno=0' "$run/checked.log"
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/zdb-before.sha256"
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
build-control/zdb -e -p "$run/disk0.raw" -p "$run/disk1.raw" -bcc qemu_mirror >"$run/zdb-check.log" 2>&1
rg -q 'No leaks' "$run/zdb-check.log"
for item in owner snapshot;do
    dataset=qemu_mirror/delegated-root/gui-created
    if [[ $item == snapshot ]];then
        if [[ $scenario == promotion ]];then dataset=qemu_mirror/delegated-root/gui-promoted;fi
        dataset+=@ui
    fi
    build-control/zdb -e -p "$run/disk0.raw" -p "$run/disk1.raw" -dddd "$dataset" >"$run/zdb-$item.log" 2>&1
    rg -q "^Dataset $dataset " "$run/zdb-$item.log"
    rg -q '^[[:space:]]+uid[[:space:]]+77$' "$run/zdb-$item.log"
    rg -q '^[[:space:]]+gid[[:space:]]+88$' "$run/zdb-$item.log"
done
sha256sum -c "$run/zdb-before.sha256"
echo "PASS: GUI $scenario workflow, real MuFS UID77/GID88, exported disk objects and zdb integrity: $run"
