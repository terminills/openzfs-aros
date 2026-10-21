#!/usr/bin/env bash
# Private read-only ISO; no user disks, network, DUT or hosted session.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
probe=${1:?compile probe directory required}
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
run=$(mktemp -d "$repo/build-aros/cli-platform.XXXXXXXX")
environment_test=environment-test.c
environment_pass='RESULT native CLI environment PASS runs=16'
if [[ ${AROS_TEST_ENV_UNSUPPORTED:-0} == 1 ]]; then
    environment_test=environment-unsupported-test.c
    environment_pass='RESULT native CLI environment PASS unsupported runs=16'
fi
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
extra_maps=()
if [[ -n ${AROS_TEST_PIPE_OVERLAY:-} ]]; then
    cp aros/cli/pipe-test.c "$run/"
    cp "$AROS_TEST_PIPE_OVERLAY/stubs/posixc_pipe2_stub.c" "$run/"
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
        "$run/pipe-test.c" "$run/posixc_pipe2_stub.c" -o "$run/OpenZFSPipeTest"
    sha256sum "$run/pipe-test.c" "$run/posixc_pipe2_stub.c" "$run/OpenZFSPipeTest" > "$run/pipe.sha256"
    extra_maps+=(-map "$run/OpenZFSPipeTest" /C/OpenZFSPipeTest)
fi
cp aros/cli/exec-helper.c "$run/"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    -nix -DOZ_EXEC_TEST_DIAGNOSTICS "$run/exec-helper.c" -o "$run/ExecHelper"
extra_maps+=(-map "$run/ExecHelper" /Tools/OpenZFS/ExecHelper)
if [[ -n ${AROS_TEST_PROCESS_BINARY:-} ]]; then
    cp "$AROS_TEST_PROCESS_BINARY" "$run/OpenZFSUpstreamProcessTest"
    sha256sum "$run/OpenZFSUpstreamProcessTest" "$run/ExecHelper" > "$run/upstream-process.sha256"
    extra_maps+=(-map "$run/OpenZFSUpstreamProcessTest" /C/OpenZFSUpstreamProcessTest)
fi
if [[ -n ${AROS_TEST_CONTROL_BINARY:-} ]]; then
    cp "$AROS_TEST_CONTROL_BINARY" "$run/OpenZFSControlFileTest"
    sha256sum "$run/OpenZFSControlFileTest" > "$run/control-file.sha256"
    extra_maps+=(-map "$run/OpenZFSControlFileTest" /C/OpenZFSControlFileTest)
fi
if [[ -n ${AROS_TEST_IOCTL_COPY_BINARY:-} ]]; then
    cp "$AROS_TEST_IOCTL_COPY_BINARY" "$run/OpenZFSIoctlCopyTest"
    sha256sum "$run/OpenZFSIoctlCopyTest" > "$run/ioctl-copy.sha256"
    extra_maps+=(-map "$run/OpenZFSIoctlCopyTest" /C/OpenZFSIoctlCopyTest)
fi
if [[ -n ${AROS_TEST_MOUNT_REFERENCE_BINARY:-} ]]; then
    cp "$AROS_TEST_MOUNT_REFERENCE_BINARY" "$run/OpenZFSMountReferenceTest"
    sha256sum "$run/OpenZFSMountReferenceTest" > "$run/mount-reference.sha256"
    extra_maps+=(-map "$run/OpenZFSMountReferenceTest" /C/OpenZFSMountReferenceTest)
fi
if [[ -n ${AROS_TEST_SESSION_BINARY:-} ]]; then
    cp "$AROS_TEST_SESSION_BINARY" "$run/OpenZFSSessionCleanupTest"
    sha256sum "$run/OpenZFSSessionCleanupTest" > "$run/session-cleanup.sha256"
    extra_maps+=(-map "$run/OpenZFSSessionCleanupTest" /C/OpenZFSSessionCleanupTest)
fi
if [[ -n ${AROS_TEST_STDCIO_LIBRARY:-} ]]; then
    cp "$AROS_TEST_STDCIO_LIBRARY" "$run/stdcio.library"
    sha256sum "$run/stdcio.library" > "$run/stdcio.sha256"
    extra_maps+=(-map "$run/stdcio.library" /Libs/stdcio.library)
fi
if [[ -n ${AROS_TEST_COMPAT_BINARY:-} ]]; then
    cp "$AROS_TEST_COMPAT_BINARY" "$run/OpenZFSCompatibilityTest"
    sha256sum "$run/OpenZFSCompatibilityTest" > "$run/compatibility.sha256"
    extra_maps+=(-map "$run/OpenZFSCompatibilityTest" /C/OpenZFSCompatibilityTest)
    cp -r aros/cli/compatibility-fixtures "$run/"
    sha256sum "$run/compatibility-fixtures/"* >> "$run/compatibility.sha256"
    extra_maps+=(-map "$run/compatibility-fixtures" /Storage/OpenZFS/compatibility.d)
fi
cp "$probe/cmd_zpool_zpool_util.c.o" "$run/zpool_util.o"
cp "$probe/metadata.h" "$probe/metadata.sha256" "$probe/META" "$run/"
expected_version=$(awk '/^#define ZFS_META_ALIAS / {gsub(/"/, "", $3); print $3}' "$run/metadata.h")
[[ -n $expected_version ]]
cp aros/cli/nls-test.c aros/cli/Startup-Platform-Test "$run/"
cp aros/cli/test-qemu-platform.sh aros/cli/probe-upstream.sh "$run/"
cp aros/cli/arguments.c aros/cli/arguments.h aros/cli/arguments-test.c aros/cli/arguments-child.c "$run/"
cp aros/cli/process.c aros/cli/process.h aros/cli/process-test.c "$run/"
cp aros/cli/output.c aros/cli/output.h aros/cli/output-test.c aros/cli/output-child.c "$run/"
cp aros/cli/environment-test.c aros/cli/environment-child.c "$run/"
cp "aros/cli/$environment_test" "$run/"
cp -r aros/cli/include "$run/"
cp lib/libspl/include/unistd.h "$run/include/"
cp aros/cli/positioned-io-test.c "$run/"
cp aros/cli/progress-wait.c aros/cli/progress-wait.h aros/cli/progress-wait-test.c "$run/"
cp aros/cli/title.c aros/cli/title.h aros/cli/title-test.c "$run/"
cp cmd/zpool/zpool_util.c "$run/"
cp "$probe/flags.txt" "$probe/sources.sha256" "$run/"
git rev-parse HEAD > "$run/source-head.txt"
git diff --binary > "$run/source.patch"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -I"$run/include" \
    -include "$run/metadata.h" \
    -c "$run/nls-test.c" -o "$run/test.o"
"${cc%gcc}ld" -r --gc-sections -u main "$run/test.o" "$run/zpool_util.o" -o "$run/selected.o"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" "$run/selected.o" -o "$run/OpenZFSCLIPlatformTest"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    "$run/arguments.c" "$run/arguments-test.c" -o "$run/OpenZFSArgumentsTest"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    "$run/arguments-child.c" -o "$run/OpenZFSArgvChild"
sha256sum "$run/OpenZFSArgumentsTest" "$run/OpenZFSArgvChild" > "$run/arguments.sha256"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    "$run/arguments.c" "$run/process.c" "$run/output.c" "$run/process-test.c" -o "$run/OpenZFSProcessTest"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    "$run/arguments.c" "$run/process.c" "$run/output.c" "$run/output-test.c" -o "$run/OpenZFSOutputTest"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    "$run/output-child.c" -o "$run/OpenZFSOutputChild"
sha256sum "$run/OpenZFSOutputTest" "$run/OpenZFSOutputChild" > "$run/output.sha256"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    "$run/arguments.c" "$run/process.c" "$run/output.c" "$run/$environment_test" -o "$run/OpenZFSEnvironmentTest"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    "$run/arguments.c" "$run/process.c" "$run/output.c" "$run/environment-child.c" -o "$run/OpenZFSEnvironmentChild"
sha256sum "$run/OpenZFSEnvironmentTest" "$run/OpenZFSEnvironmentChild" > "$run/environment.sha256"
sha256sum "$run/OpenZFSProcessTest" > "$run/process.sha256"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    -I"$run/include" "$run/positioned-io-test.c" -o "$run/OpenZFSPositionedIOTest"
sha256sum "$run/OpenZFSPositionedIOTest" "$run/include/unistd.h" > "$run/positioned-io.sha256"
pthread_include=${AROS_PTHREAD_INCLUDE:-$repo/build-aros/pthread-consistent.s3r1CHla/source}
pthread_archive=${AROS_PRIVATE_PTHREAD:-$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a}
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    -I"$pthread_include" "$run/progress-wait.c" "$run/progress-wait-test.c" \
    "$pthread_archive" -o "$run/OpenZFSProgressWaitTest"
sha256sum "$pthread_include/pthread.h" "$pthread_archive" "$run/OpenZFSProgressWaitTest" > "$run/progress-wait.sha256"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -Wall -Wextra -Werror \
    -I"$pthread_include" "$run/title.c" "$run/title-test.c" \
    "$pthread_archive" -o "$run/OpenZFSTitleTest"
sha256sum "$run/title.c" "$run/title-test.c" "$run/OpenZFSTitleTest" > "$run/title.sha256"
sha256sum "$base" "$run/OpenZFSCLIPlatformTest" "$dos/aros-base.pkg.xz" \
    "$ata/aros-bsp.pkg.xz" "$posix" > "$run/inputs.sha256"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    "${extra_maps[@]}" \
    -map "$run/OpenZFSCLIPlatformTest" /C/OpenZFSCLIPlatformTest \
    -map "$run/OpenZFSArgumentsTest" /C/OpenZFSArgumentsTest \
    -map "$run/OpenZFSArgvChild" /C/OpenZFSArgvChild \
    -map "$run/OpenZFSProcessTest" /C/OpenZFSProcessTest \
    -map "$run/OpenZFSOutputTest" /C/OpenZFSOutputTest \
    -map "$run/OpenZFSOutputChild" /C/OpenZFSOutputChild \
    -map "$run/OpenZFSEnvironmentTest" /C/OpenZFSEnvironmentTest \
    -map "$run/OpenZFSEnvironmentChild" /C/OpenZFSEnvironmentChild \
    -map "$run/OpenZFSPositionedIOTest" /C/OpenZFSPositionedIOTest \
    -map "$run/OpenZFSProgressWaitTest" /C/OpenZFSProgressWaitTest \
    -map "$run/OpenZFSTitleTest" /C/OpenZFSTitleTest \
    -map "$run/Startup-Platform-Test" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-cli-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" \
    > "$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+180))
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -q 'RESULT native (CLI|control files) .*FAIL|\[PF-DBG\]|ASSERT at|went out of stack limits' "$run/serial.log";then exit 1;fi
        if rg -q 'RESULT native CLI platform PASS\r?$' "$run/serial.log" &&
           rg -Fq "CLI upstream userland version=$expected_version" "$run/serial.log" &&
           rg -q 'RESULT native CLI arguments PASS\r?$' "$run/serial.log" &&
           rg -q 'RESULT native CLI process PASS runs=16\r?$' "$run/serial.log" &&
           rg -q 'RESULT native CLI output PASS\r?$' "$run/serial.log" &&
           rg -q 'RESULT native CLI capture PASS runs=16 cleanup=1\r?$' "$run/serial.log" &&
           { [[ -z ${AROS_TEST_PROCESS_BINARY:-} ]] ||
             rg -q 'RESULT native CLI upstream process PASS runs=8 search=1 exact=1 status=1\r?$' "$run/serial.log"; } &&
           rg -q 'RESULT native CLI positioned IO PASS\r?$' "$run/serial.log" &&
           rg -q 'RESULT native CLI progress wait PASS runs=16\r?$' "$run/serial.log" &&
           rg -q 'RESULT native CLI title PASS runs=16 worker=1 bounds=1 restore=1' "$run/serial.log" &&
           { [[ -z ${AROS_TEST_PIPE_OVERLAY:-} ]] ||
             rg -q 'RESULT native CLI pipe PASS runs=64 flags=1 io=1 eof=1 errors=1' "$run/serial.log"; } &&
           rg -q "${environment_pass}"'\r?$' "$run/serial.log" &&
           { [[ -z ${AROS_TEST_IOCTL_COPY_BINARY:-} ]] ||
             rg -q 'RESULT native CLI ioctl copy PASS threads=16\r?$' "$run/serial.log"; } &&
           { [[ -z ${AROS_TEST_MOUNT_REFERENCE_BINARY:-} ]] ||
             rg -q 'RESULT native CLI mount refs PASS runs=64 threads=16\r?$' "$run/serial.log"; } &&
           { [[ -z ${AROS_TEST_CONTROL_BINARY:-} ]] ||
             rg -q 'RESULT native control files PASS cycles=16 callbacks=16 events=16 retained=1 threads=16 minors=144\r?$' "$run/serial.log"; } &&
           { [[ -z ${AROS_TEST_SESSION_BINARY:-} ]] ||
             rg -q 'RESULT native CLI session cleanup PASS runs=64 threads=16 callbacks=144\r?$' "$run/serial.log"; } &&
           { [[ -z ${AROS_TEST_COMPAT_BINARY:-} ]] ||
             rg -q 'RESULT native CLI compatibility PASS runs=16\r?$' "$run/serial.log"; };then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then
        echo "Timeout/exit: $run" >&2;exit 1
    fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
echo "PASS: native CLI platform fixture. Evidence: $run"
