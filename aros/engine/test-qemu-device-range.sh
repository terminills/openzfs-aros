#!/usr/bin/env bash
# Exact-range I/O only on new sparse images under a private evidence directory.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_TEST_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/device-range-run.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
cp aros/engine/device-io.c aros/engine/device-io.h aros/engine/device-range-test.c "$run/"
cp aros/engine/efi-uuid.c aros/engine/efi-uuid.h "$run/"
cp aros/engine/efi-native.c aros/engine/efi-native.h aros/engine/efi-native-test.c lib/libefi/rdwr_efi.c "$run/"
cp aros/engine/efi-label.c "$run/"
cp aros/engine/Startup-Device-Range "$run/Startup-Sequence"
cp "$0" "$run/"
efi_flags=(--sysroot="$sdk" -B"$sdk/lib/" -O2 -Wall -Wextra -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
    -include "$repo/aros/engine/config.h" -I"$repo/aros/cli/include" -I"$repo/build-aros/include"
    -I"$repo/aros/engine/include" -I"$repo/lib/libspl/include" -I"$repo/lib/libspl/include/os/linux"
    -I"$repo/include" -I"$repo/include/os/linux/zfs")
"$cc" "${efi_flags[@]}" -c lib/libefi/rdwr_efi.c -o "$run/efi.o" >"$run/efi-build.log" 2>&1
"$cc" "${efi_flags[@]}" -c aros/engine/efi-native-test.c -o "$run/efi-test.o" >"$run/efi-test-build.log" 2>&1
"$cc" "${efi_flags[@]}" -c aros/engine/efi-label.c -o "$run/efi-label.o" >"$run/efi-label-build.log" 2>&1
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -I"$repo/build-aros/pthread-consistent.s3r1CHla/source" -Wall -Wextra -Werror -O2 \
    "$run/device-io.c" "$run/efi-uuid.c" "$run/device-range-test.c" \
    aros/engine/efi-native.c aros/platform/partitions.c "$run/efi.o" "$run/efi-test.o" "$run/efi-label.o" \
    "$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a" -lz -o "$run/OpenZFSDeviceRangeTest"
qemu-img create -f raw "$run/disk0.raw" 256M
qemu-img create -f raw "$run/disk1.raw" 8G
sha256sum "$run/disk0.raw" "$run/disk1.raw" > "$run/disks-before.sha256"
qemu-img create -f qcow2 -F raw -b "$run/disk0.raw" "$run/disk0.qcow2"
qemu-img create -f qcow2 -F raw -b "$run/disk1.raw" "$run/disk1.qcow2"
sha256sum "$run/device-io.c" "$run/device-io.h" "$run/device-range-test.c" \
    "$run/efi-uuid.c" "$run/efi-uuid.h" \
    "$run/efi-native.c" "$run/efi-native.h" "$run/efi-native-test.c" "$run/efi-label.c" "$run/rdwr_efi.c" include/sys/efi_partition.h \
    aros/platform/partitions.c "$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a" \
    "$run/OpenZFSDeviceRangeTest" "$run/Startup-Sequence" "$run/test-qemu-device-range.sh" \
    "$base" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" "$posix" > "$run/inputs.sha256"
git rev-parse HEAD > "$run/source-head.txt"
git diff --binary > "$run/source.patch"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    -map "$run/OpenZFSDeviceRangeTest" /C/OpenZFSDeviceRangeTest \
    -map aros/engine/public-service-test-only /openzfs-device-range-test-only \
    -map "$run/Startup-Sequence" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-device-range-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -drive "file=$run/disk0.qcow2,format=qcow2,if=ide,index=0" \
    -drive "file=$run/disk1.qcow2,format=qcow2,if=ide,index=1" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" > "$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
        if rg -q '^RESULT native bounded device I/O PASS units=2 cross-4GiB=1 translated=1 guards=1 bounds=1 readonly=1 reopen=1' "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo 'QEMU exited or timed out' >&2;exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
rg -q '^RESULT native DOS claim PASS inactive=1 whole=1 range=1 read=1 other-unit=1 removed=1 line=0' "$run/serial.log"
rg -q '^RESULT native DOS revocation PASS late-claim=1 unchanged=1 sticky=1 reopen=1' "$run/serial.log"
rg -q '^RESULT native active DOS claims PASS units=2 denied=1 inhibited=1 detached=1' "$run/serial.log"
rg -q '^RESULT native DOS claims restored PASS units=2 denied=1' "$run/serial.log"
rg -q '^RESULT native shared entry read lock PASS writer-completed=1 mutators-excluded=1' "$run/serial.log"
for unit in 0 1;do
    rg -q "^RESULT native binding repath PASS unit=$unit identity=1 rollback=1 commit=1 collisions=1 reservation=1" "$run/serial.log"
done
rg -q '^RESULT native EFI UUID PASS library=1 count=16 network-order=1 version=1 variant=1' "$run/serial.log"
rg -q 'RESULT upstream native EFI PASS create=1 expand=1 reread=1 rescan=1 readonly=1 stale=1 collision=1 overflow=1 line=0' "$run/serial.log"
rg -q 'RESULT native GPT label PASS bounds=1 minimum=1 stale=1 write=1 reread=1 readonly=1' "$run/serial.log"
rg -q 'RESULT native GPT expand PASS grow=1 identity=1 already-full=1 readonly=1' "$run/serial.log"
sha256sum -c "$run/disks-before.sha256" | tee "$run/disks-after-check.txt"
# A new guest with retained disposable overlays. ide-hd rejects readonly
# block nodes; the guest test opens only a readonly native handle, and exact
# overlay hashes below detect any write by the guest (including automount).
# No reuse of the creating guest's memory or partition.library roots.
sha256sum "$run/disk0.qcow2" "$run/disk1.qcow2" > "$run/cold-before.sha256"
qemu-img convert -f qcow2 -O raw "$run/disk0.qcow2" "$run/gpt-host-check.raw"
sgdisk -v "$run/gpt-host-check.raw" > "$run/host-gpt-verify.log" 2>&1
rg -q 'No problems found' "$run/host-gpt-verify.log"
sfdisk --json "$run/gpt-host-check.raw" > "$run/host-gpt.json"
xorriso -indev "$base" -outdev "$run/cold.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    -map "$run/OpenZFSDeviceRangeTest" /C/OpenZFSDeviceRangeTest \
    -map aros/engine/public-service-test-only /openzfs-device-range-test-only \
    -map "$run/Startup-Sequence" /S/Startup-Sequence \
    -map aros/engine/public-service-test-only /openzfs-efi-cold-only \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$run/cold-pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-efi-cold-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/cold.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -drive "file=$run/disk0.qcow2,format=qcow2,if=ide,index=0" \
    -drive "file=$run/disk1.qcow2,format=qcow2,if=ide,index=1" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/cold-serial.log" -monitor "unix:$run/cold-monitor,server=on,wait=off" > "$run/cold-qemu.log" 2>&1 &
pid=$!
trap cleanup EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f "$run/cold-serial.log" ]];then
        if rg -q 'RESULT .*FAIL|\[PF-DBG\]|ASSERT at' "$run/cold-serial.log";then exit 1;fi
        if rg -q 'RESULT upstream native EFI cold PASS layout=1 readonly=1 rescan=1 error=0' "$run/cold-serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo 'Cold QEMU exited or timed out' >&2;exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/cold-monitor" >/dev/null
wait "$pid"
trap - EXIT
rg -o 'EFI identity (disk|data|reserved)=[0-9a-f]{32}' "$run/serial.log" > "$run/warm-identity.txt"
rg -o 'EFI identity (disk|data|reserved)=[0-9a-f]{32}' "$run/cold-serial.log" > "$run/cold-identity.txt"
[[ $(wc -l < "$run/warm-identity.txt") == 3 ]]
cmp "$run/warm-identity.txt" "$run/cold-identity.txt"
sha256sum -c "$run/cold-before.sha256" | tee "$run/cold-after-check.txt"
sha256sum -c "$run/disks-before.sha256" | tee "$run/cold-originals-check.txt"
echo "PASS: native bounded I/O and cold upstream GPT persistence. Evidence: $run"
