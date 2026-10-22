#!/usr/bin/env bash
# New private runtime and X server only. Never touch another hosted session.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
library=$(realpath -e "${AROS_TEST_SECURITY_LIBRARY:?private hosted security library required}")
service=${AROS_TEST_HOSTED_SERVICE_BUILD:-}
if [[ -n $service ]];then service=$(realpath -e "$service");fi
mode=${AROS_TEST_HOSTED_SERVICE_MODE:-STORAGE}
cli_dir=${AROS_TEST_CLI_DIR:-}
handler=${AROS_TEST_HANDLER:-}
if [[ -n $handler ]];then
    handler=$(realpath -e "$handler")
    [[ -n $service && -f $handler ]]
fi
if [[ -n $cli_dir ]];then
    cli_dir=$(realpath -e "$cli_dir")
    [[ -n $service && -f $cli_dir/zfs && -f $cli_dir/zpool ]]
fi
[[ $mode == STORAGE || $mode == GUI ]]
if [[ $mode == GUI ]];then [[ -n $service ]];fi
persistent=${AROS_TEST_HOSTED_PERSISTENT:-0}
[[ $persistent == 0 || $persistent == 1 ]]
if [[ $persistent == 1 ]];then [[ -n $service ]];fi
assign_fixture=${AROS_TEST_HOSTED_ASSIGN:-0}
[[ $assign_fixture == 0 || $assign_fixture == 1 ]]
if [[ $assign_fixture == 1 ]];then [[ $persistent == 1 ]];fi
sdk=${AROS_SDK:?explicit hosted SDK required}
cc=${AROS_CC:?explicit AROS compiler required}
runtime=$(realpath -e "${AROS_TEST_HOSTED_RUNTIME:-$repo/build-aros/hosted}")
shared=${AROS_TEST_SHARED_BUILD:-}
host_zdb=${AROS_TEST_HOST_ZDB:-$repo/build-control/zdb}
host_libpath=${AROS_TEST_HOST_LIBRARY_PATH:-$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu}
if [[ $persistent == 1 ]];then [[ -x $host_zdb ]];fi
if [[ -n $shared ]];then
    [[ -n $service ]]
    shared=$(realpath -e "$shared")
    rg -Fxq 'service_test_modes=0' "$shared/service-build-options.txt"
    rg -Fxq 'full_dispatcher=1' "$shared/service-build-options.txt"
    rg -Fxq 'shared_engine=1' "$shared/service-build-options.txt"
    sha256sum -c "$shared/binaries.sha256"
    cmp "$shared/engine-object.sha256" "$service/engine-object.sha256"
fi
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/hosted-security-test.XXXXXXXX")
echo "Evidence: $run"
for dir in boot C Classes Devs Fonts L Libs Locale Prefs S System;do
    cp -a --reflink=auto "$runtime/$dir" "$run/$dir"
done
cp "$runtime/AROS.boot" "$run/AROS.boot"
cp -a aros/engine/security-fixture "$run/Security"
cp "$library" "$run/Libs/security.library"
if [[ -n ${AROS_TEST_DOS_LIBRARY:-} ]];then
    cp "$AROS_TEST_DOS_LIBRARY" "$run/Libs/dos.library"
fi
if [[ -n ${AROS_TEST_HOSTED_STDCIO_LIBRARY:-} ]];then
    cp "$AROS_TEST_HOSTED_STDCIO_LIBRARY" "$run/Libs/stdcio.library"
fi
cp aros/engine/hosted-security-test.c "$0" "$run/"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 "$run/hosted-security-test.c" -o "$run/C/OpenZFSSecurityTest"
sed "s|^logfile .*|logfile $run/serial.log|" aros/engine/hosted.conf >"$run/hosted.conf"
printf '\nmodule Libs/security.library\n' >>"$run/hosted.conf"
printf '%s\n' 'FailAt 21' 'Stack 262144' \
    'C:OpenZFSSecurityTest >SYS:security-test.log' \
    'Echo finished >SYS:security-finished.log' 'EndCLI' >"$run/S/Startup-Sequence"
if [[ -n $service ]];then
    rg -Fxq 'service_test_modes=0' "$service/service-build-options.txt"
    rg -Fxq 'full_dispatcher=1' "$service/service-build-options.txt"
    cp "$service/OpenZFSServiceNative" "$service/OpenZFSPublicServiceTest" "$run/C/"
    if [[ -n $shared ]];then
        cp "$shared/OpenZFSServiceNative" "$run/C/"
        cp "$shared/openzfs.library" "$run/Libs/"
    fi
    cp aros/engine/public-service-test.c "$run/"
    if [[ -n $handler ]];then
        cp "$handler" "$run/L/OpenZFS-Handler"
        printf '%s\n' 'Disposable pool handler activation' >"$run/openzfs-handler-package-test"
        printf '%s\n' 'FileSystem = L:OpenZFS-Handler' 'Device = openzfs' \
            'Unit = 0' 'Flags = 0' 'Surfaces = 1' 'BlocksPerTrack = 1' \
            'LowCyl = 0' 'HighCyl = 0' 'DosType = 0x5a465301' \
            'Control = "public_mirror/files"' 'StackSize = 262144' \
            'Activate = 0' >"$run/OZPACKAGE"
    fi
    if [[ -n $cli_dir ]];then
        cp "$cli_dir/zfs" "$cli_dir/zpool" "$run/C/"
        printf '%s\n' 'Actual CLI tests on the disposable public_mirror only' >"$run/openzfs-cli-package-test"
    fi
    if [[ $mode == GUI ]];then cp "$service/OpenZFSManagerNative" "$run/C/";fi
    if [[ -n ${AROS_TEST_RUNTIME_CHECK:-} ]];then
        cp "$AROS_TEST_RUNTIME_CHECK" "$run/C/OpenZFSRuntimeCheck"
    fi
    printf '%s\n' 'Private hosted fixture only' >"$run/openzfs-service-test-only"
    if [[ ${AROS_TEST_LOADER_POLICY:-0} == 1 ]];then
        printf '%s\n' 'Load known ELF but never execute it' >"$run/openzfs-loader-policy-test"
    fi
    if [[ $persistent == 1 ]];then
        printf '%s\n' 'New disposable directory only' >"$run/openzfs-hosted-file-fixture"
        if [[ $assign_fixture == 1 ]];then
            printf '%s\n' 'Test SYS assign without bypassing admission' >"$run/openzfs-hosted-assign-fixture"
        fi
        if [[ $mode == GUI ]];then
            printf '%s\n' 'Cold reader must find actual GUI snapshot' >"$run/openzfs-gui-snapshot-required"
        fi
    fi
    printf '%s\n' 'FailAt 21' 'MakeDir RAM:T RAM:ENV' 'Assign T: RAM:T' \
        'Assign ENV: RAM:ENV' 'Assign ENVARC: SYS:Prefs/Env-Archive' \
        'Assign LIBS: SYS:Classes ADD' 'Assign KEYMAPS: DEVS:Keymaps' \
        'Assign LOCALE: SYS:Locale' 'Assign FONTS: SYS:Fonts' 'Stack 1048576' \
        'C:OpenZFSSecurityTest >SYS:security-test.log' \
        "C:OpenZFSPublicServiceTest $mode >SYS:service-test.log" \
        'If EXISTS T:OpenZFS-Manager.log' 'Copy T:OpenZFS-Manager.log SYS:manager-activity.log' 'EndIf' \
        'If EXISTS T:OpenZFS-Response.txt' 'Copy T:OpenZFS-Response.txt SYS:manager-response.txt' 'EndIf' \
        'Echo finished >SYS:security-finished.log' 'EndCLI' >"$run/S/Startup-Sequence"
fi
sha256sum "$run/Libs/security.library" "$run/boot/linux/kernel" "$run/C/OpenZFSSecurityTest" >"$run/inputs.sha256"
if [[ -n ${AROS_TEST_RUNTIME_CHECK:-} ]];then
    cp "$AROS_TEST_RUNTIME_CHECK" "$run/C/OpenZFSRuntimeCheck"
    sed -i '/Echo finished/i C:OpenZFSRuntimeCheck >SYS:runtime-check.log' "$run/S/Startup-Sequence"
fi
sha256sum "$run/Libs/stdcio.library" >>"$run/inputs.sha256"
if [[ -n ${AROS_TEST_DOS_LIBRARY:-} ]];then
    sha256sum "$run/Libs/dos.library" >>"$run/inputs.sha256"
fi
if [[ -n ${AROS_TEST_RUNTIME_CHECK:-} ]];then
    sha256sum "$run/C/OpenZFSRuntimeCheck" >>"$run/inputs.sha256"
fi
if [[ -n $service ]];then
    sha256sum "$run/C/OpenZFSServiceNative" "$run/C/OpenZFSPublicServiceTest" >>"$run/inputs.sha256"
    if [[ $mode == GUI ]];then sha256sum "$run/C/OpenZFSManagerNative" >>"$run/inputs.sha256";fi
fi
if [[ -n $shared ]];then sha256sum "$run/Libs/openzfs.library" >>"$run/inputs.sha256";fi
if [[ -n $cli_dir ]];then sha256sum "$run/C/zfs" "$run/C/zpool" >>"$run/inputs.sha256";fi
if [[ -n $handler ]];then sha256sum "$run/L/OpenZFS-Handler" >>"$run/inputs.sha256";fi
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
xpid=;guest=
cleanup() {
    if [[ -n $guest ]];then kill -TERM -- "-$guest" 2>/dev/null || true;wait "$guest" || true;guest=;fi
    if [[ -n $xpid ]];then kill "$xpid" 2>/dev/null || true;wait "$xpid" || true;xpid=;fi
}
trap cleanup EXIT
start_guest() {
local phase=$1
Xvfb -displayfd 3 -screen 0 1280x960x24 -nolisten tcp 3>"$run/${phase}display.txt" >"$run/${phase}xvfb.log" 2>&1 &
xpid=$!
for attempt in {1..50};do [[ -s "$run/${phase}display.txt" ]] && break;sleep .1;done
display=$(<"$run/${phase}display.txt");[[ $display =~ ^[0-9]+$ ]]
cd "$run"
DISPLAY=":$display" setsid ./boot/linux/AROSBootstrap -c "$run/${phase}hosted.conf" >"$run/${phase}bootstrap.log" 2>&1 &
guest=$!;printf '%s\n' "$guest" >"$run/${phase}guest.pid"
}
start_guest ''
deadline=$((SECONDS+300))
if [[ $mode == GUI ]];then deadline=$((SECONDS+900));fi
while [[ ! -f "$run/security-finished.log" ]];do
    if ! kill -0 "$guest" 2>/dev/null || ((SECONDS>=deadline));then
        echo 'FAIL private hosted security exit/deadline' >&2;exit 1
    fi
    sleep 1
done
cat "$run/security-test.log"
if [[ -n ${AROS_TEST_RUNTIME_CHECK:-} ]];then
    expected=${AROS_TEST_RUNTIME_EXPECT:-PASS}
    [[ $expected == PASS || $expected == FAIL ]]
    cat "$run/runtime-check.log"
    rg -q "^$expected .*runtime prerequisites" "$run/runtime-check.log"
fi
rg -q '^RESULT hosted security login/logout PASS configured=1 uid77gid88=1 restored=1' "$run/security-test.log"
if [[ -n $service ]];then
    rg -q 'RESULT public service storage PASS' "$run/serial.log"
    if [[ -n $cli_dir ]];then
        rg -q 'Public packaged CLI PASS' "$run/serial.log"
        rg -q 'Public packaged replication PASS .*incremental=1' "$run/serial.log"
    fi
    if [[ -n $handler ]];then rg -q 'Public packaged handler PASS' "$run/serial.log";fi
fi
if [[ $persistent == 1 ]];then
    # The writer has reported orderly export. Stop only this private process
    # before inspecting files, and preserve the creator's startup and logs.
    cleanup
    members=("$run/OZPublicPool/disk0" "$run/OZPublicPool/disk1")
    for member in "${members[@]}";do [[ -f $member && $(stat -c %s "$member") == 134217728 ]];done
    sha256sum "${members[@]}" >"$run/before-cold.sha256"
    LD_LIBRARY_PATH="$host_libpath" \
        "$host_zdb" -e -p "$run/OZPublicPool" -bcc public_mirror >"$run/zdb-check.log" 2>&1
    rg -q 'No leaks' "$run/zdb-check.log"
    if [[ $mode == GUI ]];then
        LD_LIBRARY_PATH="$host_libpath" \
            "$host_zdb" -e -p "$run/OZPublicPool" -dddd public_mirror/files@hosted_gui >"$run/zdb-snapshot.log" 2>&1
        rg -q '^Dataset public_mirror/files@hosted_gui ' "$run/zdb-snapshot.log"
    fi
    cp "$run/S/Startup-Sequence" "$run/creator-startup"
    sed 's/OpenZFSPublicServiceTest \(STORAGE\|GUI\)/OpenZFSPublicServiceTest STORAGE-COLD/; s/security-finished.log/cold-finished.log/; s/service-test.log/cold-service-test.log/; s/security-test.log/cold-security-test.log/' \
        "$run/creator-startup" >"$run/S/Startup-Sequence"
    sed "s|^logfile .*|logfile $run/cold-serial.log|" "$run/hosted.conf" >"$run/cold-hosted.conf"
    start_guest cold-
    deadline=$((SECONDS+300))
    while [[ ! -f "$run/cold-finished.log" ]];do
        if ! kill -0 "$guest" 2>/dev/null || ((SECONDS>=deadline));then
            echo 'FAIL private hosted cold reader exit/deadline' >&2;exit 1
        fi
        sleep 1
    done
    rg -q 'RESULT public service storage PASS' "$run/cold-serial.log"
    rg -q 'Public cold hosted DOS PASS exact=1 readonly-refusal=1 exported=1' "$run/cold-serial.log"
    rg -q 'Public cold degraded assign PASS missing=1 exact=1 readonly=1 restored=1' "$run/cold-serial.log"
    if [[ $mode == GUI ]];then rg -q 'Public cold GUI snapshot retained PASS' "$run/cold-serial.log";fi
    cleanup
    sha256sum -c "$run/before-cold.sha256" | tee "$run/after-cold-verified.log"
    echo 'PASS persistent hosted mirror: independent zdb, cold DOS read and unchanged files'
fi
sha256sum -c "$run/inputs.sha256"
echo "PASS private hosted authentication: $run"
