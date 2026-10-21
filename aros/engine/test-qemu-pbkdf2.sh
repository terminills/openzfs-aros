#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
run=$(mktemp -d "$repo/build-aros/pbkdf2-run.XXXXXXXX")
echo "Evidence: $run"
cp build-aros/OpenZFSPBKDF2Test "$run/OpenZFSPBKDF2Test"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
cp aros/engine/pbkdf2-test.c "$run/pbkdf2-test.c"
cp aros/engine/passphrase.c aros/engine/passphrase.h "$run/"
cp build-aros/pbkdf2-build-inputs.sha256 build-aros/pbkdf2-crypto-members.sha256 \
    aros/engine/build-pbkdf2-test.sh "$run/"
sha256sum "$base" "$run/OpenZFSPBKDF2Test" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" "$posix" >"$run/inputs.sha256"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    -map "$run/OpenZFSPBKDF2Test" /C/OpenZFSPBKDF2Test \
    -map aros/engine/Startup-PBKDF2 /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-pbkdf2-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then exit 1;fi
        if rg -q '^RESULT native PBKDF2 known answers PASS' "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "Timeout/exit: $run" >&2;exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
echo "PASS: native PBKDF2 known answers. Evidence: $run"
