#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA required}
bundle=${AROS_TEST_STARTUP_BUNDLE:-}
qemu=$(command -v "${AROS_TEST_QEMU:-qemu-system-x86_64}")
run=$(mktemp -d "$artifact_root/startup-cold.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
cp "$0" aros/ui/startup-cold-test.c aros/ui/OZBOOT aros/ui/Startup-Cold-* "$run/"
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
flags=();maps=();drives=();gui_devices=()
if [[ -n ${AROS_TEST_MALFORMED_KEY_CYCLES:-} ]];then
    cycles=$AROS_TEST_MALFORMED_KEY_CYCLES
    [[ ${AROS_TEST_ENCRYPTED_CREATE_GUI:-0} == 1 && $cycles =~ ^[1-9][0-9]{0,2}$ && $cycles -le 128 ]]
    flags+=("-DOZ_MALFORMED_KEY_CYCLES=$cycles")
fi
if [[ -n ${AROS_TEST_CLI_LAUNCH_CYCLES:-} ]];then
    cycles=$AROS_TEST_CLI_LAUNCH_CYCLES
    [[ -n $bundle && $cycles =~ ^[1-9][0-9]{0,3}$ && $cycles -le 1024 ]]
    flags+=("-DOZ_CLI_LAUNCH_CYCLES=$cycles")
fi
if [[ ${AROS_TEST_KEY_INHERIT_GUI:-0} == 1 ]];then
    [[ ${AROS_TEST_ENCRYPTED_CREATE_GUI:-0} == 1 && -n $bundle ]]
    flags+=(-DOZ_KEY_INHERIT_GUI)
fi
if [[ ${AROS_TEST_KEY_PROMPT_GUI:-0} == 1 ]];then
    [[ ${AROS_TEST_ENCRYPTED_CREATE_GUI:-0} == 1 && -n $bundle ]]
    flags+=(-DOZ_KEY_PROMPT_GUI)
fi
if [[ ${AROS_TEST_STARTUP_MMP:-0} == 1 ]];then
    [[ ${AROS_TEST_STARTUP_HOSTID:-0} == 1 && ${AROS_TEST_STARTUP_DEFAULT_CACHE:-0} == 1 ]]
    flags+=(-DOZ_STARTUP_MMP)
fi
if [[ ${AROS_TEST_STARTUP_HOSTID:-0} == 1 ]];then
    [[ -n $bundle ]]
    flags+=(-DOZ_STARTUP_HOSTID)
    [[ -f $bundle/C/OpenZFSHostIdentity ]]
    sha256sum "$bundle/C/OpenZFSHostIdentity" >"$run/hostid-inputs.sha256"
    maps+=(-map "$bundle/C/OpenZFSHostIdentity" /C/OpenZFSHostIdentity)
fi
if [[ ${AROS_TEST_STARTUP_HOSTID_EMPTY:-0} == 1 ]];then
    [[ ${AROS_TEST_STARTUP_HOSTID:-0} == 1 ]]
    flags+=(-DOZ_STARTUP_HOSTID_EMPTY)
fi
timeout_seconds=${AROS_TEST_TIMEOUT_SECONDS:-180}
[[ $timeout_seconds =~ ^[0-9]+$ && $timeout_seconds -gt 0 ]]
if [[ ${AROS_TEST_ENCRYPTED_CREATE_GUI:-0} == 1 ]];then
    [[ -n $bundle && ${AROS_TEST_STARTUP_HANDLER:-0} == 1 ]]
    flags+=(-DOZ_ENCRYPTED_CREATE_GUI)
    case ${AROS_TEST_ENCRYPTED_KEY_FORMAT:-passphrase} in
        passphrase) flags+=(-DOZ_ENCRYPTED_KEY_FORMAT=0) ;;
        raw) flags+=(-DOZ_ENCRYPTED_KEY_FORMAT=1) ;;
        hex) flags+=(-DOZ_ENCRYPTED_KEY_FORMAT=2) ;;
        *) echo 'Invalid encrypted key format' >&2;exit 2 ;;
    esac
    gui_devices+=(-usb -device usb-tablet)
    manager=${AROS_TEST_MANAGER_BINARY:-$bundle/C/OpenZFSManagerNative}
    cp "$manager" "$run/OpenZFSManagerNative"
    sha256sum "$run/OpenZFSManagerNative" >"$run/manager.sha256"
    maps+=(-map "$run/OpenZFSManagerNative" /C/OpenZFSManagerNative)
fi
if [[ ${AROS_TEST_STARTUP_HOSTID_GUI:-0} == 1 ]];then
    [[ ${AROS_TEST_STARTUP_HOSTID:-0} == 1 && ${AROS_TEST_STARTUP_HOSTID_EMPTY:-0} != 1 && ${AROS_TEST_STARTUP_CACHE_GUI:-0} != 1 ]]
    flags+=(-DOZ_STARTUP_HOSTID_GUI)
    gui_devices+=(-usb -device usb-tablet)
    maps+=(-map "$bundle/C/OpenZFSManagerNative" /C/OpenZFSManagerNative)
fi
if [[ ${AROS_TEST_STARTUP_CACHE_GUI:-0} == 1 ]];then
    [[ ${AROS_TEST_STARTUP_DEFAULT_CACHE:-0} == 1 && -n $bundle ]]
    flags+=(-DOZ_STARTUP_CACHE_GUI)
    gui_devices+=(-usb -device usb-tablet)
    maps+=(-map "$bundle/C/OpenZFSManagerNative" /C/OpenZFSManagerNative)
fi
if [[ ${AROS_TEST_STARTUP_DEFAULT_CACHE:-0} == 1 ]];then
    [[ ${AROS_TEST_STARTUP_CACHE:-0} == 1 ]]
    flags+=(-DOZ_STARTUP_DEFAULT_CACHE)
fi
if [[ ${AROS_TEST_STARTUP_CACHE:-0} == 1 ]];then
    [[ -n $bundle ]]
    flags+=(-DOZ_STARTUP_COLD_CACHE)
fi
if [[ ${AROS_TEST_STARTUP_HANDLER:-0} == 1 ]];then
    [[ -n $bundle && -f $bundle/L/OpenZFS-Handler ]]
    flags+=(-DOZ_STARTUP_COLD_HANDLER)
    cp "$bundle/L/OpenZFS-Handler" "$run/OpenZFS-Handler"
    sha256sum "$run/OpenZFS-Handler" >"$run/handler.sha256"
    maps+=(-map "$run/OpenZFS-Handler" /L/OpenZFS-Handler)
fi
if [[ ${AROS_TEST_STARTUP_COPY:-0} == 1 ]];then
    [[ -n $bundle ]]
    flags+=(-DOZ_STARTUP_COLD_COPY)
fi
if [[ -n $bundle ]];then
    bundle=$(realpath -e "$bundle")
    fixture=$(realpath -e "${AROS_TEST_NATIVE_FIXTURE:?private fixture required}")
    case "$bundle" in "$repo"/build-aros/native-bundle.*|"$artifact_root"/native-bundle.*) ;; *) exit 2;; esac
    case "$fixture" in "$repo"/build-aros/qemu-run.*) ;; *) exit 2;; esac
    (cd "$bundle" && sha256sum -c SHA256SUMS)
    flags+=(-DOZ_STARTUP_COLD_POOL)
    for cmd in OpenZFSServiceNative OpenZFSStartup zfs zpool;do
        command_source="$bundle/C/$cmd"
        if [[ $cmd == zfs && -n ${AROS_TEST_ZFS_BINARY:-} ]];then
            cp "$AROS_TEST_ZFS_BINARY" "$run/zfs"
            sha256sum "$run/zfs" >"$run/zfs-override.sha256"
            command_source="$run/zfs"
        fi
        maps+=(-map "$command_source" "/C/$cmd")
    done
    if rg -Fxq 'shared_engine=1' "$bundle/Metadata/service-build-options.txt";then
        cp "$bundle/Libs/openzfs.library" "$run/openzfs.library"
        sha256sum "$run/openzfs.library" >"$run/engine-library.sha256"
        maps+=(-map "$run/openzfs.library" /Libs/openzfs.library)
    fi
    maps+=(-map "${AROS_TEST_POSIX_LIBRARY:?private POSIX required}" /Libs/posixc.library
        -map "${AROS_TEST_PARTITION_LIBRARY:?private partition library required}" /Libs/partition.library
        -map "${AROS_TEST_FS_PACKAGE:?private FS package required}" /boot/aros-fs.pkg.xz)
    if [[ -n ${AROS_TEST_STDCIO_LIBRARY:-} ]];then
        maps+=(-map "$AROS_TEST_STDCIO_LIBRARY" /Libs/stdcio.library)
        sha256sum "$AROS_TEST_STDCIO_LIBRARY" >"$run/stdcio.sha256"
    fi
    if [[ -n ${AROS_TEST_PIPE_POLICY_HANDLER:-} ]];then
        maps+=(-map "$AROS_TEST_PIPE_POLICY_HANDLER" /L/pipe-handler)
        sha256sum "$AROS_TEST_PIPE_POLICY_HANDLER" >"$run/pipe.sha256"
    fi
    sha256sum "$fixture/disk0.raw" "$fixture/disk1.raw" >"$run/backing.sha256"
    for i in 0 1;do
        qemu-img create -f qcow2 -F raw -b "$fixture/disk$i.raw" "$run/member$i.qcow2"
        drives+=(-drive "file=$run/member$i.qcow2,format=qcow2,if=ide,index=$((i*2+1))")
    done
    sha256sum "$qemu" "$AROS_TEST_POSIX_LIBRARY" "$AROS_TEST_PARTITION_LIBRARY" "$AROS_TEST_FS_PACKAGE" >"$run/pool-inputs.sha256"
    cp "$bundle/SHA256SUMS" "$run/bundle.sha256"
fi
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra \
    "${flags[@]}" aros/ui/startup-cold-test.c aros/engine/hostid-record.c aros/ui/hostid-settings.c aros/ui/admission-client.c aros/ui/startup-settings.c aros/ui/startup-settings-file.c \
    aros/ui/preferences.c aros/ui/preferences-file.c -liffparse \
    -o "$run/OpenZFSStartupColdTest" >"$run/build.log" 2>&1
sha256sum "$base" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" \
    "${sdk%/Developer}/L/sfs-handler" "$run/OpenZFSStartupColdTest" \
    aros/ui/startup-settings.c aros/ui/startup-settings-file.c aros/ui/startup-settings.h \
    aros/engine/hostid-record.c aros/engine/hostid-record.h \
    aros/ui/hostid-settings.c aros/ui/hostid-settings.h \
    aros/ui/preferences.c aros/ui/preferences-file.c >"$run/inputs.sha256"
# Only this freshly created regular file is ever formatted by the guest.
truncate -s 32M "$run/archive.raw"
pid=
cleanup(){ if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
for phase in Create Read;do
    xorriso -indev "$base" -outdev "$run/$phase.iso" \
        -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
        -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
        -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
        -map "${sdk%/Developer}/L/sfs-handler" /L/sfs-handler \
        -map aros/engine/security-fixture /Security \
        -map "$run/OpenZFSStartupColdTest" /C/OpenZFSStartupColdTest \
        -map "$run/OZBOOT" /OZBOOT \
        "${maps[@]}" \
        -map "$run/Startup-Cold-$phase" /S/Startup-Sequence \
        -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/$phase-pack.log" 2>&1
    "$qemu" -name "openzfs-startup-$phase-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
        -drive "file=$run/$phase.iso,media=cdrom,if=ide,index=2,readonly=on" \
        -drive "file=$run/archive.raw,format=raw,if=ide,index=0" \
        "${drives[@]}" \
        -display none -nic none -boot d -no-reboot -no-shutdown "${gui_devices[@]}" \
        -serial "file:$run/$phase-serial.log" -monitor "unix:$run/$phase.monitor,server=on,wait=off" \
        -qmp "unix:$run/$phase.qmp,server=on,wait=off" \
        >"$run/$phase-qemu.log" 2>&1 &
    pid=$!;deadline=$((SECONDS+timeout_seconds))
    while :;do
        if [[ -f "$run/$phase-serial.log" ]];then
            if rg -q '(^|[|] )RESULT .*FAIL|\[PF-DBG\]|(^|[|] )ASSERT at|\[STDC:SIGNAL\].*default action=terminate' "$run/$phase-serial.log";then exit 1;fi
            if rg -q "(^|[|] )RESULT native startup cold ${phase^^} PASS" "$run/$phase-serial.log";then break;fi
        fi
        if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "FAIL timeout/exit: $phase";exit 1;fi
        sleep 1
    done
    printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >"$run/$phase-monitor.log"
    wait "$pid";pid=
    sha256sum "$run/archive.raw" >"$run/$phase-disk.sha256"
    if [[ -n $bundle ]];then sha256sum "$run/member0.qcow2" "$run/member1.qcow2" >"$run/$phase-members.sha256";fi
done
if [[ ${AROS_TEST_STARTUP_HOSTID:-0} == 1 ]];then
    if [[ ${AROS_TEST_STARTUP_HOSTID_GUI:-0} == 1 ]];then
        rg -q '(^|[|] )RESULT native hostid GUI PASS saved=1 current=1' "$run/Create-serial.log"
    fi
    if [[ ${AROS_TEST_STARTUP_HOSTID_EMPTY:-0} == 1 ]];then
        for phase in Create Read;do
            rg -q '(^|[|] )RESULT native unprovisioned hostid PASS id=0 environment-ignored=1 no-files-created=1' "$run/$phase-serial.log"
        done
    else
    rg -q '(^|[|] )RESULT native hostid provisioning PASS saved=1 current=1 zero-refused=1' "$run/Create-serial.log"
    rg -q '(^|[|] )RESULT native hostid CLI PASS invalid=1 save-required=1 saved=1' "$run/Create-serial.log"
    rg -q '(^|[|] )RESULT native hostid authorization PASS ordinary-user=1 cli=1 unchanged=1' "$run/Create-serial.log"
    rg -q '(^|[|] )RESULT native hostid partial save PASS ENVARC-new=1 ENV-old=1 pending-preserved=1 recovered=1' "$run/Create-serial.log"
    rg -q '(^|[|] )RESULT native hostid transaction rejection PASS pending=1 recovered=1' "$run/Create-serial.log"
    rg -q '(^|[|] )RESULT native hostid rejection PASS malformed=1 version=1 zero=1 foreign-owner=1 unchanged=1 ports-absent=1' "$run/Create-serial.log"
    for phase in Create Read;do
        rg -q '(^|[|] )RESULT native persistent hostid PASS id=51a20519 environment-ignored=1' "$run/$phase-serial.log"
    done
    fi
fi
if [[ ${AROS_TEST_STARTUP_MMP:-0} == 1 ]];then
    if [[ ${AROS_TEST_STARTUP_HOSTID_EMPTY:-0} == 1 ]];then
    rg -q '(^|[|] )RESULT native MMP missing hostid PASS CLI-refused=1 explicit-diagnostic=1 property-off=1' "$run/Create-serial.log"
    else
    rg -q '(^|[|] )RESULT native MMP heartbeat PASS enabled=1 label-sequence-advanced=1 disabled=1' "$run/Create-serial.log"
    fi
fi
if [[ ${AROS_TEST_STARTUP_DEFAULT_CACHE:-0} == 1 ]];then
    # readonly protects pool members, not the host's cache metadata. Import
    # with cachefile=none removes its old default cache on this SFS archive.
    # Require that specific guest observation; member hashes remain strict.
    rg -q '(^|[|] )RESULT native default cache cleanup PASS stale-default-removed=1 pool-readonly=1' "$run/Read-serial.log"
else
    cmp "$run/Create-disk.sha256" "$run/Read-disk.sha256"
fi
if [[ -n $bundle ]];then
    rg -q '(^|[|] )RESULT native startup cold pool PASS' "$run/Read-serial.log"
    if [[ ${AROS_TEST_STARTUP_CACHE:-0} == 1 ]];then
        rg -q '(^|[|] )RESULT native startup cold cache CREATE PASS serialized=1 saved=1' "$run/Create-serial.log"
        rg -q '(^|[|] )RESULT native startup cold cache READ PASS imported=1 readonly=1 exported=1' "$run/Read-serial.log"
        if [[ ${AROS_TEST_STARTUP_DEFAULT_CACHE:-0} == 1 ]];then
            rg -q '(^|[|] )RESULT native default cache PASS import=1 disable=1 reset=1 persisted=1' "$run/Create-serial.log"
            rg -q '(^|[|] )RESULT native default cache create PASS created=1 serialized=1 destroyed=1 cache-removed=1 cleanup=1' "$run/Create-serial.log"
            rg -q '(^|[|] )RESULT native default cache startup PASS not-auto-imported=1' "$run/Read-serial.log"
            rg -q '(^|[|] )RESULT native default cache rejection PASS directory=1 foreign-owner=1 writable-parent=1 unchanged=1 policy-none=1' "$run/Create-serial.log"
            rg -q '(^|[|] )RESULT native default cache softlink PASS rejected=1 target-unchanged=1 retained=1 ELOOP=1' "$run/Read-serial.log"
            if [[ ${AROS_TEST_STARTUP_CACHE_GUI:-0} == 1 ]];then
                rg -q '(^|[|] )RESULT native default cache GUI PASS reset=1 serialized=1' "$run/Create-serial.log"
            fi
        fi
    fi
    if [[ ${AROS_TEST_STARTUP_HANDLER:-0} == 1 ]];then
        rg -q '(^|[|] )RESULT native startup cold handler PASS saved-mountfile=1 imported-unmounted=1 activated=1' "$run/Read-serial.log"
    fi
    if [[ ${AROS_TEST_STARTUP_COPY:-0} == 1 ]];then
        rg -q '(^|[|] )RESULT native startup prepare PASS service-absent-before=1 service-absent-after=1 protected-ENV=1' "$run/Read-serial.log"
        rg -q '(^|[|] )RESULT native startup check PASS valid=1 missing=1 malformed=1 ordinary-user=1 service-absent=1' "$run/Read-serial.log"
        rg -q '(^|[|] )RESULT native startup check damaged PASS disabled=1 nonexistent-device=1 bad-IFF=1 unchanged=1 cleanup=1' "$run/Read-serial.log"
        rg -q '(^|[|] )RESULT native startup prepare rejection PASS malformed=3 ordinary-user=1 ENV-absent=1' "$run/Read-serial.log"
        rg -q '(^|[|] )RESULT native startup cold copy PASS secure-publish=1 stock-copy=1 ENV-import=1' "$run/Read-serial.log"
    fi
    cmp "$run/Create-members.sha256" "$run/Read-members.sha256"
    sha256sum -c "$run/backing.sha256"
fi
trap - EXIT
echo "PASS native protected SFS startup record across two QEMU processes: $run"
