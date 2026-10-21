#!/usr/bin/env bash
# Read-only host acceptance of stopped private AROS admission fixtures.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(realpath -e -- "${1:?private admission evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == admission-service-run.* ]]
partitioned=${2:-0}
[[ $partitioned == 0 || $partitioned == 1 ]]
suite=$(<"$run/suite.txt")
case $suite in admission|initialize|exec|device|create|add|remove-cancel|split) ;; *) exit 2;; esac
command -v fuser >/dev/null
if [[ $partitioned == 1 ]];then
    command -v sfdisk >/dev/null
    command -v jq >/dev/null
    command -v python3 >/dev/null
fi
if fuser "$run/disk0.raw" "$run/disk1.raw" >/dev/null 2>&1;then
    echo 'Refusing offline verification while disk images have users.' >&2;exit 2
fi
out=$(mktemp -d "$run/host-verification.XXXXXXXX")
echo "Host evidence: $out"
cp "$0" "$out/"
cp aros/engine/verify-{compressed-files,atime-files,native-streams,recovered-member}.sh "$out/"
cp aros/engine/pool-partition-fixture.py "$out/"
git rev-parse HEAD >"$out/source-head.txt"
git diff --binary >"$out/source.patch"
sha256sum build-control/zdb build-control/zstream \
    build-control/.libs/zdb build-control/.libs/zstream >"$out/tools.sha256"
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$out/source-before.sha256"
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
members=("$run/disk0.raw" "$run/disk1.raw")
if [[ $partitioned == 1 ]];then
    for disk in 0 1;do
        sfdisk --json "${members[disk]}" >"$out/layout$disk.json"
        # This extractor is deliberately limited to the known test fixture.
        # Validate actual on-disk extents before using its fixed geometry.
        jq -e '.partitiontable | .unit == "sectors" and .sectorsize == 512 and
            (.partitions | length == 1) and .partitions[0].start == 4097 and
            .partitions[0].size == 524288' "$out/layout$disk.json" >/dev/null
        python3 aros/engine/pool-partition-fixture.py extract "${members[disk]}" "$out/member$disk.raw"
        members[disk]="$out/member$disk.raw"
        build-control/zdb -l "${members[disk]}" >"$out/labels$disk.log" 2>&1
        rg -q "name: 'qemu_mirror'" "$out/labels$disk.log"
    done
fi
export AROS_VERIFY_MEMBER0="${members[0]}" AROS_VERIFY_MEMBER1="${members[1]}"
sha256sum "${members[@]}" >"$out/members-before.sha256"
build-control/zdb -e -p "${members[0]}" -p "${members[1]}" -bcc qemu_mirror >"$out/linux-zdb.log" 2>&1
rg -q 'No leaks' "$out/linux-zdb.log"
if [[ $suite == device ]];then
    bash aros/engine/verify-recovered-member.sh "$run"
elif [[ $suite != exec ]];then
    bash aros/engine/verify-compressed-files.sh "$run"
    bash aros/engine/verify-atime-files.sh "$run"
    if [[ $suite == admission ]];then bash aros/engine/verify-native-streams.sh "$run" advanced;fi
fi
sha256sum -c "$out/members-before.sha256"
sha256sum -c "$out/source-before.sha256"
echo "PASS: independent offline member verification: $out"
