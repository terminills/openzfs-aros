#!/usr/bin/env bash
# Focused native control/event fixture: no writable host disks or network.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
binary=${1:?native control-file test binary required}
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
qemu=${AROS_TEST_QEMU:-qemu-system-x86_64}
run=$(mktemp -d "$repo/build-aros/control-event-run.XXXXXXXX")
echo "Evidence: $run"
cp "$binary" "$run/OpenZFSControlFileTest"
cp "$0" "$run/"
cp aros/cli/control-file-test.c "$run/"
git diff --binary >"$run/source.patch"
printf 'Assign T: RAM:\nStack 262144\nC:OpenZFSControlFileTest\n' >"$run/Startup-Sequence"
sha256sum "$base" "$run/OpenZFSControlFileTest" "$dos/aros-base.pkg.xz" \
    "$ata/aros-bsp.pkg.xz" "$posix" "$(command -v "$qemu")" >"$run/inputs.sha256"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    -map "$run/OpenZFSControlFileTest" /C/OpenZFSControlFileTest \
    -map "$run/Startup-Sequence" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
"$qemu" -name "openzfs-control-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -q 'FAIL|ASSERT at|\[PF-DBG\]|went out of stack limits' "$run/serial.log";then exit 1;fi
        if rg -q 'RESULT native event overflow PASS published=600 retained=512 ordered=1 drop-notice=1 reset=1 independent=1 drain=512' "$run/serial.log" &&
           rg -q 'RESULT native control files PASS cycles=16 callbacks=16 events=16 retained=1 threads=16 minors=144' "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "Timeout/exit: $run" >&2;exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
echo "PASS: native control lifecycle and event overflow. Evidence: $run"
