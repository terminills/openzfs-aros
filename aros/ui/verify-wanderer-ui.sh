#!/usr/bin/env bash
# Offline disk verification is separate from inspecting the UI screenshots.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(realpath -- "${1:?private native GUI evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == native-gui-run.* ]]
[[ $(<"$run/scenario.txt") == wanderer ]]
pid=$(<"$run/qemu.pid")
[[ $pid =~ ^[0-9]+$ ]]
if kill -0 "$pid" 2>/dev/null;then echo 'Guest is still alive; finish/export/quit first' >&2;exit 1;fi
command -v fuser >/dev/null
if fuser "$run/disk0.raw" "$run/disk1.raw" >"$run/wanderer-open-users.log" 2>&1;then
    echo 'Disk images have active users' >&2;exit 1
fi
rg -a -q '^Wanderer fixture READY volume=OZBROWSE uid=77 gid=88' "$run/serial.log"
rg -a -q '^Wanderer fixture UPDATED expected=After.txt,New.txt' "$run/serial.log"
rg -a -q '^RESULT Wanderer fixture DOS operations PASS' "$run/serial.log"
if rg -a -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
mkdir "$run/wanderer-extracted"
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/wanderer-before.sha256"
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
zdb=("$repo/build-control/zdb" -e -p "$run/disk0.raw" -p "$run/disk1.raw")
"${zdb[@]}" -bcc qemu_mirror >"$run/wanderer-zdb-check.log" 2>&1
rg -q 'No leaks' "$run/wanderer-zdb-check.log"
dataset=qemu_mirror/delegated-root/wanderer
for name in ReadMe.txt Documents/After.txt Documents/New.txt;do
    target="$run/wanderer-extracted/${name##*/}"
    "${zdb[@]}" -r "$dataset" "$name" "$target" >"$target.zdb.log" 2>&1
done
cmp "$run/wanderer-extracted/ReadMe.txt" <(printf 'Real upstream OpenZFS mounted through the AROS DOS handler.\n')
cmp "$run/wanderer-extracted/After.txt" <(printf 'Visible before the live update.\n')
cmp "$run/wanderer-extracted/New.txt" <(printf 'Created while the drawer was open.\n')
"${zdb[@]}" -dddd "$dataset" >"$run/wanderer-zdb-objects.log" 2>&1
rg -q '^[[:space:]]+uid[[:space:]]+77$' "$run/wanderer-zdb-objects.log"
rg -q '^[[:space:]]+gid[[:space:]]+88$' "$run/wanderer-zdb-objects.log"
sha256sum -c "$run/wanderer-before.sha256"
echo "PASS: exported Wanderer fixture, exact file bytes and zdb integrity. Visual verification is separate: $run"
