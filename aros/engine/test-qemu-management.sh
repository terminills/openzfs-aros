#!/usr/bin/env bash
# Private copies of a verified native mirror. Never attach original disks RW.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
baseline=$(realpath -- "${1:?validated five-boot evidence directory required}")
[[ $baseline == "$repo/build-aros/qemu-run."* ]]
rg -a -q '^RESULT QEMU native two-device mirror PASS' "$baseline/create-serial.log"
rg -a -q '^RESULT cold writable reopen PASS' "$baseline/reopen-serial.log"
sha256sum -c "$baseline/disks-before.sha256"
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?ATA flush overlay required}
run=$(mktemp -d "$repo/build-aros/management-run.XXXXXXXX")
echo "Evidence: $run"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
tar -czf "$run/aros-source.tar.gz" aros/engine aros/zpl aros/ui aros/patches
sha256sum "$base" build-aros/OpenZFSQEMUManagementTest "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" >"$run/inputs.sha256"
cp "$baseline/export.nvlist" "$run/export.nvlist"
for disk in 0 1; do cp --reflink=auto --sparse=always "$baseline/disk$disk.raw" "$run/disk$disk.raw"; done
pid=
cleanup() { if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null; then kill "$pid"; wait "$pid" || true; fi; }
trap cleanup EXIT
for phase in write verify degraded0 degraded1; do
    startup=aros/engine/Startup-QEMU-Management-Write
    [[ $phase != verify ]] || startup=aros/engine/Startup-QEMU-Management-Verify
    [[ $phase != degraded* ]] || startup=aros/engine/Startup-QEMU-Management-Degraded
    drive_args=()
    for disk in 0 1; do
        if [[ $phase != degraded* || $phase == degraded$disk ]]; then
            drive_args+=(-drive "file=$run/disk$disk.raw,format=raw,if=ide,index=$disk")
        fi
    done
    # AROS ATA unit numbering needs an occupied first slot to preserve unit
    # one. This blank disk has no pool labels and is not a mirror member.
    if [[ $phase == degraded1 ]]; then
        qemu-img create -f qcow2 "$run/blank-nonmember.qcow2" 256M
        drive_args+=(-drive "file=$run/blank-nonmember.qcow2,format=qcow2,if=ide,index=0")
    fi
    if [[ $phase == verify ]]; then sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/verify-before.sha256"; fi
    xorriso -indev "$base" -outdev "$run/$phase.iso" \
        -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
        -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
        -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
        -map build-aros/hosted/Libs/z1.library /Libs/z1.library \
        -map build-aros/OpenZFSQEMUManagementTest /C/OpenZFSQEMUManagementTest \
        -map "$run/export.nvlist" /export.nvlist -map "$startup" /S/Startup-Sequence \
        -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack-$phase.log" 2>&1
    qemu-system-x86_64 -name "openzfs-management-$phase-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
        -drive "file=$run/$phase.iso,media=cdrom,if=ide,index=2,readonly=on" \
        "${drive_args[@]}" \
        -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
        -serial "file:$run/$phase-serial.log" -monitor "unix:$run/$phase.monitor,server=on,wait=off" >"$run/$phase-qemu.log" 2>&1 &
    pid=$!;deadline=$((SECONDS+180))
    until rg -a -q '^RESULT management clone PASS' "$run/$phase-serial.log" 2>/dev/null; do
        if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline)) ||
            rg -a -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/$phase-serial.log" 2>/dev/null; then
            if kill -0 "$pid" 2>/dev/null; then
                printf 'screendump %s/%s-failure.ppm\n' "$run" "$phase" |
                    timeout 5 socat - "UNIX-CONNECT:$run/$phase.monitor" >"$run/$phase-failure-monitor.txt" || true
            fi
            echo "$phase failed; evidence $run" >&2;exit 1
        fi
        sleep 1
    done
    printf 'info blockstats\nscreendump %s/%s.ppm\n' "$run" "$phase" |
        socat - "UNIX-CONNECT:$run/$phase.monitor" >"$run/$phase-monitor.txt"
    # A kernel task-notification record may interrupt RawPutChar mid-line.
    # Preserve raw evidence and rejoin only these known notification records;
    # never filter crashes, assertions or access-control errors.
    checked="$run/$phase-checked.log"
    perl -0777 -pe 's/[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| \[security.library\] secTaskNotify: [^\r\n]*\r?\n//g' \
        "$run/$phase-serial.log" >"$checked"
    [[ $(rg -c '^management mounted DOS read=0' "$checked") == 5 ]]
    rg -a -q '^management dataset create/root ACL/persistence=0' "$checked"
    rg -a -q '^management rename/destroy persistence=0' "$checked"
    rg -a -q '^management pagination=0' "$checked"
    rg -a -q '^management direct snapshot list=0' "$checked"
    rg -a -q '^management persistent holds=0' "$checked"
    rg -a -q '^management hold pagination and last release=0' "$checked"
    [[ $(rg -c '^management list .*result=0' "$checked") == 6 ]]
    rg -a -q '^management scrub completion=0' "$checked"
    scrub_polls=$(sed -n 's/^management scrub status polls=\([0-9]*\).*/\1/p' "$checked")
    [[ $scrub_polls =~ ^[0-9]+$ && $scrub_polls -ge 1 && $scrub_polls -le 60 ]]
    expected_ipc=$((31+scrub_polls))
    [[ $phase != write ]] || expected_ipc=$((155+scrub_polls))
    # Per-request serial lines can interleave with security task notifications.
    # The guest verifies the complete count after joining every IPC client.
    rg -a -q "^management IPC verified total=$expected_ipc" "$checked"
    if [[ $phase == write ]]; then
        rg -a -q '^management scrub controls=0' "$checked"
        for state in paused scanning canceled; do
            rg -a -q "^scan=scrub state=$state=0" "$checked"
        done
        rg -a -q '^management mirror status=0' "$checked"
        [[ $(rg -c '^ide0-hd[01]: .*flush_operations=[1-9][0-9]* ' "$run/$phase-monitor.txt") == 2 ]]
    else
        expected_disks=2
        if [[ $phase == degraded* ]]; then
            expected_disks=1
            [[ $phase != degraded1 ]] || expected_disks=2
            rg -a -q '^management degraded status=0' "$checked"
        else
            rg -a -q '^management mirror status=0' "$checked"
        fi
        [[ $(rg -c '^ide0-hd[01]: .*wr_bytes=0 ' "$run/$phase-monitor.txt") == "$expected_disks" ]]
    fi
    printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
    wait "$pid";pid=
done
sha256sum -c "$baseline/disks-before.sha256"
sha256sum -c "$run/verify-before.sha256"
if [[ -x build-control/zdb ]]; then
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        build-control/zdb -e -p "$run/disk0.raw" -p "$run/disk1.raw" -bcc qemu_mirror >"$run/management-zdb.log" 2>&1
    rg -q 'No leaks' "$run/management-zdb.log"
fi
echo "PASS: native management clone independence and cold persistence. Evidence: $run"
