#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(realpath -- "${1:?private admission evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == admission-service-run.* ]]
command -v fuser >/dev/null
if [[ -S "$run/write.monitor" || -S "$run/verify.monitor" ]] ||
    fuser "$run/disk0.raw" "$run/disk1.raw" >"$run/compression-open-users.log" 2>&1;then
    echo 'Refusing compression verification while guest/storage users remain' >&2;exit 1
fi
for phase in write verify;do
    rg -q '^RESULT standalone admission PASS' "$run/$phase-checked.log"
done
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/compression-before.sha256"
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
zdb=(build-control/zdb -e -p "${AROS_VERIFY_MEMBER0:-$run/disk0.raw}" -p "${AROS_VERIFY_MEMBER1:-$run/disk1.raw}")
dataset=qemu_mirror/delegated-root/moved
"${zdb[@]}" -dddd "$dataset" >"$run/compression-objects.log" 2>&1
for algo in lz4 gzip-6 zstd-3;do
    object=$(awk -v name="compressed-$algo" '$1==name && $2=="=" {print $3}' "$run/compression-objects.log")
    [[ $object =~ ^[0-9]+$ ]]
    output="$run/compression-$algo.log"
    "${zdb[@]}" -dddddd -bbbbbb -Z "$dataset" "$object" >"$output" 2>&1
    rg -q "^[[:space:]]+path[[:space:]]+/compressed-$algo$" "$output"
    rg -q '^[[:space:]]+size[[:space:]]+524288$' "$output"
    expected=$algo
    if [[ $algo == zstd-3 ]];then expected=zstd;fi
    [[ $(rg -c '\[L0 ZFS plain file\]' "$output") == 4 ]]
    [[ $(rg -c "\\[L0 ZFS plain file\\].* $expected .*size=20000L/1000P" "$output") == 4 ]]
    if [[ $algo == zstd-3 ]];then
        [[ $(rg -c '\[L0 ZFS plain file\].*ZSTD:.*:level=3:NORMAL' "$output") == 4 ]]
    fi
done
for spec in 32K:8000:64 128K:20000:16 1M:100000:2;do
    IFS=: read -r size hex blocks <<<"$spec"
    object=$(awk -v name="record-$size" '$1==name && $2=="=" {print $3}' "$run/compression-objects.log")
    [[ $object =~ ^[0-9]+$ ]]
    output="$run/recordsize-$size.log"
    "${zdb[@]}" -dddddd -bbbbbb -Z "$dataset" "$object" >"$output" 2>&1
    rg -q "^[[:space:]]+path[[:space:]]+/record-$size$" "$output"
    rg -q '^[[:space:]]+size[[:space:]]+2097152$' "$output"
    [[ $(rg -c '\[L0 ZFS plain file\]' "$output") == "$blocks" ]]
    [[ $(rg -c "\\[L0 ZFS plain file\\].*size=${hex}L/" "$output") == "$blocks" ]]
done
sha256sum -c "$run/compression-before.sha256"
echo "PASS: independent data-block compression, ZSTD level and record sizes: $run"
