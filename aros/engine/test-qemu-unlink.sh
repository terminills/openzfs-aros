#!/usr/bin/env bash
# No hard disk or networking; qualify the real native adapter on RAM:.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:?external artifact root required}")
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?ATA overlay required}
posix=${AROS_TEST_POSIX_LIBRARY:?POSIX library required}
stdio=${AROS_TEST_STDCIO_LIBRARY:?matching stdcio library required}
binary=${AROS_TEST_BINARY:?native unlink test required}
run=$(mktemp -d "$root/unlink-run.XXXXXXXX")
echo "Evidence: $run"
cp "$binary" "$run/OpenZFSUnlinkTest"
cp aros/engine/unlink-test.c aros/engine/Startup-Unlink "$run/"
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
sha256sum "$base" "$binary" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" "$posix" "$stdio" >"$run/inputs.sha256"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    -map "$stdio" /Libs/stdcio.library \
    -map "$run/OpenZFSUnlinkTest" /C/OpenZFSUnlinkTest \
    -map "$run/Startup-Unlink" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-unlink-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -q 'RESULT native unlink FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
        if rg -q 'RESULT native unlink PASS existing=0 missing=ENOENT' "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "Timeout/exit: $run" >&2;exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
echo "PASS: native unlink errno contract: $run"
