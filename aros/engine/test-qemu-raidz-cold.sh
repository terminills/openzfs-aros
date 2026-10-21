#!/usr/bin/env bash
# Cold read-only import of an accepted private RAIDZ fixture; no host devices.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_TEST_ARTIFACT_ROOT:-}")
fixture=$(realpath -e "${1:?accepted RAIDZ fixture required}")
[[ $(dirname -- "$fixture") == "$root" && $fixture == "$root/native-raidz-run."* ]]
rg -q '^PASS: native .* fixture and DOS tests:' "$fixture/runner.log"
sha256sum -c "$fixture/created.sha256"
parity=$(<"$fixture/raidz-parity.txt")
[[ $parity == 1 || $parity == 2 || $parity == 3 ]]
units=(0 1 3); if [[ $parity == 3 ]];then units+=(4);fi
missing=${AROS_TEST_RAIDZ_MISSING:-none}
[[ $missing == none || $missing =~ ^[0-3]$ ]] || exit 2
mask=${AROS_TEST_RAIDZ_MISSING_MASK:-0}
[[ $mask =~ ^([0-9]|1[0-5])$ ]] || exit 2
extra=
if [[ $missing != none ]];then
    [[ $mask == 0 ]] || exit 2
    ((missing<${#units[@]})) || exit 2
    mask=$((1<<missing))
fi
((mask<(1<<${#units[@]}))) || exit 2
absent=0
for member in "${!units[@]}";do absent=$((absent+((mask>>member)&1)));done
((absent<=parity)) || exit 2
if ((mask));then extra=" MASK-$mask";fi
binary=${AROS_TEST_IMPORT_BINARY:?cold verifier required}
qemu=${AROS_TEST_QEMU:-qemu-system-x86_64}
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?}; ata=${AROS_TEST_ATA_OVERLAY:?}
posix=${AROS_TEST_POSIX_LIBRARY:?}; fs=${AROS_TEST_FS_PACKAGE:?}; partition=${AROS_TEST_PARTITION_LIBRARY:?}
strings "$binary" | rg -F 'DIAGNOSTIC cold ZPL recovery payload=' >/dev/null
run=$(mktemp -d "$root/raidz-cold-run.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
cp "$binary" "$run/OpenZFSQEMUImportTest"
cp "$0" "$run/"
sed "s/OpenZFSQEMUImportTest READONLY /OpenZFSQEMUImportTest READONLY-RAIDZ$parity$extra /" aros/engine/Startup-QEMU-Import-Test >"$run/Startup-Sequence"
printf '%s\n' "$missing" >"$run/missing-member.txt"
printf '%s\n' "$mask" >"$run/missing-mask.txt"
git rev-parse HEAD >"$run/source-head.txt"
sha256sum "$binary" "$(command -v "$qemu")" "$base" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" "$posix" "$fs" "$partition" >"$run/inputs.sha256"
awk '/^EXPORT_XDR / {gsub(/\r/, "", $2); print $2}' "$fixture/serial-completion.log" | xxd -r -p >"$run/export.nvlist"
expected=$(awk '/^EXPORT_XDR_BEGIN / {gsub(/\r/, "", $2); print $2}' "$fixture/serial-completion.log")
[[ $expected =~ ^[1-9][0-9]*$ && $(stat -c %s "$run/export.nvlist") == "$expected" ]]
drives=()
for member in "${!units[@]}";do
    unit=${units[member]}
    # IDE rejects a read-only disk frontend. COW isolates the immutable
    # fixture; blockstats below must still prove zero guest writes.
    if ((mask&(1<<member)));then
        # Preserve ATA master/slave enumeration with a blank nonmember.
        qemu-img create -f qcow2 "$run/read$unit.qcow2" 512M
    else
        qemu-img create -f qcow2 -F raw -b "$fixture/disk$unit.raw" "$run/read$unit.qcow2"
    fi
    if [[ $unit == 4 ]];then
        drives+=(-device ich9-ahci,id=ozahci -drive "file=$run/read$unit.qcow2,format=qcow2,if=none,id=ozfour,aio=threads" -device ide-hd,drive=ozfour,bus=ozahci.0)
    else
        drives+=(-drive "file=$run/read$unit.qcow2,format=qcow2,if=ide,index=$unit,aio=threads")
    fi
done
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$fs" /boot/aros-fs.pkg.xz -map "$partition" /Libs/partition.library \
    -map "$posix" /Libs/posixc.library -map build-aros/hosted/Libs/z1.library /Libs/z1.library \
    -map aros/engine/security-fixture /Security \
    -map "$run/OpenZFSQEMUImportTest" /C/OpenZFSQEMUImportTest \
    -map "$run/export.nvlist" /export.nvlist \
    -map "$run/Startup-Sequence" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
"$qemu" -name "openzfs-raidz-cold-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" "${drives[@]}" \
    -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
trap 'if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi' EXIT
deadline=$((SECONDS+600))
while :;do
    if [[ -f $run/serial.log ]];then
        perl -0777 -pe 's/[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| \[security.library\] secTaskNotify: [^\r\n]*\r?\n//g' "$run/serial.log" >"$run/checked.log"
        if rg -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/checked.log";then exit 1;fi
        if rg -q '^RESULT cold-boot QEMU (two-drive pool|degraded mirror) through AROS handler PASS' "$run/checked.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then exit 1;fi
    sleep 1
done
rg -q '^DIAGNOSTIC cold TXG flush payload=1 .* error=0' "$run/checked.log"
rg -q '^DIAGNOSTIC cold ZPL recovery payload=1 size=8 error=0' "$run/checked.log"
if ((mask));then
    for member in "${!units[@]}";do
        if ((mask&(1<<member)));then
            rg -q "^cold RAIDZ member=$member unavailable-binding=0" "$run/checked.log"
        fi
    done
    rg -q '^cold pool health=6 expected=6' "$run/checked.log"
else
    rg -q '^cold pool health=7 expected=7' "$run/checked.log"
fi
printf 'info blockstats\nquit\n' | timeout 5 socat - "UNIX-CONNECT:$run/monitor" >"$run/monitor.txt"
wait "$pid";trap - EXIT
[[ $(rg -c '^(ide[01]-hd[01]|ozfour): .*wr_bytes=0 .*wr_operations=0 ' "$run/monitor.txt") == ${#units[@]} ]]
sha256sum -c "$fixture/created.sha256"
echo "PASS: native RAIDZ$parity cold read-only missing-mask=$mask handler and recovered TXG/ZPL payloads: $run"
