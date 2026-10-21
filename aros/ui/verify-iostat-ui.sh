#!/usr/bin/env bash
# Saved native GUI counters plus exported-image integrity, never live images.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
run=$(realpath -- "${1:?private native GUI evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == native-gui-run.* ]]
[[ $(<"$run/scenario.txt") == export ]]
pid=$(<"$run/qemu.pid")
[[ $pid =~ ^[0-9]+$ ]]
if kill -0 "$pid" 2>/dev/null;then echo 'Guest is still alive' >&2;exit 1;fi
command -v fuser >/dev/null
if fuser "$run/disk0.raw" "$run/disk1.raw" >"$run/iostat-open-users.log" 2>&1;then
    echo 'Disk images have active users' >&2;exit 1
fi
rg -a -q '^pool.iostat: OK' "$run/serial.log"
rg -a -q '^Upstream cumulative I/O counters errno=0, load errno=0.' "$run/serial.log"
rg -a -q '^GUI authenticated export absent pool PASS' "$run/serial.log"
rg -a -q '^RESULT authenticated native GUI PASS' "$run/serial.log"
if rg -a -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
for member in 0 1;do
    for metric in bytes ops;do
        rg -a -q "^io-$metric \\| /AROS/ata.device/$member(/partition/0)? \\| ONLINE \\| type=disk role=data guid=[0-9]+ counters=cumulative-since-vdev-load" "$run/serial.log"
    done
done
if [[ ${AROS_VERIFY_IO_RATES:-0} == 1 ]];then
    [[ $(rg -a -c '^io-(bytes|ops) .* load-guid=[1-9][0-9]*' "$run/serial.log") == 8 ]]
    for unit in bytes operations;do
        [[ $(rg -a -c "^  interval read=[0-9]+\\.[0-9]{2} write=[0-9]+\\.[0-9]{2} $unit/s; sample=[1-9][0-9]* ns" "$run/serial.log") == 4 ]]
    done
fi
for unit in bytes operations;do
    [[ $(rg -a -c "^  parent=mirror-.* cumulative read=[1-9][0-9]* write=[1-9][0-9]* $unit; elapsed=[1-9][0-9]* ns" "$run/serial.log") == 2 ]]
done
verify=$(mktemp -d "$run/iostat-offline.XXXXXXXX")
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$verify/before.sha256"
members=()
for member in 0 1;do
    if rg -a -q "^io-bytes \\| /AROS/ata.device/$member/partition/0 \\|" "$run/serial.log";then
        sfdisk --json "$run/disk$member.raw" >"$verify/disk$member-gpt.json"
        read -r start count < <(jq -er '.partitiontable | select(.label == "gpt" and .sectorsize == 512 and (.partitions|length) == 1) | .partitions[0] | select(.type == "6A898CC3-1DD2-11B2-99A6-080020736631") | "\(.start) \(.size)"' "$verify/disk$member-gpt.json")
        [[ $start =~ ^[0-9]+$ && $count =~ ^[0-9]+$ ]]
        (( start > 0 && count > 0 && (start+count)*512 <= $(stat -c %s "$run/disk$member.raw") ))
        dd if="$run/disk$member.raw" of="$verify/member$member.raw" bs=512 skip="$start" count="$count" conv=excl status=none
        members+=(-p "$verify/member$member.raw")
    else
        members+=(-p "$run/disk$member.raw")
    fi
done
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
"$repo/build-control/zdb" -e "${members[@]}" -bcc qemu_mirror >"$verify/zdb.log" 2>&1
rg -q 'No leaks' "$verify/zdb.log"
sha256sum -c "$verify/before.sha256"
echo "PASS: saved native GUI counters/rates, exported pool and offline zdb integrity; inspect screenshots separately: $verify"
