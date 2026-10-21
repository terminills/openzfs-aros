#!/usr/bin/env bash
# No existing disks accepted. Optional adapter tests write only private
# QEMU snapshots; capacity-only mode issues no writes.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(mktemp -d "$repo/build-aros/ahci-capacity-run.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
release=/opt/aros-workspace/aros-amiwest-nightly-release
sdk=$release/bin/pc-x86_64-smp/AROS/Developer
overlay=${AROS_TEST_AHCI_OVERLAY:-$repo/build-aros/ahci-capacity.ajxoahp2}
device_io=${AROS_TEST_DEVICE_IO:-0}
[[ $device_io == 0 || $device_io == 1 ]]
flush_error=${AROS_TEST_FLUSH_ERROR:-0}
[[ $flush_error == 0 || ( $flush_error == 1 && $device_io == 1 ) ]]
fault_arg=
if [[ $flush_error == 1 ]];then fault_arg=' EXPECT-FLUSH-ERROR';fi
qemu=$repo/build-aros/qemu-worker.Svcpa0sX/build/qemu-system-x86_64
cp "$0" aros/engine/ahci-capacity-test.c "$run/"
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc \
    --sysroot "$sdk" -O2 -Wall -Wextra "$run/ahci-capacity-test.c" \
    -o "$run/AHCICapacityTest" >"$run/build.log" 2>&1
printf '%s\n' 'FailAt 21' 'Stack 131072' 'C:AHCICapacityTest' 'EndCLI' >"$run/Startup-Sequence"
extra=(); marker='RESULT AHCI capacity'
if [[ $device_io == 1 ]];then
    cp aros/engine/{ahci-device-io-test.c,device-io.c,device-io.h} "$run/"
    pthread=$repo/build-aros/pthread-consistent.s3r1CHla
    /opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc \
        --sysroot "$sdk" -B"$sdk/lib/" -nix -O2 -Wall -Wextra -Wno-pointer-sign -DOZ_DEVICE_IO_DIAGNOSTIC \
        -I"$pthread/source" "$run/ahci-device-io-test.c" "$run/device-io.c" \
        "$pthread/libpthread.a" -o "$run/AHCIDeviceIOTest" >"$run/device-io-build.log" 2>&1
    printf '%s\n' 'FailAt 21' 'Stack 1048576' 'C:AHCICapacityTest' \
        "C:AHCIDeviceIOTest DISPOSABLE-QEMU-3T$fault_arg" 'EndCLI' >"$run/Startup-Sequence"
    extra=(-map "$run/AHCIDeviceIOTest" /C/AHCIDeviceIOTest
        -map "$repo/build-aros/loader-overlay.L7ctzwkQ/aros-base.pkg.xz" /boot/aros-base.pkg.xz
        -map "$repo/build-aros/cli-isatty.wSnzlNPf/posixc.library" /Libs/posixc.library
        -map "$repo/build-aros/partition-gpt.1LReDZq8/aros-fs.pkg.xz" /boot/aros-fs.pkg.xz
        -map "$repo/build-aros/partition-gpt.1LReDZq8/partition.library" /Libs/partition.library)
    sha256sum "$run/AHCIDeviceIOTest" "$pthread/libpthread.a" \
        "$repo/build-aros/loader-overlay.L7ctzwkQ/aros-base.pkg.xz" \
        "$repo/build-aros/cli-isatty.wSnzlNPf/posixc.library" \
        "$repo/build-aros/partition-gpt.1LReDZq8/aros-fs.pkg.xz" \
        "$repo/build-aros/partition-gpt.1LReDZq8/partition.library" >"$run/device-io-inputs.sha256"
    marker='RESULT AHCI device IO'
fi
truncate -s 512M "$run/small.raw"
truncate -s 3T "$run/large.raw"
if [[ $device_io == 1 ]];then
    # An entirely blank disk is auto-claimed by AFS on this release. A real
    # ZFS-type GPT partition keeps it unmounted; do not bypass DOS ownership.
    sgdisk --clear --new=1:2048:0 --typecode=1:BF01 "$run/large.raw" >"$run/gpt.log" 2>&1
    sgdisk --verify "$run/large.raw" >>"$run/gpt.log" 2>&1
fi
stat --printf='%n size=%s blocks=%b\n' "$run/"*.raw >"$run/sparse.before"
large_drive="file=$run/large.raw,format=raw,if=none,id=large,aio=threads,snapshot=on"
if [[ $flush_error == 1 ]];then
    qemu-img create -f qcow2 -F raw -b "$run/large.raw" "$run/large.qcow2" >"$run/qcow.log"
    printf '%s\n' '[inject-error]' 'event = "flush_to_disk"' 'iotype = "flush"' \
        'errno = "5"' 'once = "on"' 'immediately = "on"' >"$run/flush-error.conf"
    # A raw wrapper triggers the event on its blkdebug child; the private
    # qcow2 and backing raw live below it. Ordinary writes are not injected.
    large_drive="if=none,id=large,driver=raw,file.driver=blkdebug,file.config=$run/flush-error.conf,file.image.driver=qcow2,file.image.file.driver=file,file.image.file.filename=$run/large.qcow2,file.image.file.aio=threads,werror=report,rerror=report"
fi
sha256sum "$run/AHCICapacityTest" "$overlay/aros-bsp.pkg.xz" \
    "$release/distfiles/aros-smp-pc-x86_64.iso" "$qemu" >"$run/inputs.sha256"
xorriso -indev "$release/distfiles/aros-smp-pc-x86_64.iso" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$overlay/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$run/AHCICapacityTest" /C/AHCICapacityTest \
    -map "$run/Startup-Sequence" /S/Startup-Sequence \
    "${extra[@]}" \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
"$qemu" -name "openzfs-ahci-capacity-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -device ich9-ahci,id=ozahci \
    -drive "file=$run/small.raw,format=raw,if=none,id=small,aio=threads,snapshot=on" \
    -device ide-hd,drive=small,bus=ozahci.0 \
    -drive "$large_drive" \
    -device ide-hd,drive=large,bus=ozahci.1 \
    -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
trap 'if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi' EXIT
deadline=$((SECONDS+300)); result=1
while kill -0 "$pid" 2>/dev/null && ((SECONDS<deadline));do
    if [[ -f $run/serial.log ]];then
        # Preserve raw bytes; strip only identified complete notification
        # records which can interrupt a RawPutChar marker mid-line.
        perl -0777 -pe 's/[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| \[security.library\] secTaskNotify: [^\r\n]*\r?\n//g' \
            "$run/serial.log" >"$run/serial-completion.log"
        if rg -q "^$marker PASS" "$run/serial-completion.log";then result=0;break;fi
        if rg -q '^RESULT AHCI .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial-completion.log";then break;fi
    fi
    sleep 1
done
printf 'info blockstats\n' | socat -T2 - "UNIX-CONNECT:$run/monitor" >"$run/blockstats.txt" || true
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null || true
wait "$pid" || true
trap - EXIT
stat --printf='%n size=%s blocks=%b\n' "$run/"*.raw >"$run/sparse.after"
cmp "$run/sparse.before" "$run/sparse.after"
if [[ $device_io == 1 ]];then
    if ! awk '/^large:/ {for(i=1;i<=NF;i++)if($i ~ /^flush_operations=/){split($i,v,"=");ok=v[2]>=196}} END {exit !ok}' "$run/blockstats.txt";then
        echo 'FAIL: 196 successful native flushes must reach the large QEMU backend'
        result=1
    fi
    for worker in 0 1 2;do
        rg -q "^CONCURRENT worker=$worker completed=64 error=0 join=0 PASS" "$run/serial-completion.log" || result=1
    done
fi
if [[ $flush_error == 1 ]];then
    rg -q '^FLUSH injected error=5 PASS' "$run/serial-completion.log" || result=1
    rg -q '^FLUSH retry PASS' "$run/serial-completion.log" || result=1
fi
rg 'BEGIN read-only|GEOMETRY|CAPACITY unit|BOUNDARY unit|HIGHIO|INQUIRY |CONCURRENT |FLUSH |RESULT AHCI' "$run/serial-completion.log" || true
echo "Diagnostic exit: $result; evidence: $run"
exit "$result"
