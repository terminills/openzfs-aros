#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
baseline=$(realpath -- "${1:?verified mirror evidence directory required}")
[[ $baseline == "$repo/build-aros/qemu-run."* ]]
rg -q '^RESULT QEMU native two-device mirror PASS' "$baseline/create-serial.log"
sha256sum -c "$baseline/disks-before.sha256"
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private security/ATA overlay required}
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
service=${AROS_TEST_SERVICE_BINARY:-build-aros/OpenZFSAdmissionServiceHarness}
manager=${AROS_TEST_MANAGER_BINARY:-build-aros/OpenZFSManagerNative}
scenario=${AROS_UI_SCENARIO:-properties}
startup=aros/ui/Startup-Native-UI
case "$scenario" in
    properties) ;;
    promotion) startup=aros/ui/Startup-Native-Promote ;;
    bookmark) startup=aros/ui/Startup-Native-Bookmark ;;
    space) startup=aros/ui/Startup-Native-Space ;;
    compression) startup=aros/ui/Startup-Native-Compression ;;
    recordsize) startup=aros/ui/Startup-Native-Recordsize ;;
    wanderer) startup=aros/ui/Startup-Native-Wanderer ;;
    export) startup=aros/ui/Startup-Native-Export ;;
    receive) startup=aros/ui/Startup-Native-Receive ;;
    send) startup=aros/ui/Startup-Native-Send ;;
    ownsend) startup=aros/ui/Startup-Native-OwnSend ;;
    resumesend) startup=aros/ui/Startup-Native-ResumeSend ;;
    abortreceive) startup=aros/ui/Startup-Native-AbortReceive ;;
    encryption) startup=aros/ui/Startup-Native-Encryption ;;
    passphrase) startup=aros/ui/Startup-Native-Passphrase ;;
    initialize-readonly) startup=aros/ui/Startup-Native-Initialize-Readonly ;;
    create) startup=aros/ui/Startup-Native-Create ;;
    create-partition) startup=aros/ui/Startup-Native-Create-Partition ;;
    create-partition-cold) startup=aros/ui/Startup-Native-Create-Partition ;;
    editor) startup=aros/ui/Startup-Native-Editor ;;
    *) echo 'Unknown UI scenario' >&2;exit 2 ;;
esac
public_create=${AROS_UI_PUBLIC_CREATE:-0}
[[ $public_create == 0 || $public_create == 1 ]]
if [[ $public_create == 1 ]];then
    [[ $scenario == create-partition || $scenario == create-partition-cold ]]
    rg -Fxq 'service_test_modes=0' "$(dirname "$service")/service-build-options.txt"
    rg -Fxq 'full_dispatcher=1' "$(dirname "$service")/service-build-options.txt"
    startup=aros/ui/Startup-Native-Create-Public
fi
disk_source=$baseline
if [[ $scenario == initialize-readonly ]];then
    disk_source=$(realpath -- "${AROS_UI_INITIALIZE_SOURCE:?verified suspended initialization run required}")
    [[ $(dirname -- "$disk_source") == "$repo/build-aros" && $(basename -- "$disk_source") == admission-service-run.* ]]
    rg -a -q '^Native initialization cold suspended state PASS' "$disk_source/verify-serial.log"
    rg -a -q '^RESULT standalone admission PASS' "$disk_source/verify-serial.log"
    if fuser "$disk_source/disk0.raw" "$disk_source/disk1.raw" >/dev/null 2>&1;then
        echo 'Initialization source disks are still in use' >&2;exit 1
    fi
    sha256sum -c "$disk_source/readonly-before.sha256"
fi
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_TEST_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/native-gui-run.XXXXXXXX")
cp "$0" "$run/native-ui-qemu.sh"
printf '%s\n' "$scenario" >"$run/scenario.txt"
printf '%s\n' "$public_create" >"$run/public-create.txt"
echo "Evidence: $run"
iso_dir=$run
if [[ -n ${AROS_TEST_ISO_TMPDIR:-} ]];then
    echo 'AROS_TEST_ISO_TMPDIR is retired; ISO evidence stays in the private repo run directory' >&2;exit 2
fi
printf '%s\n' "$iso_dir" >"$run/iso-location.txt"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
tar -czf "$run/aros-source.tar.gz" aros/engine aros/ui aros/zpl
cp "$disk_source/export.nvlist" "$run/export.nvlist"
for disk in 0 1;do cp --reflink=auto --sparse=always "$disk_source/disk$disk.raw" "$run/disk$disk.raw";done
sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/disks-before.sha256"
cp -- "$service" "$run/OpenZFSAdmissionServiceHarness"
cp -- "$manager" "$run/OpenZFSManagerNative"
service="$run/OpenZFSAdmissionServiceHarness"
manager="$run/OpenZFSManagerNative"
sha256sum "$base" "$service" "$manager" \
    build-aros/OpenZFSAdmissionGuiLauncher "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" >"$run/inputs.sha256"
extra_maps=()
if [[ $scenario == create || $scenario == create-partition || $scenario == create-partition-cold ]];then
    fixture=${AROS_TEST_CREATE_BINARY:-build-aros/OpenZFSPoolCreateGui}
    cp -- "$fixture" "$run/OpenZFSPoolCreateGui"
    if [[ $public_create == 1 ]];then
        strings "$run/OpenZFSPoolCreateGui" | rg '^Public GUI create service bootstrap$' >/dev/null
    fi
    extra_maps=(-map "$run/OpenZFSPoolCreateGui" /C/OpenZFSPoolCreateGui)
    sha256sum "$run/OpenZFSPoolCreateGui" >>"$run/inputs.sha256"
fi
extra_drives=()
if [[ $scenario == create-partition || $scenario == create-partition-cold ]];then
    partition=${AROS_TEST_PARTITION_LIBRARY:?native partition library required}
    cp -- "$partition" "$run/partition.library"
    extra_maps+=(-map "$run/partition.library" /Libs/partition.library)
    sha256sum "$run/partition.library" >>"$run/inputs.sha256"
    if [[ $scenario == create-partition-cold ]];then
        source_result=$(realpath -- "${AROS_UI_CREATE_RESULT:?verified offline GUI pool result required}")
        [[ $source_result == "$repo/build-aros/native-gui-run."*/offline.*/result.raw ]]
        source_dir=$(dirname -- "$source_result")
        writer=$(dirname -- "$source_dir")
        [[ $(<"$writer/scenario.txt") == create-partition ]]
        rg -a -q '^RESULT GUI native partition pool creation PASS' "$writer/serial.log"
        rg -q 'No leaks' "$source_dir/zdb.log"
        sha256sum -c "$source_dir/results.sha256"
        if fuser "$source_result" >/dev/null 2>&1;then echo 'Source result is busy' >&2;exit 1;fi
        sha256sum "$source_result" >"$run/cold-source.sha256"
        cp --reflink=auto --sparse=always "$source_result" "$run/create-partition.raw"
    else
    qemu-img create -f raw "$run/create-partition.raw" 256M
    sgdisk --clear --new=1:2048:0 --typecode=1:BF01 "$run/create-partition.raw" >"$run/partition-create.log" 2>&1
    sgdisk --verify --print "$run/create-partition.raw" >"$run/partition-verify.log" 2>&1
    rg -q 'No problems found' "$run/partition-verify.log"
    fi
    sha256sum "$run/create-partition.raw" >"$run/partition-before.sha256"
    qemu-img create -f qcow2 -F raw -b "$run/create-partition.raw" "$run/create-partition.qcow2"
    if [[ $scenario == create-partition-cold ]];then
        sha256sum "$run/create-partition.qcow2" >"$run/cold-overlay.sha256"
    fi
    extra_drives=(-drive "file=$run/create-partition.qcow2,format=qcow2,if=ide,index=3,aio=threads")
fi
if [[ $scenario == wanderer ]];then
    fixture=${AROS_TEST_WANDERER_BINARY:-build-aros/OpenZFSWandererFixture}
    cp -- "$fixture" "$run/OpenZFSWandererFixture"
    extra_maps=(-map "$run/OpenZFSWandererFixture" /C/OpenZFSWandererFixture)
    sha256sum "$run/OpenZFSWandererFixture" >>"$run/inputs.sha256"
fi
if [[ -n ${AROS_TEST_POSIX_LIBRARY:-} ]];then
    cp -- "$AROS_TEST_POSIX_LIBRARY" "$run/posixc.library"
    extra_maps+=(-map "$run/posixc.library" /Libs/posixc.library)
    sha256sum "$run/posixc.library" >>"$run/inputs.sha256"
fi
xorriso -indev "$base" -outdev "$iso_dir/gui.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map build-aros/hosted/Libs/z1.library /Libs/z1.library \
    -map aros/engine/security-fixture /Security \
    -map "$service" /C/OpenZFSAdmissionServiceHarness \
    -map "$manager" /C/OpenZFSManagerNative \
    -map build-aros/OpenZFSAdmissionGuiLauncher /C/OpenZFSAdmissionGuiLauncher \
    "${extra_maps[@]}" \
    -map "$run/export.nvlist" /export.nvlist -map "$startup" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
sha256sum "$iso_dir/gui.iso" >"$run/gui-iso.sha256"
qemu=${AROS_TEST_QEMU:-qemu-system-x86_64}
sha256sum "$(command -v "$qemu")" >>"$run/inputs.sha256"
"$qemu" -name "openzfs-native-gui-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$iso_dir/gui.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -drive "file=$run/disk0.raw,format=raw,if=ide,index=0,aio=threads" \
    -drive "file=$run/disk1.raw,format=raw,if=ide,index=1,aio=threads" \
    "${extra_drives[@]}" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown -usb -device usb-tablet \
    -serial "file:$run/serial.log" -qmp "unix:$run/qmp.sock,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!;printf '%s\n' "$pid" >"$run/qemu.pid"
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
wait "$pid"
