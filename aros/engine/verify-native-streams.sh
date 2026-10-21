#!/usr/bin/env bash
# Extract AROS-produced streams only after private guests have stopped.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(realpath -- "${1:?private admission evidence directory required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == admission-service-run.* ]]
command -v fuser >/dev/null
if [[ -S "$run/write.monitor" || -S "$run/verify.monitor" ]] ||
    fuser "$run/disk0.raw" "$run/disk1.raw" >"$run/streams-open-users.log" 2>&1;then
    echo 'Refusing inspection while guest/storage users remain' >&2;exit 1
fi
rg -a -q '^Native send/receive WRITE full\+incremental errno=0' "$run/write-serial.log"
rg -a -q '^Native send/receive VERIFY cold full\+incremental errno=0' "$run/verify-serial.log"
for phase in write verify;do
    rg -a -q '^RESULT standalone admission PASS' "$run/$phase-serial.log"
done
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/streams-before.sha256"
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
streams=(full incremental)
if [[ ${2:-} == advanced ]];then
    rg -a -q '^Native send/receive WRITE advanced representations errno=0' "$run/write-serial.log"
    rg -a -q '^Native send/receive VERIFY cold advanced representations errno=0' "$run/verify-serial.log"
    rg -a -q '^Native send/receive WRITE bookmark after snapshot deletion errno=0' "$run/write-serial.log"
    rg -a -q '^Native send/receive VERIFY cold bookmark after snapshot deletion errno=0' "$run/verify-serial.log"
    rg -a -q '^RESULT native replication resume PASS' "$run/replication-serial.log"
    streams+=(plain advanced bookmark api-bookmark resumed api-resumed cold-resumed
        inc-resume-base inc-resume-delta inc-snapshot-rest inc-bookmark-rest)
elif [[ -n ${2:-} ]];then exit 2;fi
for stream in "${streams[@]}";do
    dataset=qemu_mirror/stream-artifacts
    if [[ $stream == api-bookmark || $stream == api-resumed ]];then dataset=qemu_mirror/direct-stream-policy;fi
    build-control/zdb -e -p "${AROS_VERIFY_MEMBER0:-$run/disk0.raw}" -p "${AROS_VERIFY_MEMBER1:-$run/disk1.raw}" \
        -r "$dataset" "$stream.zfs" "$run/$stream.zfs" >"$run/$stream-extract.log" 2>&1
    build-control/zstream dump "$run/$stream.zfs" >"$run/$stream-dump.log" 2>"$run/$stream-dump.err"
    # dump can return zero after a checksum mismatch: status alone is NOT proof.
    [[ ! -s "$run/$stream-dump.err" ]]
    if rg -i -q 'incorrect checksum|expected checksum|invalid|aborting' "$run/$stream-dump.log";then exit 1;fi
    rg -q 'Total DRR_BEGIN records = 1 ' "$run/$stream-dump.log"
    rg -q 'Total DRR_END records = 1 ' "$run/$stream-dump.log"
    rg -q 'Total DRR_WRITE(_EMBEDDED)? records = [1-9][0-9]* ' "$run/$stream-dump.log"
    reported=$(awk '/Total stream length =/{print $5}' "$run/$stream-dump.log")
    [[ $reported == "$(stat -c %s "$run/$stream.zfs")" ]]
done
if [[ ${2:-} == advanced ]];then
    for stream in plain advanced;do
        rg -q 'toname = qemu_mirror/stream-advanced@base$' "$run/$stream-dump.log"
        [[ $(awk '/fromguid =/{print $3}' "$run/$stream-dump.log") == 0 ]]
    done
    [[ $(awk '/toguid =/{print $3}' "$run/plain-dump.log") == "$(awk '/toguid =/{print $3}' "$run/advanced-dump.log")" ]]
    rg -q 'Total DRR_WRITE_EMBEDDED records = 0 ' "$run/plain-dump.log"
    rg -q 'Total DRR_WRITE_EMBEDDED records = [1-9][0-9]* ' "$run/advanced-dump.log"
    [[ $(stat -c %s "$run/advanced.zfs") -lt $(stat -c %s "$run/plain.zfs") ]]
    advanced_guid=$(awk '/toguid =/{print $3}' "$run/advanced-dump.log")
    for stream in bookmark api-bookmark;do
        [[ $(awk '/fromguid =/{print $3}' "$run/$stream-dump.log") == "$advanced_guid" ]]
        [[ $(awk '/toguid =/{print $3}' "$run/$stream-dump.log") != "$advanced_guid" ]]
        rg -q 'toname = qemu_mirror/stream-advanced@second$' "$run/$stream-dump.log"
    done
    [[ $(awk '/toguid =/{print $3}' "$run/bookmark-dump.log") == "$(awk '/toguid =/{print $3}' "$run/api-bookmark-dump.log")" ]]
    [[ $(awk '/toguid =/{print $3}' "$run/resumed-dump.log") == "$(awk '/toguid =/{print $3}' "$run/bookmark-dump.log")" ]]
    [[ $(awk '/fromguid =/{print $3}' "$run/resumed-dump.log") == 0 ]]
    [[ $(awk '/toguid =/{print $3}' "$run/api-resumed-dump.log") == "$(awk '/toguid =/{print $3}' "$run/resumed-dump.log")" ]]
    [[ $(awk '/fromguid =/{print $3}' "$run/api-resumed-dump.log") == 0 ]]
    [[ $(awk '/toguid =/{print $3}' "$run/cold-resumed-dump.log") == "$(awk '/toguid =/{print $3}' "$run/resumed-dump.log")" ]]
    [[ $(awk '/fromguid =/{print $3}' "$run/cold-resumed-dump.log") == 0 ]]
    build-control/zstream dump -v "$run/resumed.zfs" >"$run/resumed-verbose.log" 2>"$run/resumed-verbose.err"
    [[ ! -s "$run/resumed-verbose.err" ]]
    # Fixed fixture: source object 2 uses 1MiB records but the ordinary wire
    # stream splits them. Saved position is 262144, not the block's start.
    rg -q '^WRITE object = 2 .* offset = 262144 ' "$run/resumed-verbose.log"
    if rg -q '^WRITE object = 2 .* offset = (0|131072) ' "$run/resumed-verbose.log";then exit 1;fi
    sha256sum "$run/plain.zfs" "$run/advanced.zfs" "$run/bookmark.zfs" "$run/api-bookmark.zfs" >"$run/advanced-streams.sha256"
    sha256sum "$run/resumed.zfs" >>"$run/advanced-streams.sha256"
    sha256sum "$run/api-resumed.zfs" >>"$run/advanced-streams.sha256"
    sha256sum "$run/cold-resumed.zfs" >>"$run/advanced-streams.sha256"
    inc_base_guid=$(awk '/toguid =/{print $3}' "$run/inc-resume-base-dump.log")
    inc_next_guid=$(awk '/toguid =/{print $3}' "$run/inc-resume-delta-dump.log")
    [[ -n $inc_base_guid && -n $inc_next_guid && $inc_base_guid != "$inc_next_guid" ]]
    for stream in inc-resume-delta inc-snapshot-rest inc-bookmark-rest;do
        [[ $(awk '/fromguid =/{print $3}' "$run/$stream-dump.log") == "$inc_base_guid" ]]
        [[ $(awk '/toguid =/{print $3}' "$run/$stream-dump.log") == "$inc_next_guid" ]]
        rg -q 'toname = qemu_mirror/inc-resume-source@next$' "$run/$stream-dump.log"
    done
    sha256sum "$run/inc-resume-base.zfs" "$run/inc-resume-delta.zfs" \
        "$run/inc-snapshot-rest.zfs" "$run/inc-bookmark-rest.zfs" >>"$run/advanced-streams.sha256"
fi
[[ $(awk '/fromguid =/{print $3}' "$run/full-dump.log") == 0 ]]
base_guid=$(awk '/toguid =/{print $3}' "$run/full-dump.log")
[[ -n $base_guid && $base_guid != 0 ]]
[[ $(awk '/fromguid =/{print $3}' "$run/incremental-dump.log") == "$base_guid" ]]
[[ $(awk '/toguid =/{print $3}' "$run/incremental-dump.log") != "$base_guid" ]]
rg -q 'toname = qemu_mirror/stream-source@base$' "$run/full-dump.log"
rg -q 'toname = qemu_mirror/stream-source@second$' "$run/incremental-dump.log"
sha256sum -c "$run/streams-before.sha256"
sha256sum "$run/full.zfs" "$run/incremental.zfs" >"$run/streams.sha256"
echo "PASS: upstream host parser verifies complete AROS full/incremental streams, checksums and GUID chain: $run"
