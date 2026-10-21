#!/usr/bin/env bash
# Only disposable RAM-file members; no host disks and no network.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
binary=${AROS_TEST_DISCOVERY_BINARY:?discovery test binary required}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private BSP overlay required}
run=$(mktemp -d "$repo/build-aros/discovery-run.XXXXXXXX")
echo "Evidence: $run"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
tar -czf "$run/test-source.tar.gz" aros/engine lib/libtpool lib/libzutil/zutil_import.c
sha256sum "$base" "$binary" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" >"$run/inputs.sha256"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map build-aros/hosted/Libs/z1.library /Libs/z1.library \
    -map aros/engine/security-fixture /Security \
    -map "$binary" /C/OpenZFSPoolTest \
    -map aros/engine/Startup-Discovery /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-discovery-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!;printf '%s\n' "$pid" >"$run/qemu.pid"
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+300))
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -a -q '^RESULT .*FAIL|^FAIL |\[PF-DBG\]|^ASSERT at' "$run/serial.log";then echo "FAIL: $run";exit 1;fi
        if rg -a -q '^RESULT native mirror create/open/export PASS' "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "FAIL exit/deadline: $run";exit 1;fi
    sleep 1
done
rg -a -q '^upstream label discovery=0 ' "$run/serial.log"
rg -a -q '^dataset payload read/compare=0 bytes=16384' "$run/serial.log"
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
echo "PASS: upstream label discovery and payload re-import in RAM file mirror; NOT cold persistence: $run"
