#!/usr/bin/env bash
# Startup/ABI regression fixture. No service, pool, writable disks or network.
# Interactive shell and Workbench launches are separately observed, not inferred.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
base=$(realpath -e "${AROS_TEST_ISO:?matching target ISO required}")
manager=$(realpath -e "${AROS_TEST_MANAGER_BINARY:?matching target Manager required}")
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_TEST_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$root/manager-startup-run.XXXXXXXX")
echo "Evidence: $run"
cp "$0" "$run/test-qemu-no-service.sh"
cp aros/ui/Startup-No-Service "$run/Startup-Sequence"
cp "$manager" "$run/OpenZFSManagerNative"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
sha256sum "$base" "$manager" "$run/Startup-Sequence" >"$run/inputs.sha256"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$run/Startup-Sequence" /S/Startup-Sequence \
    -map "$run/OpenZFSManagerNative" /Tools/OpenZFSManagerNative \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-manager-startup-$$" -machine pc,accel=kvm \
    -cpu host -smp 2 -m 2048 -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -device qemu-xhci -device usb-tablet -serial "file:$run/serial.log" \
    -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
printf '%s\n' "$pid" >"$run/qemu.pid"
trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
wait "$pid"
