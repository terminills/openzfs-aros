#!/usr/bin/env bash
# Private hosted pool creation only; never operates an existing AROS instance.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
# Bash reads long-running scripts incrementally. Execute an immutable private
# snapshot so later development edits cannot change an active test's tail.
if [[ ${AROS_LARGE_POOL_RUNNER_SNAPSHOT:-} != "$0" ]];then
    snapshot=$(mktemp -d "$repo/build-aros/large-pool-runner.XXXXXXXX")
    cp "$0" "$snapshot/run.sh"
    exec env AROS_LARGE_POOL_RUNNER_SNAPSHOT="$snapshot/run.sh" bash "$snapshot/run.sh"
fi
binary=$(realpath -e "${AROS_TEST_POOL_BINARY:?explicit pool test binary required}")
dos64=$(realpath -e "${AROS_TEST_DOS64_LIBRARY:?qualified DOS64 library required}")
reader=${AROS_TEST_POOL_READER:-}
array50=${AROS_TEST_ARRAY50:-0}
[[ $array50 == 0 || $array50 == 1 ]]
gui=${AROS_TEST_ARRAY50_GUI:-0}
[[ $gui == 0 || ( $gui == 1 && $array50 == 1 ) ]]
diskinfo=${AROS_TEST_DISKINFO:-}
diskinfo_check=${AROS_TEST_DISKINFO_CHECK:-}
if [[ -n $diskinfo ]];then
    [[ $gui == 1 ]]
    diskinfo=$(realpath -e "$diskinfo")
fi
if [[ -n $diskinfo_check ]];then
    [[ -n $diskinfo && $gui == 1 ]]
    diskinfo_check=$(realpath -e "$diskinfo_check")
fi
topologies=(mirror raidz1 raidz2 raidz3)
member_bytes=8589934592
if [[ $array50 == 1 ]];then
    [[ -n $reader ]]
    topologies=(raidz1);member_bytes=14293651161088
fi
if [[ -n $reader ]];then reader=$(realpath -e "$reader");fi
run=$(mktemp -d "$repo/build-aros/hosted-large-pools.XXXXXXXX")
echo "Evidence: $run"
for dir in boot C Classes Devs Fonts L Libs Locale Prefs S System;do
    cp -a --reflink=auto "build-aros/hosted/$dir" "$run/$dir"
done
cp build-aros/hosted/AROS.boot "$run/AROS.boot"
if [[ -n $diskinfo ]];then cp "$diskinfo" "$run/System/Wanderer/Tools/DiskInfo";fi
cp "$binary" "$run/C/OpenZFSPoolTest"
if [[ -n $reader ]];then
    cp "$reader" "$run/C/OpenZFSRAIDReadTest"
    cp aros/engine/Startup-Hosted-RAID-Large-Read "$run/Startup-Read"
    if [[ $gui == 1 ]];then cp aros/engine/Startup-Hosted-Array50-GUI "$run/Startup-Read";fi
    if [[ -n $diskinfo_check ]];then
        cp "$diskinfo_check" "$run/C/OpenZFSDiskInfoTest"
        sed -i '/Stack 1048576/a C:OpenZFSDiskInfoTest >SYS:diskinfo-capacity.log' "$run/Startup-Read"
    fi
    cp aros/engine/raid-read-test.c "$run/"
fi
cp "$dos64" "$run/Libs/dos64.library"
cp aros/engine/Startup-Hosted-RAID-Large "$run/S/Startup-Sequence"
if [[ $array50 == 1 ]];then
    sed -i '/OpenZFSPoolTest \(mirror\|raidz2\|raidz3\)/d; /SetEnv OPENZFS_TEST_LARGE_FILES/a SetEnv OPENZFS_TEST_ARRAY50 1' "$run/S/Startup-Sequence"
    sed -i '/Stack 1048576/a SetEnv OPENZFS_TEST_ARRAY50 1' "$run/Startup-Read"
    cp aros/engine/sparse-digest.py "$run/"
fi
cp "$0" aros/engine/pool-test.c "$run/"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
sed "s|^logfile .*|logfile $run/serial.log|" aros/engine/hosted.conf >"$run/hosted.conf"
sha256sum "$run/C/OpenZFSPoolTest" "$run/Libs/dos64.library" \
    "$run/Libs/posixc.library" "$run/Libs/dos.library" \
    "$run/boot/linux/L/emul-handler" "$repo/build-control/zdb" >"$run/inputs.sha256"
if [[ -n $reader ]];then sha256sum "$run/C/OpenZFSRAIDReadTest" >>"$run/inputs.sha256";fi
if [[ $array50 == 1 ]];then sha256sum "$run/sparse-digest.py" >>"$run/inputs.sha256";fi
if [[ -n $diskinfo ]];then sha256sum "$run/System/Wanderer/Tools/DiskInfo" >>"$run/inputs.sha256";fi
if [[ -n $diskinfo_check ]];then sha256sum "$run/C/OpenZFSDiskInfoTest" >>"$run/inputs.sha256";fi
xpid=;guest=
cleanup() {
    if [[ -n $guest ]];then kill -TERM -- "-$guest" 2>/dev/null || true;wait "$guest" || true;guest=;fi
    if [[ -n $xpid ]];then kill "$xpid" 2>/dev/null || true;wait "$xpid" || true;xpid=;fi
}
trap cleanup EXIT
start_guest() {
local phase=$1
sed "s|^logfile .*|logfile $run/$phase-serial.log|" "$run/hosted.conf" >"$run/$phase.conf"
Xvfb -displayfd 3 -screen 0 1280x960x24 -nolisten tcp 3>"$run/$phase-display.txt" >"$run/$phase-xvfb.log" 2>&1 &
xpid=$!
for attempt in {1..50};do [[ -s "$run/$phase-display.txt" ]] && break;sleep .1;done
[[ -s "$run/$phase-display.txt" ]]
display=$(<"$run/$phase-display.txt");[[ $display =~ ^[0-9]+$ ]]
cd "$run"
DISPLAY=":$display" setsid ./boot/linux/AROSBootstrap -c "$run/$phase.conf" >"$run/$phase-bootstrap.log" 2>&1 &
guest=$!;printf '%s\n' "$guest" >"$run/$phase-guest.pid"
}
start_guest create
deadline=$((SECONDS+600))
while [[ ! -f "$run/raid-created.marker" ]];do
    if ! kill -0 "$guest" 2>/dev/null || ((SECONDS>=deadline));then
        echo 'FAIL: private large-pool test exit/deadline' >&2;exit 1
    fi
    sleep 1
done
# All test programs must have exported and finished before offline inspection.
for topology in "${topologies[@]}";do
    rg -q "^RESULT native $topology data roundtrip PASS$" "$run/raid-$topology-create.log"
done
cleanup
all_members=()
for topology in "${topologies[@]}";do
    log="$run/raid-$topology-create.log"
    [[ ! -s "$run/pool-$topology-errors.log" ]]
    dir=$(sed -n 's/^Disposable native pool directory: //p' "$log")
    pool=$(sed -n "s/^Creating upstream $topology //p" "$log")
    [[ $dir =~ ^/SYS/OpenZFS-test-[0-9a-f]{8}$ && $pool =~ ^aros_${topology}_[0-9a-f]{8}$ ]]
    members=("$run/${dir#/SYS/}"/disk*)
    expected=2
    case "$topology" in raidz1) expected=3;; raidz2) expected=4;; raidz3) expected=5;; esac
    if [[ $array50 == 1 ]];then expected=5;fi
    [[ ${#members[@]} == "$expected" ]]
    for member in "${members[@]}";do [[ -f $member && $(stat -c %s "$member") == "$member_bytes" ]];done
    all_members+=("${members[@]}")
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        "$repo/build-control/zdb" -e -p "$run/${dir#/SYS/}" -bcc "$pool" >"$run/$topology-zdb.log" 2>&1
    rg -q 'No leaks' "$run/$topology-zdb.log"
    printf '%s member-bytes=%s members=%s native roundtrip and independent zdb PASS\n' "$topology" "$member_bytes" "$expected" | tee -a "$run/results.txt"
done
if [[ -n $reader ]];then
    # Both processes are stopped while hashing. The 50 TB-class fixture
    # hashes size, hole layout and allocated bytes, not a logical SHA256.
    echo "Hashing members before independent readonly boot (structural-sparse=$array50)"
    if [[ $array50 == 1 ]];then
        python3 "$run/sparse-digest.py" "${all_members[@]}" >"$run/before-read.sparse"
    else sha256sum "${all_members[@]}" >"$run/before-read.sha256";fi
    cp "$run/Startup-Read" "$run/S/Startup-Sequence"
    start_guest read
    deadline=$((SECONDS+600))
    while [[ ! -f "$run/raid-read.marker" ]];do
        if [[ -n $diskinfo_check && -f "$run/diskinfo-capacity.log" ]] && rg -q '^FAIL ' "$run/diskinfo-capacity.log";then
            echo 'FAIL: native DiskInfo capacity regression' >&2;exit 1
        fi
        if [[ $gui == 1 && -f "$run/raid-cold-read.log" ]];then
            if rg -q '^ARRAY50 GUI detached FAIL' "$run/raid-cold-read.log";then exit 1;fi
            if rg -q '^ARRAY50 GUI detached PASS errno=0 endpoint-retained=1$' "$run/raid-cold-read.log";then break;fi
        fi
        if ! kill -0 "$guest" 2>/dev/null || ((SECONDS>=deadline));then
            echo 'FAIL: private cold reader exit/deadline' >&2;exit 1
        fi
        sleep 1
    done
    if [[ $gui == 1 ]];then
        rg -q '^ARRAY50 GUI detached PASS errno=0 endpoint-retained=1$' "$run/raid-cold-read.log"
    else rg -q '^RESULT cold RAID DOS matrix PASS$' "$run/raid-cold-read.log";fi
    [[ $(rg -c '^DOS capacity .* errno=0$' "$run/raid-cold-read.log") == ${#topologies[@]} ]]
    if [[ -n $diskinfo_check ]];then rg -q '^PASS DiskInfo capacity boundary cases=9$' "$run/diskinfo-capacity.log";fi
    for topology in "${topologies[@]}";do
        if [[ $gui == 1 ]];then continue;fi
        rg -q "^RESULT cold RAID DOS $topology degraded=0 mode=0 errno=0 PASS$" "$run/raid-cold-read.log"
    done
    cleanup
    if [[ $array50 == 1 ]];then
        python3 "$run/sparse-digest.py" "${all_members[@]}" >"$run/after-read.sparse"
        cmp "$run/before-read.sparse" "$run/after-read.sparse"
    else sha256sum -c "$run/before-read.sha256" | tee "$run/after-read-verified.log";fi
    echo 'PASS independent hosted readonly DOS matrix and unchanged members' | tee -a "$run/results.txt"
fi
sha256sum -c "$run/inputs.sha256"
echo "PASS: hosted large file-vdev matrix: $run"
