#!/usr/bin/env bash
# Actual runtime I/O on a private RAM file; no disks, network or shared session.
set -euo pipefail
repo=${AROS_POSITIONED_SOURCE_ROOT:-$(cd -- "$(dirname -- "$0")/../.." && pwd)}
source "$repo/aros/engine/storage-preflight.sh"
root=$(oz_artifact_root "$repo" "${AROS_TEST_ARTIFACT_ROOT:?external artifact directory required}")
if [[ ${AROS_POSITIONED_RUNNER:-} != "$0" ]];then
    snapshot=$(mktemp -d "$root/positioned-runner.XXXXXXXX")
    cp "$0" "$snapshot/runner.sh"
    exec env AROS_POSITIONED_SOURCE_ROOT="$repo" AROS_POSITIONED_RUNNER="$snapshot/runner.sh" bash "$snapshot/runner.sh"
fi
: "${AROS_CC:?}" "${AROS_SDK:?}" "${AROS_PRIVATE_PTHREAD:?}" "${AROS_PTHREAD_INCLUDE:?}"
: "${AROS_TEST_ISO:?}" "${AROS_TEST_POSIX_LIBRARY:?}" "${AROS_TEST_BASE_DOS_PACKAGE:?}" "${AROS_PACKAGE_TOOL:?}"
run=$(mktemp -d "$root/positioned-concurrent.XXXXXXXX")
echo "Evidence: $run"
cp "$0" "$run/runner.sh"
cp "$repo/aros/cli/positioned-concurrent-test.c" "$repo/aros/cli/Startup-Positioned-Concurrent" "$run/"
git -C "$repo" rev-parse HEAD >"$run/source-head.txt"
git -C "$repo" diff --binary >"$run/source.patch"
"$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -I"$AROS_PTHREAD_INCLUDE" -O2 -Wall -Wextra -Werror "$run/positioned-concurrent-test.c" "$AROS_PRIVATE_PTHREAD" -o "$run/OpenZFSPositionedConcurrentTest" >"$run/build.log" 2>&1
python3 "$repo/aros/engine/verify-dos-package-overlay.py" "$AROS_TEST_ISO" "$AROS_TEST_BASE_DOS_PACKAGE" "$AROS_PACKAGE_TOOL" >"$run/dos-check.log" 2>&1
sha256sum "$AROS_CC" "$AROS_TEST_ISO" "$AROS_TEST_POSIX_LIBRARY" "$AROS_TEST_BASE_DOS_PACKAGE" "$AROS_PRIVATE_PTHREAD" "$AROS_PTHREAD_INCLUDE/pthread.h" "$run/OpenZFSPositionedConcurrentTest" "$run/positioned-concurrent-test.c" >"$run/inputs.sha256"
xorriso -indev "$AROS_TEST_ISO" -outdev "$run/test.iso" \
    -map "$run/OpenZFSPositionedConcurrentTest" /C/OpenZFSPositionedConcurrentTest \
    -map "$run/Startup-Positioned-Concurrent" /S/Startup-Sequence \
    -map "$repo/aros/engine/grub-qemu-test.cfg" /boot/grub/grub.cfg \
    -map "$AROS_TEST_POSIX_LIBRARY" /Libs/posixc.library \
    -map "$AROS_TEST_BASE_DOS_PACKAGE" /boot/aros-base.pkg.xz \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-positioned-$$" -machine pc,accel=kvm -cpu host -smp 8 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,readonly=on" -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" >"$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f $run/serial.log ]];then
        if rg -q 'RESULT positioned .*FAIL|ASSERT at|\[PF-DBG\]' "$run/serial.log";then exit 1;fi
        count=$(rg -c 'RESULT positioned concurrency PASS workers=4 iterations=1000 dup=1 ordinary-read=1 offset=1' "$run/serial.log" || true)
        if [[ $count == 3 ]] && rg -q 'RESULT positioned shell PASS runs=3' "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo 'Guest exited or timed out';exit 1;fi
    sleep 1
done
cleanup
trap - EXIT
sha256sum "$run/test.iso" >"$run/pruned-iso.sha256"
holder_status=0
fuser "$run/test.iso" >"$run/holders.log" 2>&1 || holder_status=$?
[[ $holder_status == 1 && ! -s $run/holders.log ]]
rm -- "$run/test.iso"
echo "PASS positioned concurrency: three eight-CPU executions; logs retained at $run"
