#!/usr/bin/env bash
# Read-only guest inspection of newly generated disposable GPT and blank disks.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
mode=${AROS_TEST_PARTITION_MODE:-gpt}
case "$mode" in gpt|mbr|overlap|out-of-bounds) ;; *) echo 'Invalid fixture mode' >&2;exit 2;; esac
run=$(mktemp -d "$repo/build-aros/partition-catalog-run.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
cp aros/platform/partitions.c aros/platform/partitions.h aros/platform/partitions-test.c "$run/"
cp aros/engine/device-io.c aros/engine/device-io.h "$run/"
cp aros/platform/Startup-Partitions "$run/Startup-Sequence"
sed -i "s|C:OpenZFSPartitionTest$|C:OpenZFSPartitionTest $mode|" "$run/Startup-Sequence"
cp aros/platform/make-partition-fixture.py "$run/"
cp "$0" "$run/"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -I"$repo/build-aros/pthread-consistent.s3r1CHla/source" -Wall -Wextra -Werror -O2 \
    aros/platform/partitions.c "$run/device-io.c" "$run/partitions-test.c" \
    "$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a" -o "$run/OpenZFSPartitionTest"
qemu-img create -f raw "$run/blank.raw" 256M
if [[ "$mode" == gpt ]];then
qemu-img create -f raw "$run/gpt.raw" 256M
sgdisk --clear --set-alignment=1 --new=1:4097:135168 --typecode=1:BF01 --change-name=1:OpenZFS-A \
    --new=2:139265:270336 --typecode=2:BF01 --change-name=2:OpenZFS-B "$run/gpt.raw"
sgdisk --print --verify "$run/gpt.raw" > "$run/gpt-layout.txt"
else
python3 "$run/make-partition-fixture.py" "$mode" "$run/gpt.raw"
sfdisk --dump "$run/gpt.raw" > "$run/mbr-layout.txt"
fi
printf '%s\n' "$mode" > "$run/fixture-mode.txt"
sha256sum "$run/gpt.raw" "$run/blank.raw" > "$run/disks-before.sha256"
sha256sum "$run/partitions.c" "$run/partitions.h" "$run/partitions-test.c" \
    "$run/device-io.c" "$run/device-io.h" \
    "$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a" \
    "$repo/build-aros/pthread-consistent.s3r1CHla/source/pthread.h" \
    "$run/make-partition-fixture.py" "$run/Startup-Sequence" "$run/test-qemu-partitions.sh" \
    "$run/OpenZFSPartitionTest" "$base" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" "$posix" \
    "${sdk%/Developer}/Libs/partition.library" > "$run/inputs.sha256"
git rev-parse HEAD > "$run/source-head.txt"
git diff --binary > "$run/source.patch"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    -map "${sdk%/Developer}/Libs/partition.library" /Libs/partition.library \
    -map "$run/OpenZFSPartitionTest" /C/OpenZFSPartitionTest \
    -map "$run/Startup-Sequence" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-partitions-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -drive "file=$run/gpt.raw,format=raw,if=ide,index=0,snapshot=on" \
    -drive "file=$run/blank.raw,format=raw,if=ide,index=1,snapshot=on" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" > "$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
        if rg -q "^RESULT native partition catalog PASS mode=$mode cycles=16 unchanged-output=1" "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo 'QEMU exited or timed out' >&2;exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
sha256sum -c "$run/disks-before.sha256" | tee "$run/disks-after-check.txt"
echo "PASS: native partition catalog. Evidence: $run"
