#!/usr/bin/env bash
# Reproduce native two-hardfile pool + DOS handler + cold-boot read-only tests.
# No host devices, network, DUT, shared build writes or existing test disks.
set -euo pipefail
# Bash otherwise reads later commands from this file after each long boot.
# Freeze the complete program so source edits cannot shift its read offset.
if [[ ${OZ_MIRROR_RUNNER_FROZEN:-0} != 1 ]];then
    export OZ_MIRROR_RUNNER_FROZEN=1
    exec bash -c "$(< "$0")" "$0" "$@"
fi
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
qemu=${AROS_TEST_QEMU:-qemu-system-x86_64}
cpus=${AROS_TEST_CPUS:-2}
[[ $cpus =~ ^[1-8]$ ]] || exit 2
zlib=${AROS_TEST_ZLIB_LIBRARY:-$repo/build-aros/hosted/Libs/z1.library}
test -r "$zlib"
disk_aio=${AROS_TEST_DISK_AIO:-default}
case "$disk_aio" in
    default) aio_option= ;;
    threads|io_uring) aio_option=",aio=$disk_aio" ;;
    *) exit 2 ;;
esac
for tool in "$qemu" qemu-img xorriso socat xxd sha256sum rg perl strings; do command -v "$tool" >/dev/null; done
timeout_seconds=${AROS_TEST_TIMEOUT_SECONDS:-180}
[[ $timeout_seconds =~ ^[1-9][0-9]*$ && ${#timeout_seconds} -le 5 ]] || exit 2
test -r "$base"
binary_dir=${AROS_TEST_BINARY_DIR:-$repo/build-aros}
partition_mode=${AROS_TEST_PARTITION_POOLS:-0}
[[ $partition_mode == 0 || $partition_mode == 1 ]]
replacement_mode=${AROS_TEST_COLD_REPLACEMENT:-0}
[[ $replacement_mode == 0 || $replacement_mode == 1 ]]
if [[ $replacement_mode == 1 ]];then [[ $partition_mode == 1 ]];fi
replacement_crash=${AROS_TEST_REPLACEMENT_CRASH:-0}
[[ $replacement_crash == 0 || $replacement_crash == 1 ]]
if [[ $replacement_crash == 1 ]];then [[ $replacement_mode == 1 ]];fi
partition_table=${AROS_TEST_POOL_PARTITION_TABLE:-mbr}
[[ $partition_table == mbr || $partition_table == gpt ]]
if [[ $partition_table == gpt ]];then
    [[ $partition_mode == 1 ]]
    command -v sgdisk >/dev/null
fi
if [[ $partition_mode == 1 && ${AROS_TEST_LARGE_DISKS:-0} == 1 ]];then
    echo 'Partition and large whole-disk fixtures are distinct modes' >&2;exit 2
fi
for binary in OpenZFSQEMUPoolTest OpenZFSQEMUImportTest OpenZFSQEMUReopenTest; do test -x "$binary_dir/$binary"; done
failed_mount_test=${AROS_TEST_FAILED_MOUNT:-0}
[[ $failed_mount_test == 0 || $failed_mount_test == 1 ]]
if [[ $failed_mount_test == 1 ]];then
    strings "$binary_dir/OpenZFSQEMUPoolTest" | rg -F 'RESULT native failed mount retry PASS' >/dev/null
fi
async_config_test=${AROS_TEST_ASYNC_CONFIG:-0}
runtime_flush_test=${AROS_TEST_RUNTIME_FLUSH:-0}
aux_flush_test=${AROS_TEST_AUX_FLUSH:-0}
aux_spare_test=${AROS_TEST_AUX_SPARE:-0}
log_flush_test=${AROS_TEST_LOG_FLUSH:-0}
log_data_test=${AROS_TEST_LOG_DATA_FLUSH:-0}
[[ $log_data_test == 0 || $log_data_test == 1 ]]
if ((log_data_test));then
    [[ $log_flush_test == 1 ]]
    strings "$binary_dir/OpenZFSQEMUPoolTest" | rg -F 'DIAGNOSTIC combined log-data' >/dev/null
fi
[[ $log_flush_test == 0 || $log_flush_test == 1 ]]
if ((log_flush_test));then
    [[ $aux_flush_test == 1 && $aux_spare_test == 0 ]]
    strings "$binary_dir/OpenZFSQEMUPoolTest" | rg -F 'DIAGNOSTIC log flush' >/dev/null
    for binary in OpenZFSQEMUImportTest OpenZFSQEMUReopenTest;do
        strings "$binary_dir/$binary" | rg -F 'DIAGNOSTIC cold SLOG fallback' >/dev/null
    done
fi
active_spare_test=${AROS_TEST_ACTIVE_SPARE:-0}
[[ $active_spare_test == 0 || $active_spare_test == 1 ]]
if ((active_spare_test));then
    [[ $aux_spare_test == 1 ]]
    strings "$binary_dir/OpenZFSQEMUPoolTest" | rg -F 'DIAGNOSTIC active spare' >/dev/null
    for binary in OpenZFSQEMUImportTest OpenZFSQEMUReopenTest;do
        strings "$binary_dir/$binary" | rg -F 'DIAGNOSTIC cold active spare' >/dev/null
    done
fi
[[ $aux_spare_test == 0 || $aux_spare_test == 1 ]]
aux_kind=cache
if ((aux_spare_test));then
    [[ $aux_flush_test == 1 ]]
    aux_kind=spare
fi
[[ $aux_flush_test == 0 || $aux_flush_test == 1 ]]
if ((aux_flush_test));then
    [[ $runtime_flush_test == 1 && $partition_mode == 1 ]]
    if ((!log_flush_test));then
        strings "$binary_dir/OpenZFSQEMUPoolTest" | rg -F 'DIAGNOSTIC auxiliary' >/dev/null
    fi
fi
strict_flush_test=${AROS_TEST_STRICT_FLUSH:-0}
txg_flush_test=${AROS_TEST_TXG_FLUSH_VERIFY:-0}
zil_flush_test=${AROS_TEST_ZIL_FLUSH:-0}
single_leaf_test=${AROS_TEST_SINGLE_LEAF_FLUSH:-0}
[[ $single_leaf_test == 0 || $single_leaf_test == 1 ]]
if ((single_leaf_test));then
    [[ $zil_flush_test == 1 ]]
    strings "$binary_dir/OpenZFSQEMUPoolTest" | rg -F 'DIAGNOSTIC persistent leaf' >/dev/null
fi
[[ $zil_flush_test == 0 || $zil_flush_test == 1 ]]
if ((zil_flush_test));then
    [[ $strict_flush_test == 1 ]]
    [[ $txg_flush_test == 1 ]]
    strings "$binary_dir/OpenZFSQEMUPoolTest" | rg -F 'DIAGNOSTIC ZIL flush failed=' >/dev/null
    for binary in OpenZFSQEMUImportTest OpenZFSQEMUReopenTest;do
        strings "$binary_dir/$binary" | rg -F 'DIAGNOSTIC cold ZPL recovery payload=' >/dev/null
    done
fi
[[ $txg_flush_test == 0 || $txg_flush_test == 1 ]]
if ((txg_flush_test));then [[ $strict_flush_test == 1 ]];fi
[[ $strict_flush_test == 0 || $strict_flush_test == 1 ]]
if ((strict_flush_test));then [[ $runtime_flush_test == 1 ]];fi
[[ $runtime_flush_test == 0 || $runtime_flush_test == 1 ]]
if ((runtime_flush_test));then
    strings "$binary_dir/OpenZFSQEMUPoolTest" | rg -F 'DIAGNOSTIC runtime flush injected=EIO' >/dev/null
fi
[[ $async_config_test == 0 || $async_config_test == 1 ]]
if [[ $async_config_test == 1 ]];then
    strings "$binary_dir/OpenZFSQEMUPoolTest" | rg -F 'Native suspended async config tests=' >/dev/null
fi
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_TEST_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/qemu-run.XXXXXXXX")
echo "Evidence: $run"
printf '%s\n' "$cpus" >"$run/cpus.txt"
sha256sum "$zlib" >"$run/zlib-input.sha256"
if ((aux_flush_test));then
    python3 aros/engine/pool-partition-fixture.py create "$run/aux.raw" --table gpt >"$run/aux-layout.log" 2>&1
    python3 aros/engine/pool-partition-fixture.py outside "$run/aux.raw" >"$run/aux-outside-before.txt"
fi
allocation_test=${AROS_TEST_DOS_ALLOC:-0}
if [[ -f "$binary_dir/allocation-test-options.txt" ]];then
    rg -Fxq 'dos_allocation_injection=1' "$binary_dir/allocation-test-options.txt"
    cp "$binary_dir/allocation-test-options.txt" "$run/"
    allocation_test=1
fi
[[ $allocation_test == 0 || $allocation_test == 1 ]]
# Each later boot must use the binaries whose hashes we record now, not
# whatever another completed build may have placed at the source paths.
mkdir "$run/binaries"
for binary in OpenZFSQEMUPoolTest OpenZFSQEMUImportTest OpenZFSQEMUReopenTest;do
    cp --reflink=auto "$binary_dir/$binary" "$run/binaries/$binary"
done
binary_dir="$run/binaries"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
"$qemu" --version >"$run/qemu-version.txt"
sha256sum "$(command -v "$qemu")" >"$run/qemu.sha256"
printf '%s\n' "$disk_aio" >"$run/disk-aio.txt"
tar -czf "$run/aros-source.tar.gz" aros/engine aros/zpl aros/patches aros/platform
sha256sum "$base" "$binary_dir/OpenZFSQEMUPoolTest" "$binary_dir/OpenZFSQEMUImportTest" "$binary_dir/OpenZFSQEMUReopenTest" \
    "$zlib" >"$run/inputs.sha256"
vm_pid=
cleanup() { if [[ -n $vm_pid ]] && kill -0 "$vm_pid" 2>/dev/null; then kill "$vm_pid"; wait "$vm_pid" || true; fi; }
trap cleanup EXIT
pack() {
    local output=$1; shift
    local security_args=()
    local dos_args=()
    local ata_args=()
    local posix_args=()
    local partition_args=()
    if [[ $partition_mode == 1 ]];then
        local sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
        partition_args=(-map "${sdk%/Developer}/Libs/partition.library" /Libs/partition.library)
        sha256sum "${sdk%/Developer}/Libs/partition.library" > "$run/partition-library.sha256"
    fi
    if [[ -n ${AROS_TEST_POSIX_LIBRARY:-} ]]; then
        test -r "$AROS_TEST_POSIX_LIBRARY"
        posix_args=(-map "$AROS_TEST_POSIX_LIBRARY" /Libs/posixc.library)
        sha256sum "$AROS_TEST_POSIX_LIBRARY" >"$run/posix-overlay.sha256"
    fi
    if [[ -n ${AROS_TEST_ATA_OVERLAY:-} ]]; then
        test -r "$AROS_TEST_ATA_OVERLAY/aros-bsp.pkg.xz"
        test -r "$AROS_TEST_ATA_OVERLAY/ata.device"
        ata_args=(-map "$AROS_TEST_ATA_OVERLAY/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz
                  -map "$AROS_TEST_ATA_OVERLAY/ata.device" /Devs/ata.device)
        sha256sum "$AROS_TEST_ATA_OVERLAY/aros-bsp.pkg.xz" "$AROS_TEST_ATA_OVERLAY/ata.device" >"$run/ata-overlay.sha256"
        # The private MuFS candidate composes security.library into this BSP.
        # Keep the on-disk copy consistent with the resident boot module too.
        if [[ -f "$AROS_TEST_ATA_OVERLAY/security.library" ]]; then
            ata_args+=(-map "$AROS_TEST_ATA_OVERLAY/security.library" /Libs/security.library)
            sha256sum "$AROS_TEST_ATA_OVERLAY/security.library" >"$run/security-overlay.sha256"
        fi
    fi
    if [[ -n ${AROS_TEST_DOS_OVERLAY:-} ]]; then
        test -r "$AROS_TEST_DOS_OVERLAY/aros-base.pkg.xz"
        test -r "$AROS_TEST_DOS_OVERLAY/dos.library"
        dos_args=(-map "$AROS_TEST_DOS_OVERLAY/aros-base.pkg.xz" /boot/aros-base.pkg.xz
                  -map "$AROS_TEST_DOS_OVERLAY/dos.library" /Libs/dos.library)
        sha256sum "$AROS_TEST_DOS_OVERLAY/aros-base.pkg.xz" "$AROS_TEST_DOS_OVERLAY/dos.library" >"$run/dos-overlay.sha256"
    fi
    if [[ ${AROS_TEST_SECURITY:-0} == 1 ]]; then
        security_args=(-map aros/engine/security-fixture /Security)
    fi
    xorriso -indev "$base" -outdev "$output" \
        -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
        -map "$zlib" /Libs/z1.library "${security_args[@]}" "${dos_args[@]}" "${ata_args[@]}" "${posix_args[@]}" "${partition_args[@]}" "$@" \
        -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on
}
boot() {
    local phase=$1 iso=$2 format=$3 disk0=$4 disk1=$5 success=$6
    local drive_args=()
    if ((aux_flush_test)) && [[ $phase == create ]];then
        drive_args+=(-drive "file=$run/aux.raw,format=raw,if=ide,index=3$aio_option")
    fi
    if [[ $disk0 != missing ]]; then drive_args+=(-drive "file=$disk0,format=$format,if=ide,index=0$aio_option"); fi
    if [[ $disk1 != missing ]]; then drive_args+=(-drive "file=$disk1,format=$format,if=ide,index=1$aio_option"); fi
    "$qemu" -name "openzfs-$phase-$$" -machine pc,accel=kvm -cpu host -smp "$cpus" -m 2048 \
        -drive "file=$iso,media=cdrom,if=ide,index=2,readonly=on" \
        "${drive_args[@]}" \
        -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
        -serial "file:$run/$phase-serial.log" \
        -monitor "unix:$run/$phase.monitor,server=on,wait=off" >"$run/$phase-qemu.log" 2>&1 &
    vm_pid=$!
    local deadline=$((SECONDS+timeout_seconds))
    # Retain raw serial. Rejoin only complete native task-notification records
    # that can interrupt RawPutChar, including its final PASS/FAIL marker.
    local checked="$run/$phase-completion-checked.log"
    while :; do
        if [[ -f "$run/$phase-serial.log" ]]; then
            perl -0777 -pe 's/[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| \[security.library\] secTaskNotify: [^\r\n]*\r?\n//g' \
                "$run/$phase-serial.log" >"$checked"
            if rg -a -q '^RESULT .*FAIL|^Native fixture binding failed|^Native (UIO|dynamic taskq) .*tests=[1-9]|\[PF-DBG\]|^ASSERT at|signal=6 default action=terminate' "$run/$phase-serial.log" "$checked"; then
                echo "$phase reported failure; see $run" >&2; return 1
            fi
            if rg -a -q "$success" "$checked"; then break; fi
        fi
        if ! kill -0 "$vm_pid" 2>/dev/null; then
            echo "$phase QEMU exited before completion; see $run" >&2; return 1
        fi
        if (( SECONDS>=deadline )); then
            echo "$phase exceeded ${timeout_seconds}s with QEMU still live; see $run" >&2; return 1
        fi
        sleep 1
    done
    printf 'info blockstats\nscreendump %s/%s.ppm\n' "$run" "$phase" |
        socat - "UNIX-CONNECT:$run/$phase.monitor" >"$run/$phase-monitor.txt"
    printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
    wait "$vm_pid"; vm_pid=
    if [[ $replacement_mode == 1 ]];then return;fi
    if [[ $phase == create && $async_config_test == 1 ]];then
        rg -a -q '^Native suspended async config tests=0' "$run/$phase-completion-checked.log"
    fi
    if [[ $phase == reopen ]]; then
        [[ $(rg -a -c '^cold writer reopen pass=0' "$run/$phase-completion-checked.log") == 3 ]]
        rg -a -q '^cold writer export=0' "$run/$phase-completion-checked.log"
        return
    fi
    local owner_write=0
    if [[ $phase == create ]]; then owner_write=1; fi
    rg -a -q "^Native foreign mount close write=$owner_write errno=[1-9][0-9]* result=0" \
        "$run/$phase-completion-checked.log"
    rg -a -q '^Native DOS exclusive lock conflicts=0' "$run/$phase-completion-checked.log"
    rg -a -q '^Native DOS filehandle lock conversion/metadata=0' "$run/$phase-completion-checked.log"
    rg -a -q '^Native DOS ChangeMode upgrade/downgrade=0' "$run/$phase-completion-checked.log"
    rg -a -q '^Native DOS ExAll levels/continuation/filter=0' "$run/$phase-completion-checked.log"
    rg -a -q '^Native DOS ExAll hooks/invalid packets=0' "$run/$phase-completion-checked.log"
    rg -a -q '^Native MuFS capability queries authenticated=0 result=0' "$run/$phase-completion-checked.log"
    rg -a -q '^Native MuFS retained endpoints/remount authenticated=0 result=0' "$run/$phase-completion-checked.log"
    if [[ ${AROS_TEST_SECURITY:-0} == 1 ]]; then
        rg -a -q '^Native MuFS capability queries authenticated=1 result=0' "$run/$phase-completion-checked.log"
        rg -a -q '^Native MuFS retained endpoints/remount authenticated=1 result=0' "$run/$phase-completion-checked.log"
    fi
    rg -a '^RESULT|^DOS handler|^cold DOS' "$run/$phase-completion-checked.log"
}
disk_size=256M
pool_startup=aros/engine/Startup-QEMU-Pool-Test
if [[ ${AROS_TEST_LARGE_DISKS:-0} == 1 ]]; then
    disk_size=8G
    pool_startup=aros/engine/Startup-QEMU-Pool-Large
fi
if [[ $partition_mode == 1 ]];then
    disk_size=512M
    pool_startup=aros/engine/Startup-QEMU-Pool-Partition
    for disk in 0 1;do
        python3 aros/engine/pool-partition-fixture.py create "$run/disk$disk.raw" \
            --table "$partition_table" > "$run/partition$disk-layout.log" 2>&1
        python3 aros/engine/pool-partition-fixture.py outside "$run/disk$disk.raw" > "$run/outside$disk-before.txt"
    done
else
    qemu-img create -f raw "$run/disk0.raw" "$disk_size"
    qemu-img create -f raw "$run/disk1.raw" "$disk_size"
fi
if [[ $replacement_mode == 1 ]];then
    for phase in stage recover;do
        startup=aros/engine/Startup-QEMU-Replacement-Stage
        stage_marker='^RESULT native cold replacement stage PASS errno=0'
        if [[ $replacement_crash == 1 ]];then
            startup=aros/engine/Startup-QEMU-Replacement-Crash
            stage_marker='^RESULT native cold replacement crash-ready PASS'
        fi
        [[ $phase != recover ]] || startup=aros/engine/Startup-QEMU-Replacement-Recover
        pack "$run/$phase.iso" -map "$binary_dir/OpenZFSQEMUPoolTest" /C/OpenZFSQEMUPoolTest \
            -map "$startup" /S/Startup-Sequence >"$run/pack-$phase.log" 2>&1
        if [[ $phase == stage ]];then
            boot "$phase" "$run/$phase.iso" raw "$run/disk0.raw" "$run/disk1.raw" "$stage_marker"
            for disk in 0 1;do
                python3 aros/engine/pool-partition-fixture.py outside "$run/disk$disk.raw" >"$run/outside$disk-staged.txt"
                cmp "$run/outside$disk-before.txt" "$run/outside$disk-staged.txt"
            done
            sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/staged-disks.sha256"
            for disk in 0 1;do qemu-img create -f qcow2 -F raw -b "$run/disk$disk.raw" "$run/recover$disk.qcow2";done
        else
            boot "$phase" "$run/$phase.iso" qcow2 "$run/recover0.qcow2" "$run/recover1.qcow2" '^RESULT native cold replacement recover PASS errno=0'
        fi
    done
    sha256sum -c "$run/staged-disks.sha256"
    echo "PASS: unfinished replacement survives guest cold boot (abrupt=$replacement_crash) and resumes on COW disks. Evidence: $run"
    exit 0
fi
pack "$run/create.iso" -map "$binary_dir/OpenZFSQEMUPoolTest" /C/OpenZFSQEMUPoolTest \
    -map "$pool_startup" /S/Startup-Sequence >"$run/pack-create.log" 2>&1
boot create "$run/create.iso" raw "$run/disk0.raw" "$run/disk1.raw" '^RESULT QEMU native two-device mirror PASS'
rg -a -q '^RESULT legacy disk matrix PASS create=1 versions=1-4 error=0 permissions=1' "$run/create-completion-checked.log"
if [[ $partition_mode == 1 ]];then
    for disk in 0 1;do
        python3 aros/engine/pool-partition-fixture.py outside "$run/disk$disk.raw" > "$run/outside$disk-created.txt"
        cmp "$run/outside$disk-before.txt" "$run/outside$disk-created.txt"
    done
fi
if [[ ${AROS_TEST_LARGE_DISKS:-0} == 1 ]]; then
    [[ $(rg -a -c '^native large-device unit=[01] offset=.* comparison=0' "$run/create-completion-checked.log") == 6 ]]
    [[ $(rg -a -c '^native large-device unit=[01] restored=0' "$run/create-completion-checked.log") == 2 ]]
fi
rg -a -q '^Native UIO scatter/gather/copy/skip/64-bit/bounds tests=0' "$run/create-completion-checked.log"
rg -a -q '^Native engine/ZPL shared-layout tests=0' "$run/create-completion-checked.log"
rg -a -q '^Native file-size limits/internal flags tests=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream ACL storage helper tests=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream ACL conversion/core tests=0' "$run/create-completion-checked.log"
rg -a -q '^Native credential identity/groups/refcount/policy tests=0' "$run/create-completion-checked.log"
rg -a -q '^Native security packet identity conversion/capture=0' "$run/create-completion-checked.log"
if [[ $failed_mount_test == 1 ]];then
    rg -a -q 'RESULT native failed mount retry PASS admission-error=5 loan-drained=1 foreign-owner-denied=1 reopen=1' "$run/create-serial.log"
fi
if [[ ${AROS_TEST_SECURITY:-0} == 1 ]]; then
    rg -a -q '^Native DOS SetProtection readonly=0 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS SetProtection readonly=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS SetFileDate readonly=0 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS SetFileDate readonly=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS owner property restoration readonly=0 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS content write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS resize write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS create defaults write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS mkdir write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS delete/reclaim write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS rename/move write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS replacement/exclusive write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS sparse64 write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS SetOwner write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS SetComment write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS hardlink write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS symlink paths/loops write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS symlink target authorization write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS symlink cross-volume/bounds write=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS write-only open/seek/read denial=0' "$run/create-completion-checked.log"
    rg -a -q '^Native DOS owner property restoration readonly=1 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native authenticated packet root/reader/group capture=0' "$run/create-completion-checked.log"
    rg -a -q '^Native cross-task DOS sender identity/lifetime=0' "$run/create-completion-checked.log"
    rg -a -q '^Native packet security access/property/defaults=0' "$run/create-completion-checked.log"
    rg -a -q '^Native retained credential after packet context destruction=0' "$run/create-completion-checked.log"
    [[ $(rg -a -c '^Native MuFS packet delegated create expected=1 result=0' "$run/create-completion-checked.log") == 2 ]]
    rg -a -q '^Native MuFS packet delegated create expected=0 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native MuFS packet delegated create expected=17 result=0' "$run/create-completion-checked.log"
    rg -a -q '^Native authenticated admission endpoint=0' "$run/create-completion-checked.log"
    rg -a -q '^Authenticated DOS handler ACL/logout/lifetime=0' "$run/create-completion-checked.log"
    rg -a -q '^Authenticated DOS restricted file/directory ACL=0' "$run/create-completion-checked.log"
fi
rg -a -q '^Native upstream FUID mapping/AVL/unmapped-identity tests=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream dataset delegation policy=0' "$run/create-completion-checked.log"
rg -a -q '^Native ZPL timestamps/relatime/readonly/dirty-state tests=0' "$run/create-completion-checked.log"
rg -a -q '^Native named/GUID vdev policy intersection result=0' "$run/create-completion-checked.log"
rg -a -q '^Native SA atime flush/persistence=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream vnode write/fsync/append=0' "$run/create-completion-checked.log"
for kind in 0 1 2; do
    rg -a -q "^Native namespace sync-failure kind=$kind injected=1 result=0" \
        "$run/create-completion-checked.log"
done
rg -a -q '^Native mount policy callbacks write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native ZPL hole/truncate/extend write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native directory shared/exclusive lifetime=0' "$run/create-completion-checked.log"
rg -a -q '^Native hardlink/remove/relink write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream mknode write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream mkdir/rollback write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream symlink/readlink write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream create/open/truncate write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream xattr directory/file write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream rmdir/unlinked/xattr cleanup write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream unlink/open-file/symlink write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native upstream rename/move/replace/exchange write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native persistent directory exchange write=1 directories=1 mixed=1 parents=1 payloads=4 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native sibling directory exchange PASS directories=1 mixed=1 restored=1 opposing-swaps=128' "$run/create-completion-checked.log"
rg -a -q '^Native missing directory exchange PASS same-parent=1 cross-parent=1 unchanged=1' "$run/create-completion-checked.log"
rg -a -q '^Native cross-parent directory exchange PASS directories=1 mixed=1 cycles-denied=2 opposing-swaps=128 restored=1' "$run/create-completion-checked.log"
rg -a -q '^Native exchange ancestry failure PASS forward=1 reverse=1 corrupt-cycle=1 unchanged=1' "$run/create-completion-checked.log"
rg -a -q '^Native rename ancestry SA failure/retry result=0' "$run/create-completion-checked.log"
rg -a -q '^Native ordinary rename corrupt ancestry PASS unchanged=1' "$run/create-completion-checked.log"
rg -a -q '^Native upstream setattr/owner/mode/times write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native protection xattr persistence write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native writable mount lifecycle write=1 result=0' "$run/create-completion-checked.log"
rg -a -q '^Native repeated writable reopen result=0' "$run/create-completion-checked.log"
rg -a -q '^Native mount ACL traversal/read bridge=0' "$run/create-completion-checked.log"
rg -a -q '^Native direct-buffer descriptors/borrowed ABD tests=0' "$run/create-completion-checked.log"
rg -a -q '^Upstream native direct UIO read/compare=0' "$run/create-completion-checked.log"
rg -a -q '^Upstream native direct UIO write=0' "$run/create-completion-checked.log"
rg -a -q '^Upstream buffered UIO partial-block write/read=0' "$run/create-completion-checked.log"
awk '/^EXPORT_XDR / {gsub(/\r/, "", $2); print $2}' "$run/create-completion-checked.log" |
    xxd -r -p >"$run/export.nvlist"
expected=$(awk '/^EXPORT_XDR_BEGIN / {gsub(/\r/, "", $2); print $2}' "$run/create-completion-checked.log")
[[ $expected =~ ^[0-9]+$ && $(stat -c %s "$run/export.nvlist") == "$expected" ]]
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/disks-before.sha256"
# IDE rejects readonly hard disks. Explicit COW overlays protect the backing
# images; require zero guest writes as well as unchanged original hashes.
qemu-img create -f qcow2 -F raw -b "$run/disk0.raw" "$run/read0.qcow2"
qemu-img create -f qcow2 -F raw -b "$run/disk1.raw" "$run/read1.qcow2"
pack "$run/import.iso" -map "$binary_dir/OpenZFSQEMUImportTest" /C/OpenZFSQEMUImportTest \
    -map "$run/export.nvlist" /export.nvlist \
    -map aros/engine/Startup-QEMU-Import-Test /S/Startup-Sequence >"$run/pack-import.log" 2>&1
boot import "$run/import.iso" qcow2 "$run/read0.qcow2" "$run/read1.qcow2" \
    '^RESULT cold-boot QEMU two-drive pool through AROS handler PASS'
rg -a -q '^RESULT legacy disk matrix PASS create=0 versions=1-4 error=0 permissions=1' "$run/import-completion-checked.log"
rg -a -q '^cold native SA persisted atime=0' "$run/import-completion-checked.log"
rg -a -q '^cold upstream vnode written bytes/size=0' "$run/import-completion-checked.log"
rg -a -q '^Native mount policy callbacks write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native ZPL hole/truncate/extend write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native directory shared/exclusive lifetime=0' "$run/import-completion-checked.log"
rg -a -q '^Native hardlink/remove/relink write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native upstream mknode write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native upstream mkdir/rollback write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native upstream symlink/readlink write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native upstream create/open/truncate write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native upstream xattr directory/file write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native upstream rmdir/unlinked/xattr cleanup write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native upstream unlink/open-file/symlink write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native upstream rename/move/replace/exchange write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native persistent directory exchange write=0 directories=1 mixed=1 parents=1 payloads=4 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native upstream setattr/owner/mode/times write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native protection xattr persistence write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native writable mount lifecycle write=0 result=0' "$run/import-completion-checked.log"
rg -a -q '^Native mount ACL traversal/read bridge=0' "$run/import-completion-checked.log"
if [[ ${AROS_TEST_SECURITY:-0} == 1 ]]; then
    rg -a -q '^Native DOS SetProtection readonly=1 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS SetFileDate readonly=1 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS owner property restoration readonly=1 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS content write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS resize write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS create defaults write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS mkdir write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS delete/reclaim write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS rename/move write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS replacement/exclusive write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS sparse64 write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS SetOwner write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS SetComment write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS hardlink write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS symlink paths/loops write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS symlink target authorization write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Native DOS symlink cross-volume/bounds write=0 result=0' "$run/import-completion-checked.log"
    rg -a -q '^Authenticated DOS handler ACL/logout/lifetime=0' "$run/import-completion-checked.log"
    rg -a -q '^Authenticated DOS restricted file/directory ACL=0' "$run/import-completion-checked.log"
fi
rg -a -q '^cold upstream direct UIO read/compare=0' "$run/import-completion-checked.log"
[[ $(rg -c '^ide0-hd[01]: .*wr_bytes=0 .*wr_operations=0 ' "$run/import-monitor.txt") == 2 ]]
# Each mirror member must independently serve the real persisted filesystem.
# The first run omits its sibling. The old ATA driver cannot enumerate a
# slave-only channel, so the second uses a blank NON-member master instead.
pack "$run/degraded.iso" -map "$binary_dir/OpenZFSQEMUImportTest" /C/OpenZFSQEMUImportTest \
    -map "$run/export.nvlist" /export.nvlist \
    -map aros/engine/Startup-QEMU-Degraded-Test /S/Startup-Sequence >"$run/pack-degraded.log" 2>&1
for survivor in 0 1; do
    qemu-img create -f qcow2 -F raw -b "$run/disk$survivor.raw" "$run/survivor$survivor.qcow2"
    disks=(missing missing);disks[$survivor]="$run/survivor$survivor.qcow2"
    expected_disks=1
    if [[ $survivor == 1 ]]; then
        qemu-img create -f qcow2 "$run/blank-nonmember.qcow2" 256M
        disks[0]="$run/blank-nonmember.qcow2";expected_disks=2
    fi
    boot "survivor$survivor" "$run/degraded.iso" qcow2 "${disks[0]}" "${disks[1]}" \
        '^RESULT cold-boot QEMU degraded mirror through AROS handler PASS'
    rg -a -q '^RESULT legacy disk matrix PASS create=0 versions=1-4 error=0 permissions=1' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^cold native SA persisted atime=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^cold upstream vnode written bytes/size=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native mount policy callbacks write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native ZPL hole/truncate/extend write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native directory shared/exclusive lifetime=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native hardlink/remove/relink write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native upstream mknode write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native upstream mkdir/rollback write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native upstream symlink/readlink write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native upstream create/open/truncate write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native upstream xattr directory/file write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native upstream rmdir/unlinked/xattr cleanup write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native upstream unlink/open-file/symlink write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native upstream rename/move/replace/exchange write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native persistent directory exchange write=0 directories=1 mixed=1 parents=1 payloads=4 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native upstream setattr/owner/mode/times write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native protection xattr persistence write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native writable mount lifecycle write=0 result=0' "$run/survivor$survivor-completion-checked.log"
    rg -a -q '^Native mount ACL traversal/read bridge=0' "$run/survivor$survivor-completion-checked.log"
    if [[ ${AROS_TEST_SECURITY:-0} == 1 ]]; then
        rg -a -q '^Native DOS SetProtection readonly=1 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS SetFileDate readonly=1 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS owner property restoration readonly=1 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS content write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS resize write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS create defaults write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS mkdir write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS delete/reclaim write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS rename/move write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS replacement/exclusive write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS sparse64 write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS SetOwner write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS SetComment write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS hardlink write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS symlink paths/loops write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS symlink target authorization write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Native DOS symlink cross-volume/bounds write=0 result=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Authenticated DOS handler ACL/logout/lifetime=0' "$run/survivor$survivor-completion-checked.log"
        rg -a -q '^Authenticated DOS restricted file/directory ACL=0' "$run/survivor$survivor-completion-checked.log"
    fi
    rg -a -q '^cold upstream direct UIO read/compare=0' "$run/survivor$survivor-completion-checked.log"
    [[ $(rg -c '^ide0-hd[01]: .*wr_bytes=0 .*wr_operations=0 ' "$run/survivor$survivor-monitor.txt") == $expected_disks ]]
done
qemu-img create -f qcow2 -F raw -b "$run/disk0.raw" "$run/writer0.qcow2"
qemu-img create -f qcow2 -F raw -b "$run/disk1.raw" "$run/writer1.qcow2"
pack "$run/reopen.iso" -map "$binary_dir/OpenZFSQEMUReopenTest" /C/OpenZFSQEMUReopenTest \
    -map "$run/export.nvlist" /export.nvlist \
    -map aros/engine/Startup-QEMU-Reopen-Test /S/Startup-Sequence >"$run/pack-reopen.log" 2>&1
boot reopen "$run/reopen.iso" qcow2 "$run/writer0.qcow2" "$run/writer1.qcow2" \
    '^RESULT cold writable reopen PASS'
rg -a -q '^RESULT legacy disk matrix PASS create=0 versions=1-4 error=0 permissions=1' "$run/reopen-completion-checked.log"
[[ $(rg -c '^ide0-hd[01]: .*wr_bytes=[1-9][0-9]* ' "$run/reopen-monitor.txt") == 2 ]]
if [[ -n ${AROS_TEST_ATA_OVERLAY:-} ]]; then
    for phase in create reopen; do
        [[ $(rg -c '^ide0-hd[01]: .*flush_operations=[1-9][0-9]* ' "$run/$phase-monitor.txt") == 2 ]]
    done
fi
sha256sum -c "$run/disks-before.sha256" | tee "$run/disks-after.txt"
verify0="$run/disk0.raw";verify1="$run/disk1.raw"
writer0="$run/writer0-verify.raw";writer1="$run/writer1-verify.raw"
if [[ $partition_mode == 1 || -x build-control/zdb ]];then
    for disk in 0 1;do
        qemu-img convert -f qcow2 -O raw "$run/writer$disk.qcow2" "$run/writer$disk-verify.raw"
    done
fi
if [[ $partition_mode == 1 ]];then
    for disk in 0 1;do
        python3 aros/engine/pool-partition-fixture.py outside "$run/writer$disk-verify.raw" > "$run/outside$disk-reopened.txt"
        cmp "$run/outside$disk-before.txt" "$run/outside$disk-reopened.txt"
        python3 aros/engine/pool-partition-fixture.py extract "$run/disk$disk.raw" "$run/partition$disk.raw"
        python3 aros/engine/pool-partition-fixture.py extract "$run/writer$disk-verify.raw" "$run/writer-partition$disk.raw"
    done
    verify0="$run/partition0.raw";verify1="$run/partition1.raw"
    writer0="$run/writer-partition0.raw";writer1="$run/writer-partition1.raw"
fi
if [[ -x build-control/zdb ]]; then
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        build-control/zdb -e -p "$verify0" -p "$verify1" -bcc qemu_mirror \
        >"$run/linux-zdb.log" 2>&1
    rg -q 'No leaks' "$run/linux-zdb.log"
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        build-control/zdb -e -p "$writer0" -p "$writer1" -bcc qemu_mirror \
        >"$run/writer-linux-zdb.log" 2>&1
    rg -q 'No leaks' "$run/writer-linux-zdb.log"
fi
if [[ $allocation_test == 1 ]];then
    if [[ ${AROS_TEST_SECURITY:-0} == 1 ]];then
        for linked in 0 1;do
            rg -Fq "DOS replacement identity/reclaim hardlink=$linked result=0 policy-calls=" "$run/create-serial.log" || {
                echo "FAIL: missing replacement identity/reclamation evidence: hardlink=$linked" >&2;exit 1;
            }
        done
        rg -Fq 'DOS output allocation failure hits=1 result=0 original-preserved=1' "$run/create-serial.log" || {
            echo 'FAIL: missing output allocation preservation evidence' >&2;exit 1;
        }
        rg -Fq 'DOS output creation failure hits=1 result=0 original-preserved=1' "$run/create-serial.log" || {
            echo 'FAIL: missing output creation preservation evidence' >&2;exit 1;
        }
        rg -Fq 'DOS output publication failure hits=1 result=0 original-preserved=1' "$run/create-serial.log" || {
            echo 'FAIL: missing output publication preservation and cleanup evidence' >&2;exit 1;
        }
        rg -Fq 'DOS output post-publication failure hits=1 result=0 published-preserved=1' "$run/create-serial.log" || {
            echo 'FAIL: missing published replacement preservation evidence' >&2;exit 1;
        }
    fi
    for boot in create import survivor0 survivor1;do
        if ! rg -Fq 'DOS lock allocation failure hits=1 result=0' "$run/$boot-serial.log";then
            echo "FAIL: missing lock allocation evidence: boot=$boot" >&2;exit 1
        fi
        for nth in 1 2;do
            for kind in 'mount' 'adopted mount';do
                if ! rg -Fq "DOS $kind allocation failure nth=$nth hits=1 result=0" "$run/$boot-serial.log";then
                    echo "FAIL: missing $kind allocation evidence: boot=$boot nth=$nth" >&2
                    exit 1
                fi
            done
        done
    done
    echo 'PASS: handler allocation failures: two mount sites, two ownership paths, DOS lock, four boots'
fi
if ((runtime_flush_test));then
    expected_flush=0
    if ((strict_flush_test));then expected_flush=5;fi
    rg -q "^DIAGNOSTIC runtime flush injected=EIO hits=([2-9]|[1-9][0-9]+) parent=$expected_flush recovery=0" "$run/create-serial.log"
    rg -q "^DIAGNOSTIC runtime flush single hits=1 real=[1-9][0-9]* errors=0 parent=$expected_flush recovery-leaves=([2-9]|[1-9][0-9]+) recovery-errors=0" "$run/create-serial.log"
    echo "DIAGNOSTIC: runtime leaf flush parent=$expected_flush; not complete durability acceptance"
fi
if ((aux_flush_test));then
    if ((log_flush_test));then
        if ((log_data_test));then
            rg -q '^DIAGNOSTIC combined log-data result=[1-9][0-9]* log-failures=[1-9][0-9]* data-failures=[1-9][0-9]* suspended=1 retry=0 error=0' "$run/create-serial.log"
            rg -q '^DIAGNOSTIC log flush result=[1-9][0-9]* failures=[1-9][0-9]* healthy=0 healthy-errors=0 suspended=1 resume=0 sync=0 payload=1 remove=0 fallbacks=[1-9][0-9]* error=0' "$run/create-serial.log"
        else
        rg -q '^DIAGNOSTIC log flush result=0 failures=[1-9][0-9]* healthy=[1-9][0-9]* healthy-errors=0 suspended=0 resume=0 sync=0 payload=1 remove=0 fallbacks=[1-9][0-9]* error=0' "$run/create-serial.log"
        fi
        for phase in import survivor0 survivor1 reopen;do
            rg -q '^DIAGNOSTIC cold SLOG fallback payload=1 size=8 error=0' "$run/$phase-serial.log"
        done
    else
    rg -q "^DIAGNOSTIC auxiliary $aux_kind failures=[1-9][0-9]* healthy=[1-9][0-9]* healthy-errors=0 suspended=0 resume=0 sync=0 remove=0 error=0" "$run/create-serial.log"
    if ((active_spare_test));then
        rg -q '^DIAGNOSTIC active spare verified=1 failures=[1-9][0-9]* parent=5 recovery=0 detach=0 txg=0 error=0' "$run/create-serial.log"
        for phase in import survivor0 survivor1 reopen;do
            rg -q '^DIAGNOSTIC cold active spare payload=1 object=[1-9][0-9]* error=0' "$run/$phase-serial.log"
        done
    fi
    fi
    python3 aros/engine/pool-partition-fixture.py outside "$run/aux.raw" >"$run/aux-outside-after.txt"
    cmp "$run/aux-outside-before.txt" "$run/aux-outside-after.txt"
    if ((log_flush_test));then
        if ((log_data_test));then
            echo 'DIAGNOSTIC: combined SLOG/data failure refused success, recovered and cold retry payload passed; private experiment only'
        else
            echo 'DIAGNOSTIC: SLOG flush failure fallback passed; private experiment only'
        fi
    else
        echo "DIAGNOSTIC: $aux_kind flush failure isolated without data-pool suspension; private experiment only"
    fi
fi
if ((zil_flush_test));then
    rg -q '^DIAGNOSTIC ZIL flush failed=[1-9][0-9]* failures=[1-9][0-9]* suspended=1 resume=0 synced=0 retry=0 payload=1 error=0' "$run/create-serial.log"
    if ((single_leaf_test));then
        for phase in TXG ZPL;do
            rg -q "^DIAGNOSTIC persistent leaf $phase failures=[1-9][0-9]* healthy=[1-9][0-9]* healthy-errors=0 error=0" "$run/create-serial.log"
        done
    fi
    for phase in import survivor0 survivor1 reopen;do
        rg -q '^DIAGNOSTIC cold ZPL recovery payload=1 size=8 error=0' "$run/$phase-serial.log"
    done
    echo 'DIAGNOSTIC: synchronous ZPL failure, same-mount recovery and cold payload passed; no uncommitted ZIL replay claim'
fi
if ((txg_flush_test));then
    rg -q '^DIAGNOSTIC TXG flush suspended=1 failures=[1-9][0-9]* target=[0-9]+ stopped=[0-9]+ resume=0 synced=0 payload=1 error=0' "$run/create-serial.log"
    for phase in import survivor0 survivor1 reopen;do
        rg -q '^DIAGNOSTIC cold TXG flush payload=1 object=[1-9][0-9]* error=0' "$run/$phase-serial.log"
    done
    echo 'DIAGNOSTIC: real TXG suspension/recovery and four cold payload verifications passed'
fi
echo "PASS: native mirror, DOS access, cold readonly/survivors with zero writes, cold writable reopen on COW disks. Evidence: $run"
