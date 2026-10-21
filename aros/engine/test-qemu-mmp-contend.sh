#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$root/mmp-contend.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
bundle=$(realpath -e "${AROS_TEST_STARTUP_BUNDLE:?bundle required}")
fixture=$(realpath -e "${AROS_TEST_NATIVE_FIXTURE:?fixture required}")
case "$bundle" in "$repo"/build-aros/native-bundle.*|"$root"/native-bundle.*) ;; *) exit 2;; esac
case "$fixture" in "$repo"/build-aros/qemu-run.*) ;; *) exit 2;; esac
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
qemu=$(realpath -e "${AROS_TEST_QEMU:?private QEMU required}")
dos=${AROS_TEST_DOS_OVERLAY:?private DOS required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA required}
cycles=${AROS_TEST_MMP_TYPED_CYCLES:-1}
[[ $cycles =~ ^[1-9][0-9]?$ && $cycles -le 32 ]]
printf 'typed_cycles=%s\nstall=%s\n' "$cycles" "${AROS_TEST_MMP_STALL:-0}" >"$run/test-options.txt"
(cd "$bundle" && sha256sum -c SHA256SUMS)
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
cp "$0" aros/engine/mmp-contend-test.c aros/engine/mmp-raw-property.c aros/engine/mmp-stall-control.py aros/engine/Startup-MMP-* "$run/"
sha256sum "$fixture/disk0.raw" "$fixture/disk1.raw" >"$run/backing.sha256"
for i in 0 1;do
    [[ -f $fixture/disk$i.raw && ! -b $fixture/disk$i.raw ]]
    cp --reflink=auto --sparse=always "$fixture/disk$i.raw" "$run/member$i.raw"
    [[ -f $run/member$i.raw && ! -L $run/member$i.raw ]]
done
AROS_SDK="$sdk" AROS_ENGINE_OBJECT_DIR="$run/objects" \
    AROS_PTHREAD_INCLUDE="$repo/build-aros/pthread-consistent.s3r1CHla/source" \
    bash aros/engine/compile-object.sh aros/engine/mmp-raw-property.c >"$run/raw-build.log" 2>&1
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra -DOZ_MMP_TYPED_CYCLES="$cycles" \
    aros/engine/mmp-contend-test.c aros/engine/hostid-record.c aros/ui/admission-client.c \
    "$run/objects/aros_engine_mmp-raw-property.c.o" \
    "$repo/build-aros/libnvpair-adt-consistent.a" \
    "$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a" \
    -liffparse -o "$run/OpenZFSMMPContendTest" >"$run/build.log" 2>&1
maps=(-map "$run/OpenZFSMMPContendTest" /C/OpenZFSMMPContendTest
    -map aros/engine/security-fixture /Security
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz
    -map "${AROS_TEST_POSIX_LIBRARY:?POSIX required}" /Libs/posixc.library
    -map "${AROS_TEST_STDCIO_LIBRARY:?stdio required}" /Libs/stdcio.library
    -map "${AROS_TEST_PIPE_POLICY_HANDLER:?pipe required}" /L/pipe-handler
    -map "${AROS_TEST_PARTITION_LIBRARY:?partition required}" /Libs/partition.library
    -map "${AROS_TEST_FS_PACKAGE:?FS package required}" /boot/aros-fs.pkg.xz
    -map "$bundle/Libs/openzfs.library" /Libs/openzfs.library)
for command in OpenZFSServiceNative zpool;do maps+=(-map "$bundle/C/$command" "/C/$command");done
sha256sum "$qemu" "$base" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" \
    "$AROS_TEST_POSIX_LIBRARY" "$AROS_TEST_STDCIO_LIBRARY" "$AROS_TEST_PIPE_POLICY_HANDLER" \
    "$AROS_TEST_PARTITION_LIBRARY" "$AROS_TEST_FS_PACKAGE" "$run/OpenZFSMMPContendTest" \
    aros/engine/hostid-record.c aros/ui/admission-client.c \
    aros/engine/compile-object.sh aros/engine/mmp-raw-property.c \
    "$repo/build-aros/libnvpair-adt-consistent.a" \
    "$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a" >"$run/inputs.sha256"
cp "$bundle/SHA256SUMS" "$run/bundle.sha256"
roles=(zeroset owner contender nohost recovery)
if [[ ${AROS_TEST_MMP_STALL:-0} == 1 ]];then roles=(stall);fi
for role in "${roles[@]}";do
    # Match the qualified ATA topology: this driver does not identify the
    # first slave reliably when its master is absent. Never share this disk.
    truncate -s 32M "$run/$role-scratch.raw"
    xorriso -indev "$base" -outdev "$run/$role.iso" "${maps[@]}" \
        -map "aros/engine/Startup-MMP-$role" /S/Startup-Sequence \
        -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/$role-pack.log" 2>&1
done
pids=()
cleanup(){
    if [[ ${AROS_TEST_MMP_STALL:-0} == 1 && -S $run/stall.qmp ]];then
        python3 "$run/mmp-stall-control.py" "$run" release >"$run/stall-release.log" 2>&1 || true
    fi
    for pid in "${pids[@]}";do
        if kill -0 "$pid" 2>/dev/null;then
            kill "$pid"
            for attempt in {1..10};do kill -0 "$pid" 2>/dev/null || break;sleep 1;done
            if kill -0 "$pid" 2>/dev/null;then
                echo "Private QEMU $pid did not exit; forcing exact child termination"
                kill -KILL "$pid"
            fi
            wait "$pid" || true
        fi
    done
}
trap cleanup EXIT
boot(){
    local role=$1
    local file0=member0file file1=member1file
    local extra=()
    if [[ $role == stall ]];then
        extra=(-qmp "unix:$run/stall.qmp,server=on,wait=off")
    fi
    # Only these mktemp-owned COPIES are shared. Raw file locking is disabled
    # deliberately; direct I/O prevents separate QEMU block caches hiding MMP.
    "$qemu" -name "openzfs-mmp-$role-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
        -drive "file=$run/$role.iso,media=cdrom,if=ide,index=2,readonly=on" \
        -drive "file=$run/$role-scratch.raw,format=raw,if=ide,index=0" \
        -blockdev "driver=file,filename=$run/member0.raw,node-name=member0file,locking=off,cache.direct=on" \
        -blockdev "driver=file,filename=$run/member1.raw,node-name=member1file,locking=off,cache.direct=on" \
        "${extra[@]}" \
        -blockdev "driver=raw,file=$file0,node-name=member0" \
        -blockdev "driver=raw,file=$file1,node-name=member1" \
        -device ide-hd,id=mmpdisk0,drive=member0,bus=ide.0,unit=1 \
        -device ide-hd,id=mmpdisk1,drive=member1,bus=ide.1,unit=1 \
        -display none -nic none -boot d -no-reboot -no-shutdown \
        -serial "file:$run/$role-serial.log" -monitor "unix:$run/$role.monitor,server=on,wait=off" \
        >"$run/$role-qemu.log" 2>&1 &
    pids+=("$!")
}
wait_marker(){
    local role=$1 marker=$2 pid=$3 deadline=$((SECONDS+240))
    while :;do
        for log in "$run/owner-serial.log" "$run/$role-serial.log";do
            if [[ -f $log ]] && rg -q 'RESULT .*FAIL|\[PF-DBG\]|ASSERT at|\[STDC:SIGNAL\].*default action=terminate' "$log";then return 1;fi
        done
        if [[ -f $run/$role-serial.log ]] && rg -Fq "$marker" "$run/$role-serial.log";then return 0;fi
        if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "FAIL timeout/exit: $role";return 1;fi
        sleep 1
    done
}
if [[ ${AROS_TEST_MMP_STALL:-0} == 1 ]];then
    boot stall
    wait_marker stall 'RESULT native MMP OWNER READY hostid=51a20519' "${pids[0]}"
    python3 "$run/mmp-stall-control.py" "$run" >"$run/stall-control.log" 2>&1
    wait_marker stall 'RESULT native MMP STALL PASS pool-suspended=1' "${pids[0]}"
    # The engine's CE_WARN is sent to service stderr, not necessarily serial.
    # Require the distinct MMP suspension reason from upstream pool status.
    rg -q 'pool is suspended because multihost writes failed or were delayed' "$run/stall-serial.log"
    rg -Fq 'WRITE THROTTLES ARMED 64 B/s' "$run/stall-control.log"
    python3 "$run/mmp-stall-control.py" "$run" release >"$run/stall-recovery-control.log" 2>&1
    wait_marker stall 'RESULT native MMP STALL RECOVERY PASS cleared=1 synced=1 online=1 multihost=on exported=1' "${pids[0]}"
    sha256sum -c "$run/backing.sha256"
    echo "PASS native MMP write-stall suspension and clear recovery: $run"
    exit 0
fi
boot zeroset
wait_marker zeroset 'RESULT native MMP ZEROSET PASS transport=0 engine=ENOTSUP unchanged=off' "${pids[0]}"
kill "${pids[0]}";wait "${pids[0]}" || true
pids=()
boot owner
wait_marker owner 'RESULT native MMP OWNER READY hostid=51a20519' "${pids[0]}"
before=$(wc -l <"$run/owner-serial.log")
boot contender
wait_marker contender 'RESULT native MMP CONTENDER PASS active-owner=1 absent-locally=1 distinct-hostid=1' "${pids[1]}"
kill -0 "${pids[0]}"
tail -n "+$((before+1))" "$run/owner-serial.log" | rg -q 'MMP owner alive'
echo "PASS active owner exclusion; stopping exact private guests without pool export"
# Reap both guests before recovery: no live owner remains, but the pool was
# never exported. This tests stale ownership, not a physical power-loss claim.
for pid in "${pids[@]}";do kill "$pid";wait "$pid" || true;done
pids=()
boot nohost
wait_marker nohost 'RESULT native MMP NOHOST PASS identity=0 import-refused=1 absent-locally=1' "${pids[0]}"
kill "${pids[0]}";wait "${pids[0]}" || true
pids=()
boot recovery
wait_marker recovery 'RESULT native MMP RECOVERY PASS stopped-owner=1 imported=1 synced=1 exported=1' "${pids[0]}"
sha256sum -c "$run/backing.sha256"
echo "PASS native MMP active owner exclusion and stopped-owner recovery: $run"
