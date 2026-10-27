#!/usr/bin/env bash
# Read-only ISO fixture, no guest disks, network, service or host filesystem share.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
: "${AROS_CC:?}" "${AROS_SDK:?}" "${AROS_TEST_ISO:?}"
: "${AROS_TEST_BASE_DOS_PACKAGE:?}" "${AROS_TEST_POSIX_LIBRARY:?}"
: "${AROS_TEST_ZDB_BINARY:?}" "${AROS_TEST_ZDB_MEMBER:?}" "${AROS_TEST_HOST_ZDB:?}"
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:?}")
run=$(mktemp -d "$root/zdb-label-run.XXXXXXXX")
echo "Evidence: $run"
cp "$0" aros/cli/zdb-label-native-test.c aros/cli/Startup-Zdb-Labels "$run/"
sha256sum "$AROS_TEST_ISO" "$AROS_TEST_BASE_DOS_PACKAGE" "$AROS_TEST_POSIX_LIBRARY" \
    "$AROS_TEST_ZDB_BINARY" "$AROS_TEST_ZDB_MEMBER" "$AROS_TEST_HOST_ZDB" >"$run/inputs.sha256"
"$AROS_TEST_HOST_ZDB" -l "$AROS_TEST_ZDB_MEMBER" >"$run/host-labels.log" 2>&1
grep -q "name: 'topo_source'" "$run/host-labels.log"
grep -q 'labels = 0 1 2 3 ' "$run/host-labels.log"
"$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -O2 -Wall -Wextra \
    aros/cli/zdb-label-native-test.c -o "$run/OpenZFSZdbLabelsTest" >"$run/build.log" 2>&1
xorriso -indev "$AROS_TEST_ISO" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$AROS_TEST_BASE_DOS_PACKAGE" /boot/aros-base.pkg.xz \
    -map "$AROS_TEST_POSIX_LIBRARY" /Libs/posixc.library \
    -map "$AROS_TEST_ZDB_BINARY" /C/ZdbAudit \
    -map "$AROS_TEST_ZDB_MEMBER" /zdb-member \
    -map "$run/OpenZFSZdbLabelsTest" /C/OpenZFSZdbLabelsTest \
    -map aros/cli/Startup-Zdb-Labels /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/iso.log" 2>&1
qemu-system-x86_64 -name "openzfs-zdb-labels-$$" -machine pc,accel=kvm \
    -cpu host -smp 8 -m 2048 -cdrom "$run/test.iso" -boot d -nic none \
    -display none -serial "file:$run/serial.log" \
    -monitor "unix:$run/monitor,server=on,wait=off" -no-reboot -no-shutdown >"$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+240))
while :;do
    if [[ -f $run/serial.log ]];then
        if rg -q 'RESULT native zdb labels FAIL|\[PF-DBG\]|ASSERT at|went out of stack limits' "$run/serial.log";then exit 1;fi
        if rg -q '(^| \| )RESULT native zdb labels PASS runs=2 storage=readonly-iso\r?$' "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then
        echo 'Native zdb label test timed out or exited' >&2;exit 1
    fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
sha256sum -c "$run/inputs.sha256" >"$run/unchanged.log"
python3 aros/cli/verify-zdb-labels.py "$run/serial.log" "$run/host-labels.log" >"$run/comparison.log"
echo 'PASS native zdb labels only; shared-library packaging and full pool traversal remain unverified.'
