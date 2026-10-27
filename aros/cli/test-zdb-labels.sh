#!/usr/bin/env bash
# Read-only ISO fixture, no guest disks, network, service or host filesystem share.
set -euo pipefail
repo=${OZ_ZDB_SOURCE_ROOT:-$(cd -- "$(dirname -- "$0")/../.." && pwd)}
cd "$repo"
: "${AROS_CC:?}" "${AROS_SDK:?}" "${AROS_TEST_ISO:?}"
: "${AROS_TEST_BASE_DOS_PACKAGE:?}" "${AROS_TEST_POSIX_LIBRARY:?}"
: "${AROS_TEST_ZDB_BINARY:?}" "${AROS_TEST_ZDB_MEMBER:?}" "${AROS_TEST_HOST_ZDB:?}"
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:?}")
if [[ ${OZ_ZDB_RUNNER_SNAPSHOT:-} != "$0" ]];then
    snapshot=$(mktemp -d "$root/zdb-runner.XXXXXXXX")
    cp "$0" "$snapshot/runner.sh"
    exec env OZ_ZDB_SOURCE_ROOT="$repo" OZ_ZDB_RUNNER_SNAPSHOT="$snapshot/runner.sh" \
        bash "$snapshot/runner.sh" "$@"
fi
run=$(mktemp -d "$root/zdb-label-run.XXXXXXXX")
echo "Evidence: $run"
cp "$0" aros/cli/zdb-label-native-test.c aros/cli/Startup-Zdb-Labels "$run/"
sha256sum "$AROS_TEST_ISO" "$AROS_TEST_BASE_DOS_PACKAGE" "$AROS_TEST_POSIX_LIBRARY" \
    "$AROS_TEST_ZDB_BINARY" "$AROS_TEST_ZDB_MEMBER" "$AROS_TEST_HOST_ZDB" >"$run/inputs.sha256"
library_maps=()
test_flags=()
pool_maps=()
if [[ -n ${AROS_TEST_ZDB_LIBRARY:-} ]];then
    : "${AROS_TEST_ZDB_INCLUDE:?matching generated library headers required}"
    sha256sum "$AROS_TEST_ZDB_LIBRARY" >>"$run/inputs.sha256"
    library_maps=(-map "$AROS_TEST_ZDB_LIBRARY" /Libs/openzfs.library)
    test_flags=(-DOZ_ZDB_SHARED)
    "$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -O2 -Wall -Wextra \
        -I"$AROS_TEST_ZDB_INCLUDE" aros/library/zdb-lifetime-test.c \
        -o "$run/OpenZFSZdbLifetimeTest" >"$run/lifetime-build.log" 2>&1
    library_maps+=(-map "$run/OpenZFSZdbLifetimeTest" /C/OpenZFSZdbLifetimeTest)
    printf 'shared_library=1\nlauncher_stack=65536\n' >"$run/mode.txt"
else
    printf 'shared_library=0\nlauncher_stack=4194304\n' >"$run/mode.txt"
fi
if [[ -n ${AROS_TEST_ZDB_POOL_SECOND:-} ]];then
    [[ -n ${AROS_TEST_ZDB_LIBRARY:-} ]] || { echo 'Pool test requires shared debugger' >&2;exit 1; }
    test_flags+=(-DOZ_ZDB_POOL)
    first=$(realpath -e "$AROS_TEST_ZDB_MEMBER")
    second=$(realpath -e "$AROS_TEST_ZDB_POOL_SECOND")
    sha256sum "$second" >>"$run/inputs.sha256"
    mkdir "$run/host-pool"
    ln -s "$first" "$run/host-pool/member0"
    ln -s "$second" "$run/host-pool/member2"
    TZ=UTC "$AROS_TEST_HOST_ZDB" -P -e -p "$run/host-pool" -bcc topo_source >"$run/host-blocks.log" 2>&1
    TZ=UTC "$AROS_TEST_HOST_ZDB" -P -e -p "$run/host-pool" -dddd topo_source >"$run/host-objects.log" 2>&1
    pool_maps=(-map "$first" /zdb-pool/member0 -map "$second" /zdb-pool/member2)
    printf 'pool_traversal=1\n' >>"$run/mode.txt"
fi
"$AROS_TEST_HOST_ZDB" -l "$AROS_TEST_ZDB_MEMBER" >"$run/host-labels.log" 2>&1
grep -q "name: 'topo_source'" "$run/host-labels.log"
grep -q 'labels = 0 1 2 3 ' "$run/host-labels.log"
"$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -O2 -Wall -Wextra \
    "${test_flags[@]}" aros/cli/zdb-label-native-test.c -o "$run/OpenZFSZdbLabelsTest" >"$run/build.log" 2>&1
xorriso -indev "$AROS_TEST_ISO" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$AROS_TEST_BASE_DOS_PACKAGE" /boot/aros-base.pkg.xz \
    -map "$AROS_TEST_POSIX_LIBRARY" /Libs/posixc.library \
    "${library_maps[@]}" \
    "${pool_maps[@]}" \
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
if [[ -n ${AROS_TEST_ZDB_LIBRARY:-} ]];then
    for cycle in 0 1;do
        rg -q "RESULT shared zdb lifecycle PASS cycle=$cycle invalid=5 repeated=10 service=10" "$run/serial.log"
    done
    rg -q 'RESULT shared zdb caller exit PASS status=7' "$run/serial.log"
fi
if [[ -n ${AROS_TEST_ZDB_POOL_SECOND:-} ]];then
    python3 aros/cli/verify-zdb-pool.py "$run" >"$run/pool-comparison.log"
fi
echo 'PASS selected native zdb checks; consult mode.txt and comparison logs for scope.'
