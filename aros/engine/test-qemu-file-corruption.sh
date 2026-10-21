#!/usr/bin/env bash
# Fixed, verified ZPL fixture; only new external image copies are modified.
set -euo pipefail
if [[ ${OZ_FILE_CORRUPTION_FROZEN:-0} != 1 ]];then
    export OZ_FILE_CORRUPTION_FROZEN=1
    exec bash -c "$(< "$0")" "$0" "$@"
fi
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
root=/mnt/openzfs-artifacts/build-aros
baseline="$root/qemu-run.fpp1hYtd"
gui=${AROS_TEST_CORRUPTION_GUI:-0}
[[ $gui == 0 || $gui == 1 ]]
base="$root/native-import-run.qBkVic8B/test.iso"
source aros/engine/storage-preflight.sh
oz_storage_preflight "$root"
sha256sum -c "$baseline/disks-before.sha256"
run=$(mktemp -d "$root/file-corruption-run.XXXXXXXX")
echo "Evidence: $run"
client="$root/file-corruption-20261004/OpenZFSNativeImportTest"
if [[ $gui == 1 ]];then client="${client}GUI";fi
cp "$client" "$run/OpenZFSNativeImportTest"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
sha256sum "$base" "$run/OpenZFSNativeImportTest" >"$run/inputs.sha256"
maps=()
if [[ $gui == 1 ]];then
    cp "$root/error-log-20261004/OpenZFSManagerNativeFixed" "$run/OpenZFSManagerNative"
    sha256sum "$run/OpenZFSManagerNative" >>"$run/inputs.sha256"
    maps+=(-map "$run/OpenZFSManagerNative" /C/OpenZFSManagerNative)
fi
if [[ -n ${AROS_TEST_CORRUPTION_SHARED:-} ]];then
    shared=$(realpath -e "$AROS_TEST_CORRUPTION_SHARED")
    [[ $shared == "$root"/shared-engine.* ]]
    cp "$shared/OpenZFSServiceNative" "$shared/openzfs.library" "$run/"
    sha256sum "$run/OpenZFSServiceNative" "$run/openzfs.library" >>"$run/inputs.sha256"
    maps+=(-map "$run/OpenZFSServiceNative" /C/OpenZFSServiceNative
          -map "$run/openzfs.library" /Libs/openzfs.library)
fi
for member in 0 1;do
    python3 aros/engine/corrupt-test-member.py "$baseline/disk$member.raw" "$run/disk$member.raw" \
        $((4097*512+0xbdb000)) 23b4dcd46f9704fc2e0376d331f7566548bfa96c521a7acc144904fe53df00ba >"$run/corruption$member.json"
done
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map "$run/OpenZFSNativeImportTest" /C/OpenZFSNativeImportTest \
    "${maps[@]}" \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
pid=
cleanup(){ if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
qemu-system-x86_64 -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -drive "file=$run/disk0.raw,format=raw,if=ide,index=0" \
    -drive "file=$run/disk1.raw,format=raw,if=ide,index=1" \
    -vga std -display none -device usb-ehci,id=ehci -device usb-tablet,bus=ehci.0 \
    -qmp "unix:$run/qmp.sock,server=on,wait=off" -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!;deadline=$((SECONDS+600));result=1
if [[ $gui == 1 ]];then deadline=$((SECONDS+1800));fi
while :;do
    if [[ -f "$run/serial.log" ]];then
        if rg -q 'RESULT native real file corruption FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log";then break;fi
        if rg -q 'RESULT native real file corruption PASS' "$run/serial.log";then result=0;break;fi
    fi
    kill -0 "$pid"; ((SECONDS<deadline));sleep 1
done
printf 'info blockstats\nquit\n' | socat - "UNIX-CONNECT:$run/monitor" >"$run/blockstats.txt"
wait "$pid";pid=
if [[ $result == 0 ]];then
    rg -q 'Native corrupt file repeated-read PASS retries=3 position-unchanged=1' "$run/serial.log"
fi
sha256sum -c "$baseline/disks-before.sha256"
echo "Native real-file corruption result=$result evidence=$run"
exit "$result"
