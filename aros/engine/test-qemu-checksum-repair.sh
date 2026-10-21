#!/usr/bin/env bash
# This exact fixture has a 4097-sector partition prefix and a verified payload
# object 1 L0 block at DVA 0:68000:1000. No arbitrary device or pool input.
set -euo pipefail
if [[ ${OZ_CHECKSUM_RUNNER_FROZEN:-0} != 1 ]];then
    export OZ_CHECKSUM_RUNNER_FROZEN=1
    exec bash -c "$(< "$0")" "$0" "$@"
fi
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
root=/mnt/openzfs-artifacts/build-aros
unrecoverable=${AROS_TEST_CHECKSUM_UNRECOVERABLE:-0}
[[ $unrecoverable == 0 || $unrecoverable == 1 ]]
baseline="$root/qemu-run.fpp1hYtd"
binary="$root/checksum-repair-20261004/guest/OpenZFSQEMUReopenTest"
source aros/engine/storage-preflight.sh
oz_storage_preflight "$root"
sha256sum -c "$baseline/disks-before.sha256"
run=$(mktemp -d "$root/checksum-run.XXXXXXXX")
echo "Evidence: $run"
cp "$binary" "$run/OpenZFSQEMUReopenTest"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
sha256sum "$baseline/reopen.iso" "$baseline/degraded.iso" "$run/OpenZFSQEMUReopenTest" >"$run/inputs.sha256"
python3 aros/engine/corrupt-test-member.py "$baseline/disk0.raw" "$run/disk0.raw" \
    $((4097*512+0x468000)) a39e2c53ec1becff5067bdb8de6286e321011d886afef517fece22671ce4e1a6 >"$run/corruption.json"
mode=CHECKSUM
if [[ $unrecoverable == 1 ]];then
    mode=CHECKSUM-UNRECOVERABLE
    python3 aros/engine/corrupt-test-member.py "$baseline/disk1.raw" "$run/disk1.raw" \
        $((4097*512+0x468000)) a39e2c53ec1becff5067bdb8de6286e321011d886afef517fece22671ce4e1a6 >"$run/corruption1.json"
else
    cp --reflink=auto --sparse=always "$baseline/disk1.raw" "$run/disk1.raw"
fi
sed "s/C:OpenZFSQEMUReopenTest\$/C:OpenZFSQEMUReopenTest $mode/" \
    aros/engine/Startup-QEMU-Reopen-Test >"$run/startup"
xorriso -indev "$baseline/reopen.iso" -outdev "$run/repair.iso" \
    -map "$run/OpenZFSQEMUReopenTest" /C/OpenZFSQEMUReopenTest \
    -map "$run/startup" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
pid=
cleanup(){ if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
phases=(repair survivor)
if [[ $unrecoverable == 1 ]];then
    phases=(repair log)
    sed 's/C:OpenZFSQEMUReopenTest$/C:OpenZFSQEMUReopenTest CHECKSUM-LOG/' \
        aros/engine/Startup-QEMU-Reopen-Test >"$run/log-startup"
    xorriso -indev "$run/repair.iso" -outdev "$run/log.iso" \
        -map "$run/log-startup" /S/Startup-Sequence \
        -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack-log.log" 2>&1
fi
for phase in "${phases[@]}";do
    iso="$run/repair.iso"; member=(-drive "file=$run/disk1.raw,format=raw,if=ide,index=1")
    marker='^RESULT cold writable reopen PASS'
    if [[ $phase == log ]];then iso="$run/log.iso";fi
    if [[ $phase == survivor ]];then
        iso="$baseline/degraded.iso";member=();marker='^RESULT cold-boot QEMU degraded mirror through AROS handler PASS'
        sha256sum "$run/disk0.raw" >"$run/survivor-before.sha256"
    fi
    qemu-system-x86_64 -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
        -drive "file=$iso,media=cdrom,if=ide,index=2,readonly=on" \
        -drive "file=$run/disk0.raw,format=raw,if=ide,index=0" "${member[@]}" \
        -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
        -serial "file:$run/$phase-serial.log" \
        -monitor "unix:$run/$phase.monitor,server=on,wait=off" >"$run/$phase-qemu.log" 2>&1 &
    pid=$!;deadline=$((SECONDS+600))
    while :;do
        if [[ ! -f "$run/$phase-serial.log" ]];then
            kill -0 "$pid"; ((SECONDS<deadline));sleep 1;continue
        fi
        perl -0777 -pe 's/[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| \[security.library\] secTaskNotify: [^\r\n]*\r?\n//g' \
            "$run/$phase-serial.log" >"$run/$phase-checked.log"
        if rg -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/$phase-checked.log";then exit 1;fi
        if rg -q "$marker" "$run/$phase-checked.log";then break;fi
        kill -0 "$pid"; ((SECONDS<deadline));sleep 1
    done
    printf 'info blockstats\nquit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >"$run/$phase-blockstats.txt"
    wait "$pid";pid=
done
rg -q '^cold checksum scrub detected and completed=0' "$run/repair-checked.log"
if [[ $unrecoverable == 1 ]];then
    rg -q '^cold checksum unrecoverable read rejected=0' "$run/repair-checked.log"
    rg -q '^cold checksum actual error log attribution=0' "$run/repair-checked.log"
    rg -q '^cold checksum persistent error log attribution=0' "$run/log-checked.log"
    sha256sum -c "$baseline/disks-before.sha256"
    echo "PASS: both mirror copies corrupted, native scrub and read report failure: $run"
    exit 0
fi
rg -q '^cold checksum payload comparison=0' "$run/repair-checked.log"
sha256sum -c "$run/survivor-before.sha256"
sha256sum -c "$baseline/disks-before.sha256"
python3 aros/engine/pool-partition-fixture.py extract "$run/disk0.raw" "$run/repaired-partition.raw"
build-control/zdb -e -p "$run/repaired-partition.raw" -bcc qemu_mirror >"$run/repaired-zdb.log" 2>&1
rg -q 'No leaks' "$run/repaired-zdb.log"
echo "PASS: real checksum damage, native scrub, repaired-member-only cold read: $run"
