#!/usr/bin/env bash
# Fresh writable COW fixtures only. Optional qualified baseline is backing-only.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(mktemp -d "$repo/build-aros/ahci-large-pool.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
release=/opt/aros-workspace/aros-amiwest-nightly-release
binary=${AROS_TEST_LARGE_POOL_BINARY_DIR:-$repo/build-aros/source-engine.P2gN7Bsv/ahci-survivors}
overlay=${AROS_TEST_AHCI_OVERLAY:-$repo/build-aros/ahci-capacity.nwbhsddj}
baseline=${AROS_TEST_LARGE_POOL_BASELINE:-}
if [[ ${AROS_TEST_LARGE_RESILVER:-0} == 1 ]];then
    [[ ${AROS_TEST_LARGE_DEGRADED_WRITE:-0} == 1 ]]
fi
if [[ -n $baseline ]];then
    baseline=$(realpath "$baseline")
    [[ $baseline == "$repo"/build-aros/ahci-large-pool.* && $baseline != "$run" ]]
    rg -q '^RESULT QEMU native two-device mirror PASS' "$baseline/create-checked.log"
    [[ -s $baseline/export.nvlist ]]
    sha256sum -c "$baseline/created.sha256"
    echo "Reusing read-only qualified baseline: $baseline"
    cp -p "$baseline/export.nvlist" "$run/export.nvlist"
fi
qemu=$repo/build-aros/qemu-worker.Svcpa0sX/build/qemu-system-x86_64
cp "$0" aros/engine/{qemu-pool-test.c,qemu-import-test.c} "$run/"
cp "$binary"/OpenZFSQEMU{Pool,Import}Test "$run/"
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
sed 's/DISPOSABLE /DISPOSABLE-AHCI3T /' aros/engine/Startup-QEMU-Pool-Test >"$run/startup-create"
sed 's/READONLY /READONLY-AHCI3T /' aros/engine/Startup-QEMU-Import-Test >"$run/startup-cold"
for survivor in 0 1;do
    sed "s/READONLY /READONLY-AHCI3T-$survivor /" aros/engine/Startup-QEMU-Import-Test >"$run/startup-survivor$survivor"
    if [[ ${AROS_TEST_LARGE_DEGRADED_WRITE:-0} == 1 ]];then
        sed "s/READONLY /WRITE-AHCI3T-$survivor /" aros/engine/Startup-QEMU-Import-Test >"$run/startup-writer$survivor"
        sed "s/READONLY /VERIFY-AHCI3T-$survivor /" aros/engine/Startup-QEMU-Import-Test >"$run/startup-verify$survivor"
        sed "s/READONLY /RECOVER-AHCI3T-$survivor /" aros/engine/Startup-QEMU-Import-Test >"$run/startup-recover$survivor"
        sed "s/READONLY /REPAIRED-AHCI3T-$survivor /" aros/engine/Startup-QEMU-Import-Test >"$run/startup-repaired$survivor"
    fi
done
sha256sum "$run"/OpenZFSQEMU*Test "$overlay/aros-bsp.pkg.xz" \
    "$repo/build-aros/loader-overlay.L7ctzwkQ/aros-base.pkg.xz" \
    "$repo/build-aros/cli-isatty.wSnzlNPf/posixc.library" \
    "$repo/build-aros/partition-gpt.1LReDZq8/aros-fs.pkg.xz" \
    "$repo/build-aros/partition-gpt.1LReDZq8/partition.library" \
    "$repo/build-aros/hosted/Libs/z1.library" "$qemu" \
    "$release/distfiles/aros-smp-pc-x86_64.iso" >"$run/inputs.sha256"
for unit in 0 1;do
    if [[ -n $baseline ]];then
        qemu-img create -f qcow2 -F qcow2 -b "$baseline/create$unit.qcow2" "$run/create$unit.qcow2"
        continue
    fi
    truncate -s 3T "$run/disk$unit.raw"
    sgdisk --clear --new=1:2048:6442448895 --typecode=1:BF01 \
        "$run/disk$unit.raw" >"$run/gpt$unit.log" 2>&1
    sgdisk --verify "$run/disk$unit.raw" >>"$run/gpt$unit.log" 2>&1
    qemu-img create -f qcow2 -F raw -b "$run/disk$unit.raw" "$run/create$unit.qcow2"
done
if [[ -z $baseline ]];then stat --printf='%n size=%s blocks=%b\n' "$run/"*.raw >"$run/sparse.before";fi
pack() {
    local phase=$1
    local extras=()
    if [[ $phase != create ]];then extras=(-map "$run/export.nvlist" /export.nvlist);fi
    xorriso -indev "$release/distfiles/aros-smp-pc-x86_64.iso" -outdev "$run/$phase.iso" \
        -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
        -map "$overlay/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
        -map "$repo/build-aros/loader-overlay.L7ctzwkQ/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
        -map "$repo/build-aros/partition-gpt.1LReDZq8/aros-fs.pkg.xz" /boot/aros-fs.pkg.xz \
        -map "$repo/build-aros/partition-gpt.1LReDZq8/partition.library" /Libs/partition.library \
        -map "$repo/build-aros/cli-isatty.wSnzlNPf/posixc.library" /Libs/posixc.library \
        -map "$repo/build-aros/hosted/Libs/z1.library" /Libs/z1.library \
        -map aros/engine/security-fixture /Security \
        -map "$run/OpenZFSQEMUPoolTest" /C/OpenZFSQEMUPoolTest \
        -map "$run/OpenZFSQEMUImportTest" /C/OpenZFSQEMUImportTest \
        -map "$run/startup-$phase" /S/Startup-Sequence "${extras[@]}" \
        -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/$phase-pack.log" 2>&1
}
pid=
trap 'if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi' EXIT
boot() {
    local phase=$1 success=$2 deadline survivor=${3:-both}
    local drives=()
    for unit in 0 1;do
        if [[ $survivor == both || $survivor == "$unit" ]];then
            drives+=(-drive "file=$run/${phase}$unit.qcow2,format=qcow2,if=none,id=disk$unit,aio=threads"
                -device "ide-hd,drive=disk$unit,bus=ozahci.$unit")
        fi
    done
    "$qemu" -name "openzfs-large-$phase-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
        -drive "file=$run/$phase.iso,media=cdrom,if=ide,index=2,readonly=on" \
        -device ich9-ahci,id=ozahci \
        "${drives[@]}" \
        -display none -nic none -boot d -no-reboot -no-shutdown \
        -serial "file:$run/$phase-serial.log" -monitor "unix:$run/$phase.monitor,server=on,wait=off" >"$run/$phase-qemu.log" 2>&1 &
    pid=$!;deadline=$((SECONDS+900))
    while :;do
        if [[ -f $run/$phase-serial.log ]];then
            perl -0777 -pe 's/[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| \[security.library\] secTaskNotify: [^\r\n]*\r?\n//g' \
                "$run/$phase-serial.log" >"$run/$phase-checked.log"
            if rg -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at|blank-label preflight=[1-9]' "$run/$phase-checked.log";then return 1;fi
            if rg -q "$success" "$run/$phase-checked.log";then break;fi
        fi
        if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then return 1;fi
        sleep 1
    done
    printf 'info blockstats\n' | socat -T2 - "UNIX-CONNECT:$run/$phase.monitor" >"$run/$phase-blockstats.txt"
    printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
    wait "$pid";pid=
}
if [[ -z $baseline ]];then
pack create
boot create '^RESULT QEMU native two-device mirror PASS'
for unit in 0 1;do
    rg -q "^disk$unit: .*wr_operations=[1-9][0-9]* flush_operations=[1-9][0-9]* " "$run/create-blockstats.txt"
done
awk '/^EXPORT_XDR / {gsub(/\r/, "", $2); print $2}' "$run/create-checked.log" | xxd -r -p >"$run/export.nvlist"
expected=$(awk '/^EXPORT_XDR_BEGIN / {gsub(/\r/, "", $2); print $2}' "$run/create-checked.log")
[[ $expected =~ ^[0-9]+$ && $(stat -c %s "$run/export.nvlist") == "$expected" ]]
fi
sha256sum "$run"/create*.qcow2 >"$run/created.sha256"
for unit in 0 1;do
    qemu-img create -f qcow2 -F qcow2 -b "$run/create$unit.qcow2" "$run/cold$unit.qcow2"
done
sha256sum "$run"/cold*.qcow2 >"$run/cold.sha256"
pack cold
boot cold '^RESULT cold-boot QEMU two-drive pool through AROS handler PASS'
rg -q '^cold large-pool usable bytes=' "$run/cold-checked.log"
for unit in 0 1;do
    rg -q "^disk$unit: .*wr_bytes=0 .*wr_operations=0 " "$run/cold-blockstats.txt"
done
sha256sum -c "$run/created.sha256"
sha256sum -c "$run/cold.sha256"
for survivor in 0 1;do
    phase=survivor$survivor
    qemu-img create -f qcow2 -F qcow2 -b "$run/create$survivor.qcow2" "$run/$phase$survivor.qcow2"
    sha256sum "$run/$phase$survivor.qcow2" >"$run/$phase.sha256"
    pack "$phase"
    boot "$phase" '^RESULT cold-boot QEMU degraded mirror through AROS handler PASS' "$survivor"
    rg -q '^cold pool health=6 expected=6' "$run/$phase-checked.log"
    rg -q "^disk$survivor: .*wr_bytes=0 .*wr_operations=0 " "$run/$phase-blockstats.txt"
    sha256sum -c "$run/created.sha256"
    sha256sum -c "$run/$phase.sha256"
done
if [[ ${AROS_TEST_LARGE_DEGRADED_WRITE:-0} == 1 ]];then
    for survivor in 0 1;do
        phase=writer$survivor
        qemu-img create -f qcow2 -F qcow2 -b "$run/create$survivor.qcow2" "$run/$phase$survivor.qcow2"
        pack "$phase"
        boot "$phase" '^RESULT cold-boot QEMU degraded mirror through AROS handler PASS' "$survivor"
        rg -q "^degraded marker member=$survivor write=1 bytes=2097152 error=0" "$run/$phase-checked.log"
        rg -q '^degraded writer export=0' "$run/$phase-checked.log"
        rg -q "^disk$survivor: .*wr_operations=[1-9][0-9]* flush_operations=[1-9][0-9]* " "$run/$phase-blockstats.txt"
        sha256sum -c "$run/created.sha256"
        sha256sum "$run/$phase$survivor.qcow2" >"$run/$phase.sha256"
        phase=verify$survivor
        qemu-img create -f qcow2 -F qcow2 -b "$run/writer$survivor$survivor.qcow2" "$run/$phase$survivor.qcow2"
        sha256sum "$run/$phase$survivor.qcow2" >"$run/$phase.sha256"
        pack "$phase"
        boot "$phase" '^RESULT cold-boot QEMU degraded mirror through AROS handler PASS' "$survivor"
        rg -q "^degraded marker member=$survivor write=0 bytes=2097152 error=0" "$run/$phase-checked.log"
        rg -q "^disk$survivor: .*wr_bytes=0 .*wr_operations=0 " "$run/$phase-blockstats.txt"
        sha256sum -c "$run/created.sha256"
        sha256sum -c "$run/writer$survivor.sha256"
        sha256sum -c "$run/$phase.sha256"
        if [[ ${AROS_TEST_LARGE_RESILVER:-0} == 1 ]];then
            phase=recover$survivor
            repaired=$((1-survivor))
            for unit in 0 1;do
                backing=$run/create$unit.qcow2
                if [[ $unit == "$survivor" ]];then backing=$run/writer$survivor$unit.qcow2;fi
                qemu-img create -f qcow2 -F qcow2 -b "$backing" "$run/$phase$unit.qcow2"
            done
            pack "$phase"
            boot "$phase" '^RESULT cold-boot QEMU two-drive pool through AROS handler PASS'
            rg -q '^large native resilver processed=[1-9][0-9]* errors=0 pending=0' "$run/$phase-checked.log"
            rg -q '^cold pool health=7 expected=7' "$run/$phase-checked.log"
            rg -q '^degraded writer export=0' "$run/$phase-checked.log"
            for unit in 0 1;do
                rg -q "^disk$unit: .*wr_operations=[1-9][0-9]* flush_operations=[1-9][0-9]* " "$run/$phase-blockstats.txt"
            done
            sha256sum "$run/$phase"[01].qcow2 >"$run/$phase.sha256"
            phase=repaired$survivor
            qemu-img create -f qcow2 -F qcow2 -b "$run/recover$survivor$repaired.qcow2" "$run/$phase$repaired.qcow2"
            sha256sum "$run/$phase$repaired.qcow2" >"$run/$phase.sha256"
            pack "$phase"
            boot "$phase" '^RESULT cold-boot QEMU degraded mirror through AROS handler PASS' "$repaired"
            rg -q "^degraded marker member=$survivor write=0 bytes=2097152 error=0" "$run/$phase-checked.log"
            rg -q "^disk$repaired: .*wr_bytes=0 .*wr_operations=0 " "$run/$phase-blockstats.txt"
            sha256sum -c "$run/created.sha256"
            sha256sum -c "$run/writer$survivor.sha256"
            sha256sum -c "$run/recover$survivor.sha256"
            sha256sum -c "$run/$phase.sha256"
            echo "PASS native large AHCI resilver and repaired-member-only cold read: source=$survivor repaired=$repaired"
        fi
    done
    echo 'PASS native large AHCI degraded writes and cold persisted bytes on either member'
fi
if [[ -z $baseline ]];then
    stat --printf='%n size=%s blocks=%b\n' "$run/"*.raw >"$run/sparse.after"
    cmp "$run/sparse.before" "$run/sparse.after"
else
    sha256sum -c "$baseline/created.sha256"
fi
echo "PASS native large AHCI mirror qualification (baseline=${baseline:-fresh-create}): $run"
