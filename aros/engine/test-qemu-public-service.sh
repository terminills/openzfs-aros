#!/usr/bin/env bash
set -euo pipefail
repo=${AROS_TEST_SOURCE_ROOT:-$(cd -- "$(dirname -- "$0")/../.." && pwd)}
if [[ ${AROS_TEST_RUNNER_SNAPSHOT:-} != "$0" ]];then
    # Bash reads a script incrementally. Execute a private snapshot so source
    # edits during a guest run cannot shift the shell's read offset mid-test.
    source "$repo/aros/engine/storage-preflight.sh"
    snapshot_root=$(oz_artifact_root "$repo" "${AROS_TEST_ARTIFACT_ROOT:-}")
    if [[ -z ${AROS_TEST_ARTIFACT_ROOT:-} ]];then
        snapshot_root="$repo/build-aros"
        mkdir -p "$snapshot_root"
    fi
    snapshot=$(mktemp -d "$snapshot_root/public-runner.XXXXXXXX")
    cp "$0" "$snapshot/runner.sh"
    exec env AROS_TEST_SOURCE_ROOT="$repo" AROS_TEST_RUNNER_SNAPSHOT="$snapshot/runner.sh" \
        bash "$snapshot/runner.sh" "$@"
fi
cd "$repo"
base=${AROS_TEST_ISO:?explicit native test ISO required}
base_platform=${AROS_TEST_BASE_PLATFORM:-0}
[[ $base_platform == 0 || $base_platform == 1 ]]
platform_maps=() platform_inputs=()
if [[ $base_platform == 1 ]];then
    # Never silently combine a current boot image with older ABI overlays.
    [[ -z ${AROS_TEST_ZLIB_LIBRARY:-}${AROS_TEST_DOS_OVERLAY:-}${AROS_TEST_ATA_OVERLAY:-} ]]
    # Both replace the BSP package; never silently let one discard the other.
    [[ -z ${AROS_TEST_BASE_ATA_PACKAGE:-} || -z ${AROS_TEST_BASE_AHCI_PACKAGE:-} ]]
    if [[ -n ${AROS_TEST_BASE_ATA_PACKAGE:-} ]];then
        python3 aros/engine/verify-dos-package-overlay.py "$base" \
            "$AROS_TEST_BASE_ATA_PACKAGE" "${AROS_PACKAGE_TOOL:?package tool required}" ata.device
        platform_maps+=(-map "$AROS_TEST_BASE_ATA_PACKAGE" /boot/pc-smp/aros-bsp.pkg.xz)
        platform_inputs+=("$AROS_TEST_BASE_ATA_PACKAGE")
    fi
    if [[ -n ${AROS_TEST_BASE_DOS_PACKAGE:-} ]];then
        python3 aros/engine/verify-dos-package-overlay.py "$base" \
            "$AROS_TEST_BASE_DOS_PACKAGE" "${AROS_PACKAGE_TOOL:?package tool required}"
        platform_maps+=(-map "$AROS_TEST_BASE_DOS_PACKAGE" /boot/aros-base.pkg.xz)
        platform_inputs+=("$AROS_TEST_BASE_DOS_PACKAGE")
    fi
    if [[ -n ${AROS_TEST_BASE_AHCI_PACKAGE:-} ]];then
        python3 aros/engine/verify-dos-package-overlay.py "$base" \
            "$AROS_TEST_BASE_AHCI_PACKAGE" "${AROS_PACKAGE_TOOL:?package tool required}" ahci.device
        platform_maps+=(-map "$AROS_TEST_BASE_AHCI_PACKAGE" /boot/pc-smp/aros-bsp.pkg.xz)
        platform_inputs+=("$AROS_TEST_BASE_AHCI_PACKAGE")
    fi
else
    zlib=${AROS_TEST_ZLIB_LIBRARY:?explicit native z1.library required}
    dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
    ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
    platform_maps=(-map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz
        -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz
        -map "$zlib" /Libs/z1.library)
    platform_inputs=("$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" "$zlib")
fi
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
service=${AROS_TEST_SERVICE_BINARY:?public service executable required}
client=${AROS_TEST_CLIENT_BINARY:?native acceptance client required}
if [[ ${AROS_TEST_REQUIRE_INIT:-0} == 1 ]] &&
    ! strings "$client" | grep -F 'Public upstream libzfs init PASS' >/dev/null;then
    echo 'Selected acceptance client lacks the required optional libzfs-init probe' >&2;exit 2
fi
if [[ ${AROS_TEST_REQUIRE_CPU:-0} == 1 ]] &&
    ! strings "$client" | grep -F 'Public native CPU topology PASS' >/dev/null;then
    echo 'Selected acceptance client lacks the required optional CPU-topology probe' >&2;exit 2
fi
shared=${AROS_TEST_SHARED_BUILD:-}
cli_dir=${AROS_TEST_CLI_DIR:-}
companions=${AROS_TEST_COMPANION_TOOLS:-0}
[[ $companions == 0 || ( $companions == 1 && -n $cli_dir ) ]]
require_errno=${AROS_TEST_REQUIRE_ERRNO:-0}
[[ $require_errno == 0 || ( $require_errno == 1 && $companions == 1 ) ]]
if [[ $require_errno == 1 ]] &&
    ! strings "$cli_dir/OpenZFSRuntimeCheck" | grep -F -- '--errno-serial' >/dev/null;then
    echo 'Selected RuntimeCheck lacks the required focused errno probe' >&2;exit 2
fi
missing_helper=${AROS_TEST_MISSING_EXEC_HELPER:-0}
[[ $missing_helper == 0 || ( $missing_helper == 1 && $companions == 1 ) ]]
handler=${AROS_TEST_HANDLER_BINARY:-}
device=${AROS_TEST_DEVICE_BINARY:-}
[[ -z $device || -n $cli_dir ]]
pbkdf2=${AROS_TEST_PBKDF2_BINARY:-}
nvpair_test=${AROS_TEST_NVPAIR_BINARY:-}
persistent=${AROS_TEST_PERSISTENT:-0}
[[ $persistent == 0 || $persistent == 1 ]]
cold_release=${AROS_TEST_COLD_RELEASE:-0}
[[ $cold_release == 0 || ( $cold_release == 1 && $persistent == 1 && -n $cli_dir ) ]]
startup_import=${AROS_TEST_STARTUP_IMPORT:-0}
[[ $startup_import == 0 || ( $startup_import == 1 && $persistent == 1 && $companions == 1 ) ]]
startup_archive=${AROS_TEST_STARTUP_ARCHIVE:-0}
[[ $startup_archive == 0 || ( $startup_archive == 1 && $persistent == 1 && $companions == 1 ) ]]
keep_images=${AROS_TEST_KEEP_IMAGES:-0}
[[ $keep_images == 0 || $keep_images == 1 ]]
zvol_fs=${AROS_TEST_ZVOL_FILESYSTEM:-0}
[[ $zvol_fs == 0 || ( $zvol_fs == 1 && -n $device && $persistent == 1 ) ]]
zvol_ffs=${AROS_TEST_ZVOL_FFS:-0}
[[ $zvol_ffs == 0 || ( $zvol_ffs == 1 && $zvol_fs == 1 ) ]]
layout=${AROS_TEST_LAYOUT:-mirror}
case "$layout" in
    mirror) members=2; missing=1 ;;
    raidz1) members=3; missing=1 ;;
    raidz2) members=4; missing=2 ;;
    raidz3) members=5; missing=3 ;;
    draid1) members=3; missing=1 ;;
    draid2) members=4; missing=2 ;;
    draid3) members=5; missing=3 ;;
    *) echo 'AROS_TEST_LAYOUT must be mirror, raidz1/2/3 or draid1/2/3' >&2; exit 2 ;;
esac
[[ $layout == mirror || $persistent == 1 ]]
replace=${AROS_TEST_REPLACE:-0}
[[ $replace == 0 || ( $replace == 1 && $layout == mirror && $persistent == 1 && -n $cli_dir ) ]]
dspare=${AROS_TEST_DSPARE:-0}
[[ $dspare == 0 || ( $dspare == 1 && $layout == draid[123] && $persistent == 1 && -n $cli_dir ) ]]
sequential=${AROS_TEST_SEQUENTIAL:-0}
[[ $sequential == 0 || ( $sequential == 1 && $dspare == 1 ) ]]
[[ $startup_import == 0 || ( $layout == mirror && $replace == 0 && $dspare == 0 ) ]]
gui=${AROS_PUBLIC_GUI:-0}
gui_defer=${AROS_TEST_GUI_DEFER:-0}
[[ $gui_defer == 0 || ( $gui_defer == 1 && $gui == 1 && -n $cli_dir ) ]]
gui_destroy_preview=${AROS_TEST_GUI_DESTROY_PREVIEW:-0}
[[ $gui_destroy_preview == 0 || ( $gui_destroy_preview == 1 && $gui_defer == 1 ) ]]
gui_key_check=${AROS_TEST_GUI_KEY_CHECK:-0}
[[ $gui_key_check == 0 || ( $gui_key_check == 1 && $gui == 1 && -n $cli_dir ) ]]
gui_key_change=${AROS_TEST_GUI_KEY_CHANGE:-0}
[[ $gui_key_change == 0 || ( $gui_key_change == 1 && $gui_key_check == 1 ) ]]
gui_desktop=${AROS_TEST_GUI_DESKTOP:-0}
gui_clone=${AROS_TEST_GUI_CLONE:-0}
[[ $gui_clone == 0 || ( $gui_clone == 1 && $gui == 1 && -n $cli_dir ) ]]
[[ $gui_desktop == 0 || ( $gui_desktop == 1 && $gui == 1 ) ]]
gui_volume=${AROS_TEST_GUI_VOLUME:-0}
[[ $gui_volume == 0 || ( $gui_volume == 1 && $gui == 1 && -n $device && -n $cli_dir ) ]]
cpus=${AROS_TEST_CPUS:-2}
qemu=${AROS_TEST_QEMU:-qemu-system-x86_64}
ide_trace=${AROS_TEST_IDE_TRACE:-0}
worker_trace=${AROS_TEST_WORKER_TRACE:-0}
[[ $worker_trace == 0 || $worker_trace == 1 ]]
host_read_trace=${AROS_TEST_HOST_READ_TRACE:-0}
[[ $host_read_trace == 0 || $host_read_trace == 1 ]]
disk_aio=${AROS_TEST_DISK_AIO:-default}
case "$disk_aio" in
    default) disk_aio_option= ;;
    threads|io_uring) disk_aio_option=",aio=$disk_aio" ;;
    *) echo 'AROS_TEST_DISK_AIO must be default, threads or io_uring' >&2; exit 2 ;;
esac
[[ $ide_trace == 0 || $ide_trace == 1 ]]
[[ $cpus =~ ^[1-8]$ ]] || { echo 'AROS_TEST_CPUS must be 1 through 8' >&2; exit 2; }
prefix=public-service-run
startup=aros/engine/Startup-Public-Service
duration=600
extra_maps=()
if [[ $startup_import == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-startup-import-test)
fi
if [[ $gui_desktop == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-gui-desktop-test)
fi
if [[ $gui_clone == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-gui-clone-test)
fi
if [[ $gui_defer == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-gui-defer-test)
fi
if [[ $gui_key_check == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-gui-key-check-test)
fi
if [[ $gui_key_change == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-gui-key-change-test)
fi
if [[ $gui_destroy_preview == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-gui-destroy-preview-test)
fi
if [[ $gui_volume == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-gui-volume-test)
fi
if [[ $zvol_fs == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-zvol-filesystem-test)
fi
if [[ $zvol_ffs == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-zvol-ffs-test)
fi
if [[ $replace == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-replacement-test)
fi
if [[ $dspare == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-dspare-test)
fi
if [[ $sequential == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-sequential-test)
fi
if [[ -n $shared ]];then
    shared=$(realpath -e "$shared")
    rg -Fxq 'service_test_modes=0' "$shared/service-build-options.txt"
    rg -Fxq 'full_dispatcher=1' "$shared/service-build-options.txt"
    rg -Fxq 'shared_engine=1' "$shared/service-build-options.txt"
    sha256sum -c "$shared/binaries.sha256"
    cmp "$service" "$shared/OpenZFSServiceNative"
    # xorriso preserves a selected symlink as a guest symlink. Resolve the
    # checked library input so a build/staging link cannot become a dangling
    # or self-referential Libs: entry in the private test ISO.
    shared_library=$(realpath -e "$shared/openzfs.library")
    extra_maps+=(-map "$shared_library" /Libs/openzfs.library)
fi
if [[ ${AROS_TEST_LOADER_POLICY:-0} == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-loader-policy-test)
fi
extra_drives=()
archive_cold_drives=()
partitions=${AROS_TEST_REQUIRE_PARTITIONS:-0}
native_discovery=${AROS_TEST_NATIVE_DISCOVERY:-0}
native_cold=${AROS_TEST_NATIVE_COLD:-0}
native_changes=${AROS_TEST_NATIVE_CHANGES:-0}
[[ $native_changes =~ ^[0-6]$ ]]
all_native=0
if [[ $native_changes == 5 || $native_changes == 6 ]];then all_native=1;fi
native_healing=${AROS_TEST_NATIVE_HEALING:-0}
[[ $native_healing == 0 || ( $native_healing == 1 && $native_changes == 6 ) ]]
if [[ $native_changes != 0 ]];then
    [[ $native_discovery == 1 && $native_cold == 0 && $gui == 0 ]]
    extra_maps+=(-map aros/engine/public-service-test-only "/openzfs-native-changes-$native_changes")
fi
[[ $native_cold == 0 || ( $native_cold == 1 && $native_discovery == 1 && $gui == 0 ) ]]
if [[ $native_cold == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-native-cold-test)
fi
[[ $native_discovery == 0 || ( $native_discovery == 1 && $partitions == 1 && $persistent == 0 && -n $cli_dir ) ]] || {
    echo 'Native discovery requires PARTITIONS=1, CLI_DIR and PERSISTENT=0; use NATIVE_COLD=1 for native-device cold boots (PERSISTENT=1 selects file-backed pools).' >&2
    exit 2
}
if [[ $native_discovery == 1 ]];then
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-native-discovery-test)
fi
ahci_query=${AROS_TEST_AHCI_QUERY:-0}
[[ $ahci_query == 0 || ( $ahci_query == 1 && $partitions == 1 ) ]]
sector_quirk=${AROS_TEST_SECTOR_QUIRK:-0}
[[ $sector_quirk == 0 || $sector_quirk == 1 ]] || exit 2
[[ $sector_quirk == 0 || $partitions == 1 ]] || exit 2
label_source=${AROS_TEST_LABEL_FIXTURE:-}
[[ $all_native == 0 || ( -z $label_source && $ahci_query == 0 ) ]]
if [[ -n $label_source ]];then
    [[ $partitions == 1 && ${AROS_TEST_REQUIRE_INIT:-0} == 1 ]] || {
        echo 'Label fixture requires PARTITIONS and INIT tests' >&2; exit 2; }
    label_source=$(realpath -e -- "$label_source")
    case "$label_source" in
        "$repo"/build-aros/qemu-run.*/disk[01].raw) ;;
        *) echo 'Only an existing disposable QEMU pool fixture is accepted' >&2; exit 2;;
    esac
    [[ -f $label_source && $(stat -c %s -- "$label_source") == 536870912 ]] || exit 2
fi
[[ $partitions == 0 || $partitions == 1 ]] || { echo 'AROS_TEST_REQUIRE_PARTITIONS must be 0 or 1' >&2; exit 2; }
if [[ $gui == 1 ]];then
    prefix=native-gui-run
    startup=aros/engine/Startup-Public-Service-GUI
    duration=1800
fi
source "$repo/aros/engine/storage-preflight.sh"
mkdir -p "$repo/build-aros"
artifact_root=$(oz_artifact_root "$repo" "${AROS_TEST_ARTIFACT_ROOT:-}")
if [[ -z ${AROS_TEST_ARTIFACT_ROOT:-} ]];then
    artifact_root="$repo/build-aros"
    mkdir -p "$artifact_root"
fi
run=$(mktemp -d "$artifact_root/$prefix.XXXXXXXX")
printf '%s\n' "$missing_helper" >"$run/missing-exec-helper.txt"
cp "$0" "$run/runner.sh"
sha256sum "$run/runner.sh" >"$run/runner.sha256"
echo "Evidence: $run"
qemu_path=$(command -v "$qemu")
"$qemu" --version > "$run/qemu-version.txt"
sha256sum "$(realpath -e "$qemu_path")" > "$run/qemu-binary.sha256"
printf '%s\n' "$cpus" > "$run/cpus.txt"
printf '%s\n' "$persistent" > "$run/persistent.txt"
printf '%s\n' "$native_changes" > "$run/native-changes.txt"
printf '%s\n' "$disk_aio" > "$run/disk-aio.txt"
trace_args=()
launcher=()
if [[ $host_read_trace == 1 ]];then
    command -v strace > "$run/strace-path.txt"
    strace --version > "$run/strace-version.txt"
    # -D leaves QEMU as the shell's child, preserving the existing PID-based
    # cleanup. Trace only host read calls, not guest memory or credentials.
    launcher=(strace -D -f --seccomp-bpf -ttt -T -e trace=pread64,preadv,preadv2
        -e raw=pread64,preadv,preadv2 -o "$run/host-reads.log")
fi
if [[ $ide_trace == 1 || $worker_trace == 1 ]];then
    if [[ $ide_trace == 1 ]];then
        cp aros/engine/qemu-ide-trace-events "$run/ide-trace-events"
    else
        # Worker-only tracing avoids the much busier IDE/AIO trace stream.
        : > "$run/ide-trace-events"
    fi
    if [[ $worker_trace == 1 ]];then
        "$qemu" -trace help > "$run/qemu-trace-events.txt" 2>&1
        for event in thread_pool_worker_start thread_pool_worker_return thread_pool_worker_notified thread_pool_worker_delivered;do
            rg -q "^$event$" "$run/qemu-trace-events.txt" || { echo "QEMU lacks $event" >&2; exit 2; }
            echo "$event" >> "$run/ide-trace-events"
        done
        cp aros/engine/qemu-worker-trace.patch "$run/"
    fi
    sha256sum "$run/ide-trace-events" > "$run/trace-inputs.sha256"
    if [[ $worker_trace == 1 ]];then
        sha256sum "$run/qemu-worker-trace.patch" >> "$run/trace-inputs.sha256"
    fi
    trace_args=(-trace "events=$run/ide-trace-events,file=$run/ide-trace.log")
fi
cp "$service" "$run/OpenZFSServiceNative"
cp "$client" "$run/OpenZFSPublicServiceTest"
cp "$startup" "$run/Startup-Sequence"
printf '%s\n' "$layout" >"$run/layout.txt"
extra_maps+=(-map "$run/layout.txt" /openzfs-test-layout)
if [[ $persistent == 1 ]];then
    # Only a newly created regular file, never a supplied or physical disk.
    [[ $gui == 0 && $partitions == 0 && -z $label_source && $ahci_query == 0 ]] || {
        echo 'Persistent file-vdev tests require GUI=0, PARTITIONS=0, no label fixture and AHCI_QUERY=0' >&2
        exit 2
    }
    fat=${AROS_TEST_FAT_HANDLER:?explicit flush-capable FAT handler required}
    host_zdb=${AROS_TEST_HOST_ZDB:?explicit host zdb required for persistent verification}
    command -v fsck.fat >/dev/null
    [[ -x $host_zdb ]]
    sha256sum "$host_zdb" >"$run/host-zdb.sha256"
    cp "$fat" "$run/fat-handler"
    backing_mib=$((members*128+256))
    if [[ -n $cli_dir ]];then backing_mib=$((backing_mib+384));fi
    truncate -s "${backing_mib}M" "$run/backing.raw"
    mkfs.vfat -F 16 -n OZFILES "$run/backing.raw" >"$run/format.log" 2>&1
    sed "s/^HighCyl .*/HighCyl = $((backing_mib*2048-1))/" aros/engine/OZBACK >"$run/OZBACK"
    sed '/^Stack /a Mount SYS:OZBACK' "$startup" >"$run/Startup-Sequence"
    extra_maps+=(-map "$run/fat-handler" /L/fat-handler -map "$run/OZBACK" /OZBACK
        -map aros/engine/public-service-test-only /openzfs-native-file-fixture)
    extra_drives+=(-drive "file=$run/backing.raw,format=raw,if=ide,index=0")
    sha256sum "$run/fat-handler" "$run/OZBACK" >"$run/backing-inputs.sha256"
    if [[ $startup_archive == 1 ]];then
        truncate -s 32M "$run/startup-archive.raw"
        cp aros/engine/OZARCH "$run/OZARCH"
        sha256sum "$run/OZARCH" >"$run/archive-inputs.sha256"
        extra_maps+=(-map "$run/OZARCH" /OZARCH
            -map aros/engine/public-service-test-only /openzfs-startup-archive-test)
        extra_drives+=(-drive "file=$run/startup-archive.raw,format=raw,if=ide,index=1")
        # IDE hard disks require a writable QEMU block node. Verify absence of
        # cold-restore writes with the full-image before/after hash below.
        archive_cold_drives=(-drive "file=$run/startup-archive.raw,format=raw,if=ide,index=1")
    fi
fi
if [[ -n $pbkdf2 ]];then
    cp "$pbkdf2" "$run/OpenZFSPBKDF2Test"
    sha256sum "$run/OpenZFSPBKDF2Test" >"$run/pbkdf2-input.sha256"
    extra_maps+=(-map "$run/OpenZFSPBKDF2Test" /C/OpenZFSPBKDF2Test)
    sed -i '/^Stack /a C:OpenZFSPBKDF2Test' "$run/Startup-Sequence"
fi
if [[ -n $nvpair_test ]];then
    cp "$nvpair_test" "$run/OpenZFSNvpairTest"
    sha256sum "$run/OpenZFSNvpairTest" >"$run/nvpair-input.sha256"
    extra_maps+=(-map "$run/OpenZFSNvpairTest" /C/OpenZFSNvpairTest)
    sed -i '/^Stack /a C:OpenZFSNvpairTest RAM:nvpair-aros.xdr' "$run/Startup-Sequence"
fi
if [[ -n $cli_dir ]];then
    cp "$cli_dir/zfs" "$cli_dir/zpool" "$run/"
    extra_maps+=(-map "$run/zfs" /C/zfs -map "$run/zpool" /C/zpool
        -map aros/engine/public-service-test-only /openzfs-cli-package-test)
    sha256sum "$run/zfs" "$run/zpool" >"$run/cli-inputs.sha256"
    if [[ $companions == 1 ]];then
        cp "$cli_dir/../Tools/OpenZFS/ExecHelper" "$run/ExecHelper"
        extra_maps+=(-map "$run/ExecHelper" /Tools/OpenZFS/ExecHelper)
        # Negative installation diagnostic: remove only this private ISO's
        # mapped helper. The suite must fail; never reinterpret this as PASS.
        if [[ $missing_helper == 1 ]];then
            extra_maps+=(-rm /Tools/OpenZFS/ExecHelper --)
        fi
        sha256sum "$run/ExecHelper" >>"$run/cli-inputs.sha256"
        for command in OpenZFSStartup OpenZFSStartupMount OpenZFSRuntimeCheck OpenZFSHostIdentity;do
            cp "$cli_dir/$command" "$run/$command"
            extra_maps+=(-map "$run/$command" "/C/$command")
            sha256sum "$run/$command" >>"$run/cli-inputs.sha256"
        done
        extra_maps+=(-map aros/engine/public-service-test-only /openzfs-companion-tools-test)
    fi
fi
if [[ -n $handler ]];then
    cp "$handler" "$run/OpenZFS-Handler"
    printf '%s\n' 'FileSystem = L:OpenZFS-Handler' 'Device = openzfs' \
        'Unit = 0' 'Flags = 0' 'Surfaces = 1' 'BlocksPerTrack = 1' \
        'LowCyl = 0' 'HighCyl = 0' 'DosType = 0x5a465301' \
        'Control = "public_mirror/files"' 'StackSize = 262144' \
        'Activate = 0' >"$run/OZPACKAGE"
    extra_maps+=(-map "$run/OpenZFS-Handler" /L/OpenZFS-Handler
        -map "$run/OZPACKAGE" /OZPACKAGE
        -map aros/engine/public-service-test-only /openzfs-handler-package-test)
    sha256sum "$run/OpenZFS-Handler" "$run/OZPACKAGE" >"$run/handler-inputs.sha256"
fi
if [[ $gui == 1 ]];then
    cp "${AROS_TEST_MANAGER_BINARY:?native Manager required for GUI test}" "$run/OpenZFSManagerNative"
    extra_maps+=(-map "$run/OpenZFSManagerNative" /C/OpenZFSManagerNative)
    sha256sum "$run/OpenZFSManagerNative" >"$run/gui-inputs.sha256"
fi
if [[ $partitions == 1 ]];then
    sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
    python3 aros/platform/make-partition-fixture.py mbr "$run/partition.raw"
    qemu-img create -f raw "$run/blank.raw" 256M
    sha256sum "$run/partition.raw" "$run/blank.raw" > "$run/disks-before.sha256"
    sha256sum "${sdk%/Developer}/Libs/partition.library" aros/platform/make-partition-fixture.py > "$run/partition-inputs.sha256"
    extra_maps+=(-map "${sdk%/Developer}/Libs/partition.library" /Libs/partition.library
        -map aros/engine/public-service-test-only /openzfs-partition-test-only)
    extra_drives=(-drive "file=$run/partition.raw,format=raw,if=ide,index=0,snapshot=on$disk_aio_option"
        -drive "file=$run/blank.raw,format=raw,if=ide,index=1,snapshot=on$disk_aio_option")
    if [[ $native_cold == 1 || $all_native == 1 ]];then
        # Only this newly generated private fixture persists between guests.
        extra_drives[1]="file=$run/partition.raw,format=raw,if=ide,index=0$disk_aio_option"
    fi
    if [[ $all_native == 1 ]];then
        python3 aros/platform/make-partition-fixture.py mbr-three "$run/changes.raw"
        sha256sum "$run/changes.raw" >> "$run/disks-before.sha256"
        if [[ $native_changes == 6 ]];then
            extra_drives+=(-device ich9-ahci,id=ozchanges
                -drive "file=$run/changes.raw,format=raw,if=none,id=changes$disk_aio_option"
                -device ide-hd,drive=changes,bus=ozchanges.0)
        else
            extra_drives+=(-drive "file=$run/changes.raw,format=raw,if=ide,index=3$disk_aio_option")
        fi
    fi
    if [[ $sector_quirk == 1 ]];then
        extra_drives+=(-global 'ide-hd.model=ATA     ADATA SSD S396 3')
        extra_maps+=(-map aros/engine/public-service-test-only /openzfs-sector-quirk-test)
    fi
fi
if [[ -n $label_source ]];then
    sha256sum "$label_source" >"$run/label-original.sha256"
    cp --reflink=auto --sparse=always -- "$label_source" "$run/labeled.raw"
    sha256sum -c "$run/label-original.sha256" >"$run/label-copy-check.txt"
    original_hash=$(sha256sum "$label_source");copy_hash=$(sha256sum "$run/labeled.raw")
    [[ ${original_hash%% *} == "${copy_hash%% *}" ]] || exit 1
    sha256sum "$run/labeled.raw" >>"$run/disks-before.sha256"
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-label-test-only)
    extra_drives+=(-drive "file=$run/labeled.raw,format=raw,if=ide,index=3,snapshot=on$disk_aio_option")
fi
if [[ $ahci_query == 1 ]];then
    qemu-img create -f raw "$run/ahci.raw" 64M
    sgdisk --clear --new=1:2048:0 --typecode=1:BF01 "$run/ahci.raw" >"$run/ahci-gpt.log" 2>&1
    sha256sum "$run/ahci.raw" >>"$run/disks-before.sha256"
    extra_maps+=(-map aros/engine/public-service-test-only /openzfs-ahci-query-test)
    extra_drives+=(-device ich9-ahci,id=ozahci
        -drive "file=$run/ahci.raw,format=raw,if=none,id=ahci-query,snapshot=on$disk_aio_option"
        -device ide-hd,drive=ahci-query,bus=ozahci.0)
fi
cp -a aros/engine/security-fixture "$run/security-fixture"
if [[ $zvol_ffs == 1 ]];then
    cp "${AROS_TEST_AFS_HANDLER:?explicit AFS handler required}" "$run/afs-handler"
    sha256sum "$run/afs-handler" >"$run/afs-handler.sha256"
    extra_maps+=(-map "$run/afs-handler" /L/afs-handler)
fi
if [[ -n $device ]];then
    cp "$device" "$run/OpenZFSDevice"
    sha256sum "$run/OpenZFSDevice" >"$run/device-input.sha256"
    extra_maps+=(-map "$run/OpenZFSDevice" /C/OpenZFSDevice
        -map aros/engine/public-service-test-only /openzfs-device-package-test)
fi
if [[ $require_errno == 1 ]];then
    # FailAt 20 in both startup templates aborts on the probe's failure code.
    # Keep this before service launch, not merely in the later companion test.
    sed -i '/^Stack /a C:OpenZFSRuntimeCheck --errno-serial >NIL:\nIf WARN\n  Quit 20\nEndIf' "$run/Startup-Sequence"
fi
tar -czf "$run/aros-source.tar.gz" aros/engine aros/ui aros/zpl aros/cli aros/platform aros/device \
    module/zfs/zfs_onexit.c include/sys/zfs_onexit.h lib/libzutil/zutil_import.c include/libzutil.h
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
sha256sum "$base" "$run/OpenZFSServiceNative" "$run/OpenZFSPublicServiceTest" \
    "${platform_inputs[@]}" "$posix" >"$run/inputs.sha256"
printf '%s\n' "$base_platform" >"$run/base-platform.txt"
if [[ -n $shared ]];then sha256sum "$shared/openzfs.library" >>"$run/inputs.sha256";fi
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    "${platform_maps[@]}" \
    -map "$posix" /Libs/posixc.library \
    -map "$run/security-fixture" /Security \
    -map "$run/OpenZFSServiceNative" /C/OpenZFSServiceNative \
    -map "$run/OpenZFSPublicServiceTest" /C/OpenZFSPublicServiceTest \
    "${extra_maps[@]}" \
    -map aros/engine/public-service-test-only /openzfs-service-test-only \
    -map "$run/Startup-Sequence" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
"${launcher[@]}" "$qemu" -name "openzfs-public-service-$$" -machine pc,accel=kvm -cpu host -smp "$cpus" -m 2048 \
    "${trace_args[@]}" \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    "${extra_drives[@]}" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -device qemu-xhci -device usb-tablet -qmp "unix:$run/qmp.sock,server=on,wait=off" \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
capture_failure(){
    [[ ${AROS_TEST_CAPTURE_FAILURE:-0} == 1 ]] || return 0
    # Only this harness-owned QEMU, and only after a confirmed test failure.
    # Observation errors must not hide the original failure or skip cleanup.
    printf 'info blockstats\ninfo block\nstop\ninfo blockstats\n' | timeout 5 socat - "UNIX-CONNECT:$run/monitor" > "$run/failure-monitor.log" 2>&1 || true
    timeout 20 gdb -q -batch -ex 'set pagination off' -ex 'thread apply all bt' \
        -ex detach -p "$pid" > "$run/failure-host-backtrace.log" 2>&1 || true
}
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+duration))
while :;do
    if [[ -f "$run/serial.log" ]];then
        # Preserve raw bytes; strip only the standard AROS kernel line prefix.
        sed -E 's/^[[:xdigit:]]{16} 0x[[:xdigit:]]{16} \| [0-9]+ \| //' "$run/serial.log" > "$run/serial-results.log"
        if rg -q '^RESULT .*FAIL|Public service test line=[1-9][0-9]*|\[PF-DBG\]|^ASSERT at' "$run/serial-results.log";then capture_failure;exit 1;fi
        count=$(rg -c '^RESULT public service lifecycle PASS' "$run/serial-results.log" || true)
        sessions=$(rg -c '^Public authenticated sessions ownership, capacity, stale handles and shutdown guard PASS' "$run/serial-results.log" || true)
        reaped=$(rg -c '^Public task-exit session reclamation PASS cycles=4 sessions=32' "$run/serial-results.log" || true)
        controls=$(rg -c '^Public native control descriptors PASS capacity=64 ownership=1 stale=1 session-cleanup=1' "$run/serial-results.log" || true)
        if [[ $count == 3 && $sessions == 2 && $reaped == 2 ]] &&
           { [[ ${AROS_TEST_REQUIRE_ACCOUNTING_UPGRADE:-0} != 1 ]] || {
               rg -q '^Public accounting upgrade delegation PASS denied=1 granted=1 revoked=1' "$run/serial-results.log" &&
               rg -q '^Public accounting upgrade authorization PASS changed-user=1 own-session-denied=1' "$run/serial-results.log" &&
               rg -q '^Public accounting mounted upgrade PASS writable=1 repeat=1' "$run/serial-results.log" &&
               rg -q '^Public accounting upgrade PASS readonly=1 malformed=1 absent-gated=1' "$run/serial-results.log"; }; } &&
           { [[ ${AROS_TEST_REQUIRE_BOOTENV:-0} != 1 ]] || {
               rg -q '^Public upstream bootenv PASS get=1 set=1 invalid-preserved=1 restored=1' "$run/serial-results.log" &&
               rg -q '^Public upstream bootenv authorization PASS changed-user=1 own-session-denied=1 reader-get=1 unchanged=1' "$run/serial-results.log" &&
               rg -q '^Public upstream bootenv readonly PASS get=1 set-erofs=1 unchanged=1' "$run/serial-results.log"; }; } &&
           { [[ $require_errno == 0 ]] || rg -q '^RESULT pre-storage errno PASS' "$run/serial-results.log"; } &&
           { [[ -z $pbkdf2 ]] || rg -q '^RESULT native PBKDF2 known answers PASS' "$run/serial-results.log"; } &&
           { [[ -z $nvpair_test ]] || rg -q '^RESULT native nvpair XDR PASS' "$run/serial-results.log"; } &&
           { [[ ${AROS_TEST_REQUIRE_INVENTORY_WORKER:-0} != 1 ]] ||
             rg -q '^RESULT native inventory worker PASS delayed=1 busy-free=1 success=1 denied=1 malformed=10 absent=1 unchanged=1' "$run/serial-results.log"; } &&
           { [[ ${AROS_TEST_REQUIRE_CONTROL:-0} != 1 ]] ||
             { [[ $controls == 2 ]] && rg -q '^Public upstream ioctl transport PASS stats=1 configs=1 resize=1 bounds=1 stale=1' "$run/serial-results.log" &&
               rg -q '^Public upstream snapshot ioctls PASS create=1 hold=1 onexit=1 release=1 destroy=1 schema=1' "$run/serial-results.log" &&
               rg -q '^Public upstream snapshot authorization PASS changed-user=1 own-session-denied=1 no-mutation=1' "$run/serial-results.log" &&
               rg -q '^Public upstream libzfs_core PASS snapshot=1 temporary-hold=1 destroy=1 reopen=1' "$run/serial-results.log" &&
               rg -q '^Public upstream dataset ioctls PASS create=1 clone=1 rollback=1 volume=1 handler-ready=1' "$run/serial-results.log" &&
               rg -q '^Public upstream enumeration PASS dataset=1 snapshot=1 properties=1 end=1' "$run/serial-results.log"; }; } &&
           rg -q '^Public native mount snapshot PASS empty=1 writable=1 readonly=1 retired-absent=1 bounds=1' "$run/serial-results.log" &&
           rg -q '^Public native path dataset PASS file=1 alias=1 unrelated=1 absent=1 bounds=1' "$run/serial-results.log" &&
           rg -q '^Public native root resolution PASS alias=1 file-rejected=1 unrelated=1 absent=1' "$run/serial-results.log" &&
           { [[ ${AROS_TEST_REQUIRE_MOUNT_CACHE:-0} != 1 ]] || {
               [[ $(rg -c 'Public upstream mount cache PASS state=empty iterations=32' "$run/serial-results.log" || true) == 3 ]] &&
               rg -q 'Public upstream mount cache PASS state=OZPUBLIC: iterations=32' "$run/serial-results.log" &&
               rg -q 'Public upstream mount cache PASS state=OZPUBLICRO: iterations=32' "$run/serial-results.log"; }; } &&
           rg -q '^Public native supported tables PASS feature=1 properties=3 unknown=1 missing=1' "$run/serial-results.log" &&
           rg -q '^Public native engine version PASS query=1 upstream=1 bounds=1 missing=1' "$run/serial-results.log" &&
           { [[ ${AROS_TEST_REQUIRE_CPU:-0} != 1 ]] ||
             rg -q "^Public native CPU topology PASS cpus=$cpus queries=128" "$run/serial-results.log"; } &&
           rg -q '^Public native mount policy PASS properties=4 transitions=8 missing=1 absent=1' "$run/serial-results.log" &&
           rg -q '^Public native shares object PASS absent=1 unmounted=1 missing=1 invalid=1' "$run/serial-results.log" &&
           rg -q '^Public native mount control PASS writable=1 readonly=1 busy=1 duplicate=1 absent=1 invalid=1' "$run/serial-results.log" &&
           { [[ ${AROS_TEST_REQUIRE_INIT:-0} != 1 ]] || {
               rg -q 'Public upstream libzfs init PASS cycles=80 simultaneous=2' "$run/serial-results.log" &&
               rg -q "Public native CPU topology PASS cpus=$cpus queries=128" "$run/serial-results.log" &&
               rg -q 'Public native init diagnostics PASS missing=1 denied=1 protocol=1 settings=1 memory=1' "$run/serial-results.log" &&
               rg -q 'Public upstream hostid PASS service-identity=1 client-environment-ignored=1' "$run/serial-results.log" &&
               rg -q 'Public upstream property ioctls PASS set=4 inherit=4 fresh-read=8 denied-set=1 denied-inherit=1 no-mutation=1' "$run/serial-results.log" &&
               rg -q 'Public upstream property sources PASS received=1 local-precedence=1 revert-received=1 invalid-value=1 invalid-source=1' "$run/serial-results.log" &&
               rg -q 'Public upstream snapshot lifecycle PASS busy-preserved=1 detach=1 destroy=1 absent=1' "$run/serial-results.log" &&
               rg -q '^Public upstream promotion PASS denied=1 unchanged=1 conflict=1 promote=1 origins=1 repeat-invalid=1 cleanup=1' "$run/serial-results.log" &&
               rg -q '^Public upstream promotion mount guard PASS busy=1 endpoint-retained=1 origin-unchanged=1' "$run/serial-results.log" &&
               rg -q '^Public upstream readonly promotion PASS reimport=1 denied=1 origins-unchanged=1 snapshot-guid=1' "$run/serial-results.log" &&
               grep -q '^Public upstream file flush PASS write=1 reopen=1 invalid=1 closed=1 readonly=1' "$run/serial-results.log" &&
               [[ $(rg -c '^Public upstream pool sync PASS phase=[012] ordinary=1 forced=1 errno=' "$run/serial-results.log") == 3 ]] &&
               rg -q '^Public upstream bookmark lifecycle PASS create=1 duplicate=1 guid=1 list=1 survives-snapshot=1 destroy=1 absent=1' "$run/serial-results.log" &&
               rg -q '^Public upstream bookmark authorization PASS own-session=1 create-denied=1 destroy-denied=1 unchanged=1' "$run/serial-results.log" &&
               rg -q '^Public upstream bookmark delegation PASS grant=1 fresh-acl=1 delegated-create=1 delegated-destroy=1 revoke=1 denied-again=1' "$run/serial-results.log" &&
               rg -q 'Public upstream shares object PASS native-metadata=1 absent=1' "$run/serial-results.log" &&
               rg -q 'Public upstream file discovery PASS members=2 identity=1 mismatch=1 directory=1 missing=1' "$run/serial-results.log" &&
               [[ $(rg -c 'Public upstream file vdev PASS phase=[012] members=2 force-policy=1 missing=1 directory=1 query-failure=1' "$run/serial-results.log" || true) == 3 ]] &&
               rg -q 'Public upstream enclosure unsupported PASS phase=0 query=1 on=1 off=1' "$run/serial-results.log" &&
               rg -q 'Public upstream enclosure unsupported PASS phase=2 query=1 on=1 off=1' "$run/serial-results.log" &&
               rg -q '^Public upstream pool property authorization PASS own-session-denied=1 fresh-read=1 no-mutation=1' "$run/serial-results.log" &&
               rg -q '^Public upstream upgrade notice PASS phase=0 unset=1 configured=1 no-boot-write=1 readonly-denied=0' "$run/serial-results.log" &&
               rg -q '^Public upstream upgrade notice PASS phase=2 unset=1 configured=0 no-boot-write=1 readonly-denied=1' "$run/serial-results.log" &&
               rg -q 'Public upstream mount adapter PASS real-handle=1 mount=1 unmount=1 unsupported=1 duplicate=1' "$run/serial-results.log" &&
               rg -q 'Public upstream init failure cleanup PASS cycles=80 survivor=1 errno=EINVAL' "$run/serial-results.log"; }; } &&
           { [[ $partitions != 1 ]] || {
               rg -q '^Public native partition RPC PASS cycles=16 exact-extents=1 slots=1 bounds=1 malformed=1 denied=1 restored=1 unchanged-output=1' "$run/serial-results.log" &&
               rg -q '^Public native inquiry PASS denied=0 absent=1 malformed=1 unchanged=1' "$run/serial-results.log" &&
               rg -q '^Public native inquiry PASS denied=1 unchanged=1' "$run/serial-results.log" &&
               { [[ $ahci_query == 0 ]] || {
                   rg -q '^Public AHCI inquiry PASS denied=0 identity=1 geometry=1' "$run/serial-results.log" &&
                   rg -q '^Public AHCI inquiry PASS denied=1 unchanged=1' "$run/serial-results.log";
               }; } &&
               rg -q '^Public native inventory PASS denied=0 cycles=8 units=1 optical=1 identity=1 malformed=1 absent=1' "$run/serial-results.log" &&
               rg -q '^Public native inventory PASS denied=1 unchanged=1' "$run/serial-results.log" &&
               { [[ ${AROS_TEST_REQUIRE_INIT:-0} != 1 ]] || {
                   rg -q "^Public native sector quirk PASS match=$sector_quirk partition=1 absent=1 unchanged=1" "$run/serial-results.log" &&
                   rg -q '^Public native DOS preflight PASS inactive=1 whole=1 partition=1 force-denied=1' "$run/serial-results.log" &&
                   rg -q '^Public native DOS claim RPC PASS denied=0 missing=1 malformed=1' "$run/serial-results.log" &&
                   rg -q '^Public native DOS claim RPC PASS denied=1' "$run/serial-results.log" &&
                   rg -q '^Public native device preflight PASS denied=0 blank=1 type=1 missing=1 malformed=1' "$run/serial-results.log" &&
                   rg -q '^Public native device preflight PASS denied=1 force-denied=1' "$run/serial-results.log" &&
                   rg -q '^Public native default discovery PASS denied=1 no-cache=1' "$run/serial-results.log"; }; } &&
               [[ $(rg -c '^Public native device read PASS denied=0 mbr=1 blank=8 last-sector=1 max=1 bounds=1 unchanged=1' "$run/serial-results.log" || true) == 2 ]] &&
               [[ $(rg -c '^Public native device read PASS denied=1 unchanged=1' "$run/serial-results.log" || true) == 1 ]]; }; } &&
           { [[ -z $label_source ]] || {
               [[ $(rg -c '^Public native default discovery PASS registered=1 partitions=1 optical-excluded=1 pool=1 shortname=1 bounded=1' "$run/serial-results.log" || true) == 2 ]] &&
               [[ $(rg -c '^Public native device preflight PASS exported=1 force=1 query-failure=1 whole-children=1' "$run/serial-results.log" || true) == 2 ]] &&
               [[ $(rg -c '^Public native retained device PASS denied=0 lifetime=1' "$run/serial-results.log" || true) == 2 ]] &&
               [[ $(rg -c '^Public native retained device PASS denied=1 lifetime=1' "$run/serial-results.log" || true) == 1 ]] &&
               [[ $(rg -c '^Public native scanner session budget PASS one-slot=1 parent-owned=1 cleanup=1 exhausted-error=1' "$run/serial-results.log" || true) == 2 ]] &&
               [[ $(rg -c 'Public upstream native labeled discovery PASS labels=4 repeats=4 identity=1 hook=1 mismatch=1 extent=1' "$run/serial-results.log" || true) == 2 ]] &&
               [[ $(rg -c 'Public upstream native labels PASS denied=1 access-flag=1' "$run/serial-results.log" || true) == 1 ]] &&
               [[ $(rg -c 'Public upstream native scanner PASS denied=0 explicit=1 duplicate=1 malformed=1' "$run/serial-results.log" || true) == 2 ]] &&
               [[ $(rg -c 'Public upstream native in-use policy PASS exported=1 query-failure=1 blank=1 missing=1' "$run/serial-results.log" || true) == 2 ]] &&
               [[ $(rg -c 'Public upstream auxiliary query failure PASS spare=1 cache=1' "$run/serial-results.log" || true) == 2 ]] &&
               [[ $(rg -c 'Public native read repetition PASS cycles=32 alternating=1 identity=1' "$run/serial-results.log" || true) == 2 ]] &&
               [[ $(rg -c 'Public upstream native scanner PASS denied=1 explicit=1 duplicate=1 malformed=1' "$run/serial-results.log" || true) == 1 ]]; }; } &&
           { [[ ${AROS_TEST_LOADER_POLICY:-0} != 1 ]] ||
             rg -q '^RESULT native loader execution policy PASS' "$run/serial-results.log"; } &&
           { [[ -z $cli_dir ]] || {
             rg -q '^Public packaged CLI PASS' "$run/serial-results.log" &&
             { [[ ${AROS_TEST_REQUIRE_TRIM:-0} != 1 ]] ||
               rg -q '^Public CLI TRIM unsupported PASS start=1 suspend=1 cancel=1 upstream-device-errors=1' "$run/serial-results.log"; } &&
             rg -q '^Public packaged replication PASS .*incremental=1' "$run/serial-results.log"; }; } &&
           { [[ -z $handler ]] ||
             rg -q '^Public packaged handler PASS' "$run/serial-results.log"; } &&
           rg -q '^RESULT public service storage PASS' "$run/serial-results.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "Timeout/exit: $run" >&2;exit 1;fi
    sleep 1
done
if [[ -n $label_source ]];then
    printf 'info blockstats\n' | socat - "UNIX-CONNECT:$run/monitor" >"$run/label-blockstats.txt"
    rg -q '^ide1-hd1: .*wr_bytes=0 .*wr_operations=0 ' "$run/label-blockstats.txt"
fi
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
if [[ $persistent == 1 ]];then
    if [[ $startup_import == 1 ]];then
        rg -q '^Public startup import record PASS persisted-fixture=1 readonly=1 enabled=1' "$run/serial-results.log"
    fi
    if [[ $layout == mirror && -n $cli_dir ]];then
        rg -q '^Public mirror resilver PASS offline=1 missed-write=1 online=1 finished=1 issued-nonzero=1 errors=0' "$run/serial-results.log"
    fi
    rg -q '^Public backing clean detach PASS' "$run/serial-results.log"
    # Read-only host validation must agree with the guest's clean detach.
    # In particular, do not bypass a dirty FAT bit with MTOOLS_SKIP_CHECK.
    fsck.fat -n -v "$run/backing.raw" >"$run/fat-after-writer.log" 2>&1
    if [[ -n $device ]];then
        rg -q '^Public device PASS geometry=1 async-128KiB=1 reopen=3 exact-read=1 eject=1 busy-refused=1 bounds=1' "$run/serial-results.log"
    fi
    if [[ $zvol_fs == 1 ]];then
        rg -q '^Public zvol filesystem PASS format=1 write=1 flush=1 exact-read=1 directory=1 volume-list=1 busy-refused=1 shutdown=1' "$run/serial-results.log"
    fi
    if [[ $zvol_ffs == 1 ]];then
        rg -q '^Public FFS zvol type PASS dos-type-verified=1' "$run/serial-results.log"
    fi
    if [[ -n $cli_dir ]];then
        rg -q '^Public zvol PASS create=1 geometry=1 write=1 discard-edges=1 flush=1 exact-read=1 bounds=1 stale-descriptor=1' "$run/serial-results.log"
    fi
    if [[ $dspare == 1 ]];then
        rg -q '^Public dRAID spare PASS offline=1 inuse=1 finished=1 errors=0' "$run/serial-results.log"
    fi
    if [[ $sequential == 1 ]];then
        rg -q '^Public sequential rebuild PASS complete=1 rebuilt-nonzero=1 errors=0 scrub-finished=1' "$run/serial-results.log"
    fi
    if [[ $replace == 1 ]];then
        rg -q '^Public mirror replacement PASS new-guid=1 old-absent=1 leaves=2 finished=1 errors=0' "$run/serial-results.log"
        # Keep the retired member as evidence, but make it unavailable to the
        # fresh guest, including to any import-directory discovery fallback.
        mcopy -i "$run/backing.raw" ::OZPublicPool/disk0 "$run/retired-disk0"
        sha256sum "$run/retired-disk0" >"$run/retired-disk0.sha256"
        mdel -i "$run/backing.raw" ::OZPublicPool/disk0
    fi
    if [[ ${AROS_TEST_REQUIRE_BOOTENV:-0} == 1 ]];then
        rg -q '^Public upstream bootenv persistence PASS write=1 readback=1' "$run/serial-results.log"
    fi
    mkdir "$run/members-before" "$run/members-after"
    for ((member=0;member<members;member++));do
        suffix=;if [[ $replace == 1 && $member == 0 ]];then suffix=-new;fi
        mcopy -i "$run/backing.raw" "::OZPublicPool/disk$member$suffix" "$run/members-before/disk$member"
    done
    sha256sum "$run/members-before/"* >"$run/members-before.sha256"
    if [[ -n $cli_dir ]];then
        mkdir "$run/reuse-before" "$run/reuse-after"
        for member in 0 1 2 3 4;do
            mcopy -i "$run/backing.raw" "::OZReuseChanges/disk$member" "$run/reuse-before/disk$member"
        done
        sha256sum "$run/reuse-before/"* >"$run/reuse-before.sha256"
    fi
    if [[ $startup_archive == 1 ]];then
        rg -q '^Public persistent settings PASS write=1' "$run/serial-results.log"
        sha256sum "$run/startup-archive.raw" >"$run/archive-before.sha256"
    fi
    printf '%s\n' 'FailAt 20' 'MakeDir RAM:T RAM:ENV' 'Assign T: RAM:T' \
        'Assign ENV: RAM:ENV' 'Stack 1048576' 'Mount SYS:OZBACK' \
        'C:OpenZFSPublicServiceTest STORAGE-COLD' 'EndCLI' >"$run/Startup-Cold"
    xorriso -indev "$base" -outdev "$run/cold.iso" \
        -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
        "${platform_maps[@]}" \
        -map "$posix" /Libs/posixc.library \
        -map "$run/security-fixture" /Security \
        -map "$run/OpenZFSServiceNative" /C/OpenZFSServiceNative \
        -map "$run/OpenZFSPublicServiceTest" /C/OpenZFSPublicServiceTest \
        "${extra_maps[@]}" \
        -map aros/engine/public-service-test-only /openzfs-service-test-only \
        -map "$run/Startup-Cold" /S/Startup-Sequence \
        -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack-cold.log" 2>&1
    "$qemu" -name "openzfs-public-cold-$$" -machine pc,accel=kvm -cpu host \
        -smp "$cpus" -m 2048 -drive "file=$run/cold.iso,media=cdrom,if=ide,index=2,readonly=on" \
        -drive "file=$run/backing.raw,format=raw,if=ide,index=0" -display none -nic none \
        "${archive_cold_drives[@]}" \
        -boot d -no-reboot -no-shutdown -serial "file:$run/cold-serial.log" \
        -monitor "unix:$run/cold-monitor,server=on,wait=off" >"$run/cold-qemu.log" 2>&1 &
    pid=$!
    trap cleanup EXIT
    deadline=$((SECONDS+duration))
    ffs_cancel_count=0
    ffs_captured=0
    while :;do
        if [[ -f "$run/cold-serial.log" ]];then
            sed -E 's/^[[:xdigit:]]{16} 0x[[:xdigit:]]{16} \| [0-9]+ \| //' "$run/cold-serial.log" >"$run/cold-results.log"
            if rg -q '^RESULT .*FAIL|Public service test line=[1-9][0-9]*|\[PF-DBG\]|^ASSERT at' "$run/cold-results.log";then exit 1;fi
            if [[ $zvol_ffs == 1 ]];then
                expected=$(rg -c '^Public FFS denial requester expected ' "$run/cold-results.log" || true)
                resolved=$(rg -c '^Public FFS denial resolved ' "$run/cold-results.log" || true)
                if (( ${expected:-0} > ${resolved:-0} ));then
                    # Only the isolated fixture's explicitly announced negative
                    # operation may receive input. AFS opens EasyRequestArgs
                    # directly; its Cancel shortcut is Left-Amiga B, not Escape.
                    (( ffs_cancel_count < 45 )) || { echo 'FFS requester cancellation limit reached' >&2; exit 1; }
                    if (( expected > ffs_captured ));then
                        printf 'screendump %s/ffs-denial-%s.ppm\n' "$run" "$expected" |
                            socat - "UNIX-CONNECT:$run/cold-monitor" >>"$run/ffs-requester-input.log"
                        ffs_captured=$expected
                    fi
                    printf 'sendkey meta_l-b\n' | socat - "UNIX-CONNECT:$run/cold-monitor" >>"$run/ffs-requester-input.log"
                    ffs_cancel_count=$((ffs_cancel_count+1))
                fi
            fi
            if rg -q '^RESULT public service storage PASS' "$run/cold-results.log" &&
                { [[ ${AROS_TEST_REQUIRE_BOOTENV:-0} != 1 ]] ||
                  rg -q '^Public upstream bootenv persistence PASS cold=1 marker=1 version=1' "$run/cold-results.log"; } &&
                rg -q "^Public cold degraded assign PASS missing=$missing exact=1 readonly=1 restored=1" "$run/cold-results.log";then break;fi
        fi
        if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "Cold timeout/exit: $run" >&2;exit 1;fi
        sleep 1
    done
    printf 'quit\n' | socat - "UNIX-CONNECT:$run/cold-monitor" >/dev/null
    wait "$pid"
    trap - EXIT
    rg -q '^Public backing clean detach PASS' "$run/cold-results.log"
    rg -q '^Public pool root payload PASS written=1 flushed=1 detached=1' "$run/serial-results.log"
    [[ $(rg -c '^Public cold pool root PASS exact-read=1 readonly-write-denied=1 detached=1' "$run/cold-results.log") -ge 3 ]]
    if [[ $startup_archive == 1 ]];then
        rg -q '^Public persistent settings PASS write=0' "$run/cold-results.log"
        sha256sum -c "$run/archive-before.sha256" >"$run/archive-unchanged.log"
    fi
    if [[ $startup_import == 1 ]];then
        [[ $(rg -c '^Public startup import command PASS fresh-guest=1 guid-bound=1 readonly=1' "$run/cold-results.log") == 3 ]]
        if [[ $startup_archive == 1 ]];then
            [[ $(rg -c '^Public persistent enabled restore PASS trusted-envarc=1 imported=1 mounted=1 readonly=1' "$run/cold-results.log") == 3 ]]
        fi
    fi
    fsck.fat -n -v "$run/backing.raw" >"$run/fat-after-cold.log" 2>&1
    if [[ -n $cli_dir ]];then
        zvol_imports=3
        if [[ $dspare == 1 ]];then zvol_imports=4;fi
        rg -q '^Public pending deferred snapshots PASS recursive=1 parent-held=1 child-held=1 retained-for-cold=1' "$run/serial-results.log"
        [[ $(rg -c '^Public cold deferred snapshots PASS parent=1 child=1 pending=1 holds=1 readonly-release-refused=1 unchanged=1' "$run/cold-results.log") == "$zvol_imports" ]]
        [[ $(rg -c '^Public cold zvol PASS exact-read=1 discard-edges=1 readonly=1 bounds=1 stale-descriptor=1' "$run/cold-results.log") == "$zvol_imports" ]]
        if [[ -n $device ]];then
            [[ $(rg -c '^Public cold device PASS geometry=1 readonly=1 reopen=3 exact-read=1 eject=1 busy-refused=1 bounds=1' "$run/cold-results.log") == "$zvol_imports" ]]
        fi
        if [[ $zvol_fs == 1 ]];then
            [[ $(rg -c '^Public cold zvol filesystem PASS exact-read=1 readonly=1 directory=1 volume-list=1 busy-refused=1 shutdown=1' "$run/cold-results.log") == "$zvol_imports" ]]
        fi
        if [[ $zvol_ffs == 1 ]];then
            [[ $(rg -c '^Public FFS zvol type PASS dos-type-verified=1' "$run/cold-results.log") == "$zvol_imports" ]]
        fi
    fi
    if [[ $layout == mirror && -n $cli_dir ]];then
        [[ $(rg -c '^Public cold resilver payload PASS bytes=1048576 exact=1' "$run/cold-results.log") == 3 ]]
    fi
    if [[ $replace == 1 ]];then
        rg -q '^Public cold replacement PASS replacement-only=1 exact=1' "$run/cold-results.log"
        sha256sum -c "$run/retired-disk0.sha256" >"$run/retired-disk0-unchanged.log"
    fi
    if [[ $dspare == 1 ]];then
        rg -q '^Public cold dRAID spare PASS original-absent=1 exact=1' "$run/cold-results.log"
        rg -q "^Public cold dRAID rebuilt redundancy PASS missing=$((missing+1)) exact=1 restored=1" "$run/cold-results.log"
        [[ $(rg -c '^Public cold resilver payload PASS bytes=1048576 exact=1' "$run/cold-results.log") == 4 ]]
    fi
    if [[ $sequential == 1 ]];then
        [[ $(rg -c '^Public cold sequential status PASS complete=1 rebuilt-nonzero=1 scrub-finished=1' "$run/cold-results.log") == 4 ]]
    fi
    for ((member=0;member<members;member++));do
        suffix=;if [[ $replace == 1 && $member == 0 ]];then suffix=-new;fi
        mcopy -i "$run/backing.raw" "::OZPublicPool/disk$member$suffix" "$run/members-after/disk$member"
        cmp "$run/members-before/disk$member" "$run/members-after/disk$member"
    done
    sha256sum "$run/members-after/"* >"$run/members-after.sha256"
    if [[ -n $cli_dir ]];then
        [[ $(rg -c '^Public changed pool cold PASS phase=[012] exact-read=1 readonly-write-denied=1 exported=1' "$run/cold-results.log") == 3 ]]
        for member in 0 1 2 3 4;do
            mcopy -i "$run/backing.raw" "::OZReuseChanges/disk$member" "$run/reuse-after/disk$member"
            cmp "$run/reuse-before/disk$member" "$run/reuse-after/disk$member"
        done
        sha256sum "$run/reuse-after/"* >"$run/reuse-after.sha256"
        "$host_zdb" -e -p "$run/reuse-after" -bcc public_reuse_target >"$run/reuse-zdb-check.log" 2>&1
        rg -q 'No leaks' "$run/reuse-zdb-check.log"
        sha256sum -c "$run/reuse-after.sha256" >"$run/reuse-zdb-unchanged.log"
    fi
    "$host_zdb" -e -p "$run/members-after" -bcc public_mirror >"$run/zdb-check.log" 2>&1
    rg -q 'No leaks' "$run/zdb-check.log"
    sha256sum -c "$run/members-after.sha256" >"$run/zdb-members-unchanged.log"
    echo "PASS: fresh-boot healthy/degraded import, exact DOS data, unchanged pool members. Evidence: $run"
    rg -q '^Public DOS links PASS write=0 hard=1 relative-soft=1 unlinked-source=1 dangling-refused=1' "$run/cold-results.log"
    if [[ $cold_release == 1 ]];then
        # A separate boot AFTER immutable read-only evidence is complete.
        # Never replace the preceding member comparisons with writable ones.
        printf '1\n' >"$run/cold-release.txt"
        sed 's/STORAGE-COLD/STORAGE-COLD-RELEASE/' "$run/Startup-Cold" >"$run/Startup-Release"
        # Start from the supplied base, not a generated ISO whose retained
        # hybrid partition metadata can refer beyond its new image size.
        xorriso -indev "$base" -outdev "$run/release.iso" \
            -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
            "${platform_maps[@]}" \
            -map "$posix" /Libs/posixc.library \
            -map "$run/security-fixture" /Security \
            -map "$run/OpenZFSServiceNative" /C/OpenZFSServiceNative \
            -map "$run/OpenZFSPublicServiceTest" /C/OpenZFSPublicServiceTest \
            "${extra_maps[@]}" \
            -map aros/engine/public-service-test-only /openzfs-service-test-only \
            -map "$run/Startup-Release" /S/Startup-Sequence \
            -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
            -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack-release.log" 2>&1
        "$qemu" -name "openzfs-public-release-$$" -machine pc,accel=kvm -cpu host \
            -smp "$cpus" -m 2048 -drive "file=$run/release.iso,media=cdrom,if=ide,index=2,readonly=on" \
            -drive "file=$run/backing.raw,format=raw,if=ide,index=0" -display none -nic none \
            "${archive_cold_drives[@]}" \
            -boot d -no-reboot -no-shutdown -serial "file:$run/release-serial.log" \
            -monitor "unix:$run/release-monitor,server=on,wait=off" >"$run/release-qemu.log" 2>&1 &
        pid=$!
        trap cleanup EXIT
        deadline=$((SECONDS+duration))
        while :;do
            if [[ -f "$run/release-serial.log" ]];then
                sed -E 's/^[[:xdigit:]]{16} 0x[[:xdigit:]]{16} \| [0-9]+ \| //' "$run/release-serial.log" >"$run/release-results.log"
                if rg -q '^RESULT .*FAIL|Public service test line=[1-9][0-9]*|\[PF-DBG\]|^ASSERT at' "$run/release-results.log";then exit 1;fi
                if rg -q '^RESULT public service storage PASS' "$run/release-results.log";then break;fi
            fi
            if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "Release timeout/exit: $run" >&2;exit 1;fi
            sleep 1
        done
        printf 'quit\n' | socat - "UNIX-CONNECT:$run/release-monitor" >/dev/null
        wait "$pid"
        trap - EXIT
        rg -q '^Public cold writable release PASS pending-before=1 recursive-release=1 parent-absent=1 child-absent=1 reimport-absent=1 exact-data=1 exported=1' "$run/release-results.log"
        rg -q '^Public backing clean detach PASS' "$run/release-results.log"
        fsck.fat -n -v "$run/backing.raw" >"$run/fat-after-release.log" 2>&1
        if [[ $startup_archive == 1 ]];then
            sha256sum -c "$run/archive-before.sha256" >"$run/archive-after-release.log"
        fi
        mkdir "$run/members-release"
        for ((member=0;member<members;member++));do
            suffix=;if [[ $replace == 1 && $member == 0 ]];then suffix=-new;fi
            mcopy -i "$run/backing.raw" "::OZPublicPool/disk$member$suffix" "$run/members-release/disk$member"
        done
        sha256sum "$run/members-release/"* >"$run/members-release.sha256"
        "$host_zdb" -e -p "$run/members-release" -bcc public_mirror >"$run/release-zdb-check.log" 2>&1
        rg -q 'No leaks' "$run/release-zdb-check.log"
        sha256sum -c "$run/members-release.sha256" >"$run/release-zdb-unchanged.log"
        echo "PASS: writable cold release, persisted deletion and independent zdb. Evidence: $run"
    fi
fi
if [[ $partitions == 1 ]];then
    if [[ $native_cold == 1 || $all_native == 1 ]];then
        # The writer intentionally changes partition.raw; other disks must not.
        if [[ $all_native == 1 ]];then
            grep -vE '/(partition|changes).raw$' "$run/disks-before.sha256" > "$run/disks-protected.sha256"
        else
            grep -v '/partition.raw$' "$run/disks-before.sha256" > "$run/disks-protected.sha256"
        fi
        sha256sum -c "$run/disks-protected.sha256" > "$run/disks-after-check.txt"
    else
        sha256sum -c "$run/disks-before.sha256" > "$run/disks-after-check.txt"
    fi
fi
if [[ $native_cold == 1 || $all_native == 1 ]];then
    native_reader='Public native cold reader PASS locked=1 mount-denied=1 key-loaded=1 exact-read=1 readonly-write-denied=1 exported=1'
    native_writer='Public native cold writer PASS encrypted-root=1 exact-write=1 detached=1 exported=1'
    native_cold_drives=()
    if [[ $all_native == 1 ]];then
        native_reader='Public all-native changed reader PASS online=1 exact-read=1 readonly-write-denied=1 exported=1'
        native_writer='Public all-native changed writer PASS attach=1 replace=1 add=1 native-leaves=4 exact-read=1 exported=1'
        native_cold_drives=(-drive "file=$run/changes.raw,format=raw,if=ide,index=3")
        if [[ $native_changes == 6 ]];then
            native_cold_drives=(-device ich9-ahci,id=ozchanges
                -drive "file=$run/changes.raw,format=raw,if=none,id=changes"
                -device ide-hd,drive=changes,bus=ozchanges.0)
        fi
    fi
    rg -q "^$native_writer" "$run/serial-results.log"
    sha256sum "$run/partition.raw" > "$run/native-before-cold.sha256"
    if [[ $all_native == 1 ]];then sha256sum "$run/changes.raw" >> "$run/native-before-cold.sha256";fi
    printf '%s\n' 'FailAt 20' 'MakeDir RAM:T RAM:ENV' 'Assign T: RAM:T' \
        'Assign ENV: RAM:ENV' 'Stack 1048576' \
        'C:OpenZFSPublicServiceTest NATIVE-COLD' 'EndCLI' > "$run/Startup-Native-Cold"
    xorriso -indev "$base" -outdev "$run/native-cold.iso" \
        -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
        "${platform_maps[@]}" \
        -map "$posix" /Libs/posixc.library \
        -map "$run/security-fixture" /Security \
        -map "$run/OpenZFSServiceNative" /C/OpenZFSServiceNative \
        -map "$run/OpenZFSPublicServiceTest" /C/OpenZFSPublicServiceTest \
        "${extra_maps[@]}" \
        -map aros/engine/public-service-test-only /openzfs-service-test-only \
        -map "$run/Startup-Native-Cold" /S/Startup-Sequence \
        -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$run/pack-native-cold.log" 2>&1
    native_phases=(native-cold)
    if [[ $all_native == 1 ]];then
        native_phases+=(native-degraded native-restored)
        printf '1\n' > "$run/native-recovery.txt"
        # Start from the original image, not a remaster with stale hybrid GPT.
        xorriso -indev "$base" -outdev "$run/native-degraded.iso" \
            -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
            "${platform_maps[@]}" \
            -map "$posix" /Libs/posixc.library \
            -map "$run/security-fixture" /Security \
            -map "$run/OpenZFSServiceNative" /C/OpenZFSServiceNative \
            -map "$run/OpenZFSPublicServiceTest" /C/OpenZFSPublicServiceTest \
            "${extra_maps[@]}" \
            -map aros/engine/public-service-test-only /openzfs-service-test-only \
            -map "$run/Startup-Native-Cold" /S/Startup-Sequence \
            -map aros/engine/public-service-test-only /openzfs-native-missing-ata \
            -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
            -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$run/pack-native-degraded.log" 2>&1
    fi
    if [[ $native_healing == 1 ]];then
        printf '1\n' > "$run/native-healing.txt"
        native_phases+=(native-write native-heal native-verify)
        for mode in native-write native-heal native-verify;do
            mode_maps=(-map aros/engine/public-service-test-only "/openzfs-$mode")
            if [[ $mode == native-write ]];then
                mode_maps+=(-map aros/engine/public-service-test-only /openzfs-native-missing-ata)
            fi
            xorriso -indev "$base" -outdev "$run/$mode.iso" \
                -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
                "${platform_maps[@]}" -map "$posix" /Libs/posixc.library \
                -map "$run/security-fixture" /Security \
                -map "$run/OpenZFSServiceNative" /C/OpenZFSServiceNative \
                -map "$run/OpenZFSPublicServiceTest" /C/OpenZFSPublicServiceTest \
                "${extra_maps[@]}" "${mode_maps[@]}" \
                -map aros/engine/public-service-test-only /openzfs-service-test-only \
                -map "$run/Startup-Native-Cold" /S/Startup-Sequence \
                -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
                -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$run/pack-$mode.log" 2>&1
        done
    fi
    for native_phase in "${native_phases[@]}";do
      native_iso="$run/native-cold.iso"
      native_first=(-drive "file=$run/partition.raw,format=raw,if=ide,index=0")
      expected_reader=$native_reader
      if [[ $native_phase == native-degraded ]];then
          native_iso="$run/native-degraded.iso";native_first=()
          expected_reader=${native_reader/online=1/degraded=1}
      fi
      case "$native_phase" in
        native-write)
          native_iso="$run/$native_phase.iso";native_first=()
          expected_reader='Public native degraded write PASS bytes=1048576 synced=1 exported=1' ;;
        native-heal)
          native_iso="$run/$native_phase.iso"
          expected_reader='Public native restored healing PASS issued-nonzero=1 errors=0 exact-data=1 exported=1' ;;
        native-verify)
          native_iso="$run/$native_phase.iso"
          expected_reader='Public native healed cold PASS exact-data=1 readonly-write-denied=1 exported=1' ;;
      esac
      "$qemu" -name "openzfs-$native_phase-$$" -machine pc,accel=kvm -cpu host \
        -smp "$cpus" -m 2048 -drive "file=$native_iso,media=cdrom,if=ide,index=2,readonly=on" \
        "${native_first[@]}" \
        "${native_cold_drives[@]}" \
        -display none -nic none -boot d -no-reboot -no-shutdown \
        -serial "file:$run/$native_phase-serial.log" \
        -monitor "unix:$run/$native_phase-monitor,server=on,wait=off" > "$run/$native_phase-qemu.log" 2>&1 &
    pid=$!
    trap cleanup EXIT
    deadline=$((SECONDS+duration))
    while :;do
        if [[ -f "$run/$native_phase-serial.log" ]];then
            sed -E 's/^[[:xdigit:]]{16} 0x[[:xdigit:]]{16} \| [0-9]+ \| //' "$run/$native_phase-serial.log" > "$run/$native_phase-results.log"
            if rg -q '^RESULT .*FAIL|Public service test line=[1-9][0-9]*|\[PF-DBG\]|^ASSERT at' "$run/$native_phase-results.log";then exit 1;fi
            if rg -q '^RESULT public service storage PASS' "$run/$native_phase-results.log" &&
               rg -q "^$expected_reader" "$run/$native_phase-results.log";then break;fi
        fi
        if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "Native cold timeout/exit: $run" >&2;exit 1;fi
        sleep 1
    done
    printf 'quit\n' | socat - "UNIX-CONNECT:$run/$native_phase-monitor" >/dev/null
    wait "$pid"
    trap - EXIT
    case "$native_phase" in
      native-write)
        # Only the absent ATA disk must retain its old bytes after writes.
        grep '/partition.raw$' "$run/native-before-cold.sha256" > "$run/native-absent-before.sha256"
        sha256sum -c "$run/native-absent-before.sha256" > "$run/native-write-absent-unchanged.log" ;;
      native-heal)
        sha256sum "$run/partition.raw" "$run/changes.raw" > "$run/native-healed.sha256" ;;
      native-verify)
        sha256sum -c "$run/native-healed.sha256" > "$run/native-verify-unchanged.log" ;;
      *) sha256sum -c "$run/native-before-cold.sha256" > "$run/$native_phase-unchanged.log" ;;
    esac
    done
    echo "PASS: native cold suite (healing=$native_healing), exact reads and unchanged read-only phases. Evidence: $run"
fi
if [[ $native_discovery == 1 ]];then
  if [[ $native_changes != 0 && $all_native == 0 ]];then
    lane=CLI; [[ $native_changes == 2 || $native_changes == 4 ]] && lane=Manager
    candidates=attach-replace; [[ $native_changes == 3 || $native_changes == 4 ]] && candidates=add
    rg -q "^Public native changes PASS lane=$lane candidates=$candidates exact-read=5 cleanup=1" "$run/serial-results.log"
  fi
  if [[ $native_cold == 1 ]];then
    rg -q '^Public native cold retained PASS properties=6 locked-import=2 wrong-key-denied=2 key-reload=2 exact-data=1 neighbor-cleanup=1' "$run/serial-results.log"
  else
    rg -q '^Public native discovery PASS create=1 default-scan=1 readonly-import=1 rw-import=1 destroy=1' "$run/serial-results.log"
    rg -q '^Public native destroyed reuse PASS same-extent=1 no-force=1 old-payload-absent=1 new-write=1 destroyed=1' "$run/serial-results.log"
    rg -q '^Public native corrupt reuse PASS labels=4 create-denied=4 full-extent-unchanged=4 restored=4' "$run/serial-results.log"
    rg -q '^Public native conflicting reuse PASS labels=4 create-denied=4 full-extent-unchanged=4 restored=4' "$run/serial-results.log"
    rg -q '^Public native conflicting label offsets PASS same-position=4' "$run/serial-results.log"
    rg -q '^Public native conflicting restore PASS recreate=1 exact-data=1 destroyed=1' "$run/serial-results.log"
    rg -q '^Public native discovery coexistence PASS separate-partitions=1 neighbor-online=1 neighbor-property=1 cleanup=1' "$run/serial-results.log"
    rg -q '^Public native discovery data PASS handler-write=1 readonly-import-read=1 readonly-write-denied=1 writable-import-read=1 cleanup=1' "$run/serial-results.log"
    rg -q '^Public native encrypted root PASS properties=6 locked-import=2 mount-denied=2 key-reload=2 exact-data=1 cleanup=1' "$run/serial-results.log"
    rg -q '^Public native encrypted root wrong key PASS denied=2 remains-locked=2 mount-denied=2 correct-recovery=2' "$run/serial-results.log"
  fi
    rg -q '^Public native reuse guards PASS active-force-denied=1 exported-force-denied=1 original-data-preserved=1' "$run/serial-results.log"
fi
if [[ $companions == 1 ]];then
    rg -q '^Public exec helper PASS real-zpool=1 captured-output=1 missing-executable=1' "$run/serial-results.log"
fi
if [[ -n $label_source ]];then sha256sum -c "$run/label-original.sha256" >"$run/label-original-after-check.txt";fi
if [[ -n $cli_dir ]];then
    rg -q '^Public key change authorization PASS no-grant-denied=1 load-only-denied=1 private-file-denied=1 explicit-grant=1 revoke-denied=1 properties-preserved=1 root-restored=1' "$run/serial-results.log"
    rg -q '^Public file destroyed reuse PASS cli=1 manager-api=1 old-payload-absent=2 exact-read=3 exported-refused=2 cleanup=1' "$run/serial-results.log"
    rg -q '^Public file label denial PASS corrupt=4 conflicting-member=4 checksum=4 manager-denied=12 forced-cli-denied=12 whole-members-unchanged=12 restored=12' "$run/serial-results.log"
    rg -q '^Public file conflicting label offsets PASS same-position=4' "$run/serial-results.log"
    rg -q '^Public destroyed changes CLI PASS attach=1 replace=1 add=1 active-force-refused=3 exported-force-refused=3 exact-read=5 cleanup=1' "$run/serial-results.log"
    reuse_finish=cleanup
    if [[ $persistent == 1 ]];then reuse_finish=retained;fi
    rg -q "^Public destroyed changes Manager PASS attach=1 replace=1 add=1 active-refused=3 exported-refused=3 exact-read=5 $reuse_finish=1" "$run/serial-results.log"
fi
rg -q '^Public DOS links PASS write=1 hard=1 relative-soft=1 unlinked-source=1 dangling-refused=1' "$run/serial-results.log"
if [[ $gui_volume == 1 ]];then
    rg -q '^Public GUI volume PASS actual-mapping=1 readonly=1 exact-data=1 saved-mountfile=1 eject=1' "$run/serial-results.log"
fi
if [[ $gui_key_change == 1 ]];then
    rg -q '^Public GUI key change PASS format=passphrase iterations=400000 location=1 old-key-denied=1 new-key-load=1 cleanup=1' "$run/serial-results.log"
elif [[ $gui_key_check == 1 ]];then
    rg -q '^Public GUI key check PASS locked=1 mount-refused=1 normal-load=1 cleanup=1' "$run/serial-results.log"
fi
if [[ $gui_destroy_preview == 1 ]];then
    rg -q '^Public GUI destruction preview PASS parent-unchanged=1 child-unchanged=1 hold-retained=1 release-does-not-destroy=1 cleanup=1' "$run/serial-results.log"
elif [[ $gui_defer == 1 ]];then
    rg -q '^Public GUI deferred PASS parent-removed=1 child-deferred=1 hold-retained=1 release-removes=1 cleanup=1' "$run/serial-results.log"
fi
if [[ $gui_clone == 1 ]];then
    rg -q '^Public GUI clone PASS quota=1 compression=1 no-mount=1 origin=1 cleanup=1' "$run/serial-results.log"
fi
if [[ $gui_desktop == 1 ]];then
    rg -q '^Public desktop post-UI data PASS exact-read=1' "$run/serial-results.log"
fi
if [[ $keep_images == 0 ]];then
    prune_root=()
    if [[ $artifact_root != "$repo/build-aros" ]];then prune_root=(--artifact-root "$artifact_root");fi
    python3 aros/distribution/prune-public-test-images.py --apply "${prune_root[@]}" --run "$(basename "$run")" \
        >"$run/image-pruning.log" 2>&1
fi
echo "PASS: public service lifecycles and $layout DOS round trip (persistent=$persistent). Evidence: $run"
