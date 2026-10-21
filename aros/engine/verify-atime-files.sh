#!/usr/bin/env bash
# Independent offline inspection of real DOS-read timestamp fixtures.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(realpath -- "${1:?private admission-service-run required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == admission-service-run.* ]]
command -v fuser >/dev/null
if fuser "$run/disk0.raw" "$run/disk1.raw" >"$run/atime-open-users.log" 2>&1;then
    echo 'FAIL: disk images still have users' >&2;exit 1
fi
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
export LC_ALL=C TZ=UTC
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/atime-before.sha256"
zdb=("$repo/build-control/zdb" -e -p "${AROS_VERIFY_MEMBER0:-$run/disk0.raw}" -p "${AROS_VERIFY_MEMBER1:-$run/disk1.raw}")
dataset=qemu_mirror/delegated-root/moved
for mode in enabled disabled;do
    name="atime-$mode"
    "${zdb[@]}" -Ov "$dataset" "$name" >"$run/$name-zdb.log" 2>&1
    access=$(awk '$1=="atime" {$1="";print substr($0,2)}' "$run/$name-zdb.log")
    modify=$(awk '$1=="mtime" {$1="";print substr($0,2)}' "$run/$name-zdb.log")
    create=$(awk '$1=="crtime" {$1="";print substr($0,2)}' "$run/$name-zdb.log")
    [[ -n $access && -n $modify && -n $create ]]
    access_seconds=$(date -u -d "$access" +%s)
    modify_seconds=$(date -u -d "$modify" +%s)
    create_seconds=$(date -u -d "$create" +%s)
    if [[ $mode == enabled ]];then
        (( access_seconds > modify_seconds ))
    else
        (( access_seconds == create_seconds && access_seconds <= modify_seconds ))
    fi
    printf '%s atime=%s mtime=%s crtime=%s\n' "$name" "$access_seconds" "$modify_seconds" "$create_seconds"
done
sha256sum -c "$run/atime-before.sha256"
echo "PASS: persisted real DOS atime enabled/disabled at second precision: $run"
