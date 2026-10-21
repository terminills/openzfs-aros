#!/usr/bin/env bash
# Diskless native path semantics test. Never opens a device or uses a network.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
run=$(mktemp -d "$repo/build-aros/device-path-run.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
cp aros/cli/device-path.c aros/cli/device-path.h aros/cli/device-path-test.c "$run/"
cp aros/cli/Startup-Device-Path "$run/Startup-Sequence"
cp "$0" "$run/"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror -O2 \
    "$run/device-path.c" "$run/device-path-test.c" -o "$run/OpenZFSDevicePathTest"
gcc -Wall -Wextra -Werror -O2 -fsanitize=address,undefined \
    "$run/device-path.c" "$run/device-path-test.c" -o "$run/host-test"
"$run/host-test" > "$run/host-test.log" 2>&1
sha256sum "$run/"*.c "$run/"*.h "$run/Startup-Sequence" "$run/test-qemu-device-path.sh" \
    "$run/OpenZFSDevicePathTest" "$base" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" "$posix" > "$run/inputs.sha256"
git rev-parse HEAD > "$run/source-head.txt"
git diff --binary > "$run/source.patch"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    -map "$run/OpenZFSDevicePathTest" /C/OpenZFSDevicePathTest \
    -map "$run/Startup-Sequence" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-device-path-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" > "$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
        if rg -q '^RESULT native device paths PASS grammar=1 bounds=1 unchanged=1 file-names=1' "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo 'QEMU exited or timed out' >&2;exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
echo "PASS: native device paths. Evidence: $run"
