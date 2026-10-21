#!/usr/bin/env bash
# Diskless, no network, no service: exact upstream commands and failure behavior.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
link=${1:?successful whole-command link directory required}
rg -Fxq 'PASS zfs link' "$link/summary.txt"
rg -Fxq 'PASS zpool link' "$link/summary.txt"
sdk=/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer
cc=/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA required}
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX required}
run=$(mktemp -d "$repo/build-aros/whole-cli-run.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
cp "$0" aros/cli/Startup-Whole-Commands aros/cli/whole-command-test.c aros/cli/share-unavailable-test.c aros/cli/share-unavailable.c "$run/"
cp "$link/zfs" "$link/zpool" "$run/"
flags=(--sysroot="$sdk" -B"$sdk/lib/" -O2 -Wall -Wextra -D_GNU_SOURCE
    -D_FILE_OFFSET_BITS=64 -include aros/engine/config.h -Iaros/cli/include
    -Ibuild-aros/include -Iaros/engine/include -Ilib/libspl/include
    -Ilib/libspl/include/os/linux -Iinclude -Iinclude/os/linux/zfs)
"$cc" "${flags[@]}" aros/cli/share-unavailable.c aros/cli/share-unavailable-test.c \
    -o "$run/OpenZFSShareUnavailableTest" >"$run/share-build.log" 2>&1
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -Wall -Wextra \
    aros/cli/whole-command-test.c aros/cli/process.c aros/cli/arguments.c aros/cli/output.c \
    -o "$run/OpenZFSWholeCommandTest" >"$run/command-build.log" 2>&1
sha256sum "$run/zfs" "$run/zpool" "$run/"*.c "$run/"OpenZFS* "$base" \
    "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" "$posix" >"$run/inputs.sha256"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    -map "$run/zfs" /C/zfs -map "$run/zpool" /C/zpool \
    -map "$run/OpenZFSShareUnavailableTest" /C/OpenZFSShareUnavailableTest \
    -map "$run/OpenZFSWholeCommandTest" /C/OpenZFSWholeCommandTest \
    -map "$run/Startup-Whole-Commands" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-whole-cli-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
trap 'if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi' EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -q 'RESULT native .*FAIL|\[PF-DBG\]|^ASSERT at|signal=6 default action=terminate' "$run/serial.log";then exit 1;fi
        if rg -q 'RESULT native whole CLI PASS commands=2 repetitions=2 absent-service=1\r?$' "$run/serial.log" &&
           rg -q 'RESULT native CLI absent sharing PASS protocols=2 actions=5\r?$' "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
echo "PASS: diskless native whole CLI and unavailable sharing: $run"
