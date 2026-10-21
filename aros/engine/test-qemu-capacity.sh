#!/usr/bin/env bash
# Fresh sparse fixtures only; no user/DUT devices.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
capacity_tib=${AROS_TEST_CAPACITY_TIB:-3}
[[ $capacity_tib == 3 || $capacity_tib == 50 ]]
capacity_bytes=$((capacity_tib*1024*1024*1024*1024))
run=$(mktemp -d "$repo/build-aros/capacity-run.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
cp aros/engine/device-io.{c,h} aros/engine/capacity-test.c "$run/"
cp aros/platform/partitions.{c,h} "$run/"
cp "$0" aros/engine/Startup-Device-Range "$run/"
# Reuse the bounded test startup, replacing only the executable mapped below.
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -I"$repo/build-aros/pthread-consistent.s3r1CHla/source" -Wall -Wextra -Werror -O2 \
    -DOZ_CAPACITY_TIB="$capacity_tib" "$run/device-io.c" "$run/capacity-test.c" aros/platform/partitions.c \
    "$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a" -o "$run/OpenZFSDeviceRangeTest" >"$run/build.log" 2>&1
qemu-img create -f qcow2 "$run/disk.qcow2" "${capacity_tib}T"
# Distinguish the final sector from a wrapped low-LBA read of this empty image.
qemu-io -f qcow2 -c "write -P 0x5a $((capacity_bytes-512)) 512" "$run/disk.qcow2" > "$run/fixture.log"
qemu-img create -f raw "$run/gpt.raw" 3T
sgdisk --clear --set-alignment=1 --new=1:4294969344:6442446847 \
    --typecode=1:BF01 --change-name=1:OpenZFS-large "$run/gpt.raw" > "$run/gpt-create.log" 2>&1
sgdisk --verify --print "$run/gpt.raw" > "$run/gpt-verify.log" 2>&1
rg -q 'No problems found' "$run/gpt-verify.log"
qemu-img create -f qcow2 -F raw -b "$run/gpt.raw" "$run/gpt.qcow2"
# QEMU opens the sparse backing file readonly; all guest writes would change
# the overlay. Hashing the overlay avoids scanning 3 TiB of sparse zeros.
sha256sum "$run/disk.qcow2" "$run/gpt.qcow2" > "$run/before.sha256"
git rev-parse HEAD > "$run/source-head.txt"
git diff --binary > "$run/source.patch"
sha256sum "$run/"*.c "$run/"*.h "$run/OpenZFSDeviceRangeTest" \
    "$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a" \
    "$repo/build-aros/pthread-consistent.s3r1CHla/source/pthread.h" \
    "$base" "$ata/aros-bsp.pkg.xz" "$dos/aros-base.pkg.xz" "$posix" > "$run/inputs.sha256"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    -map "$run/OpenZFSDeviceRangeTest" /C/OpenZFSDeviceRangeTest \
    -map aros/engine/public-service-test-only /openzfs-capacity-test-only \
    -map aros/engine/Startup-Device-Range /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-capacity-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -drive "file=$run/disk.qcow2,format=qcow2,if=ide,index=0" \
    -drive "file=$run/gpt.qcow2,format=qcow2,if=ide,index=1" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -q 'RESULT .*FAIL|\[PF-DBG\]|ASSERT at' "$run/serial.log";then exit 1;fi
        if rg -q "^RESULT native capacity PASS size=$capacity_bytes .*rc=0 line=0" "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo 'QEMU exited or timed out' >&2;exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
sha256sum -c "$run/before.sha256" | tee "$run/after-check.txt"
echo "PASS: native $capacity_tib TiB capacity and high-LBA read; separate 3 TiB GPT. Evidence: $run"
