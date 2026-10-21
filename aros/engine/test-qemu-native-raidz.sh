#!/usr/bin/env bash
# Create only fresh private GPT hardfiles; never accept a host device target.
set -euo pipefail
if [[ ${OZ_RAIDZ_RUNNER_FROZEN:-0} != 1 ]];then
    export OZ_RAIDZ_RUNNER_FROZEN=1
    exec bash -c "$(< "$0")" "$0" "$@"
fi
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_TEST_ARTIFACT_ROOT:-}")
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
binary=${AROS_TEST_RAIDZ_BINARY:?RAIDZ-capable fixture executable required}
parity=${AROS_TEST_RAIDZ_PARITY:-1}
[[ $parity == 1 || $parity == 2 || $parity == 3 ]]
timeout_seconds=${AROS_TEST_TIMEOUT_SECONDS:-600}
[[ $timeout_seconds =~ ^[1-9][0-9]*$ && ${#timeout_seconds} -le 5 ]] || exit 2
units=(0 1 3);device_count=three
if [[ $parity == 3 ]];then units+=(4);device_count=four;fi
startup=aros/engine/Startup-QEMU-Pool-RAIDZ
if [[ $parity == 2 ]];then startup=aros/engine/Startup-QEMU-Pool-RAIDZ2;fi
if [[ $parity == 3 ]];then startup=aros/engine/Startup-QEMU-Pool-RAIDZ3;fi
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
fs=${AROS_TEST_FS_PACKAGE:?private filesystem package required}
partition=${AROS_TEST_PARTITION_LIBRARY:?private partition library required}
ide_trace=${AROS_TEST_IDE_TRACE:-0}
[[ $ide_trace == 0 || $ide_trace == 1 ]]
qemu=${AROS_TEST_QEMU:-qemu-system-x86_64}
worker_trace=${AROS_TEST_WORKER_TRACE:-0}
[[ $worker_trace == 0 || $worker_trace == 1 ]]
disk_aio=${AROS_TEST_DISK_AIO:-default}
case "$disk_aio" in default) aio_option= ;; threads|io_uring) aio_option=",aio=$disk_aio" ;; *) exit 2;; esac
single_leaf_test=${AROS_TEST_SINGLE_LEAF_FLUSH:-0}
[[ $single_leaf_test == 0 || $single_leaf_test == 1 ]]
if ((single_leaf_test));then
    strings "$binary" | rg -F 'DIAGNOSTIC persistent leaf' >/dev/null
fi
run=$(mktemp -d "$artifact_root/native-raidz-run.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
"$qemu" --version >"$run/qemu-version.txt"
sha256sum "$(command -v "$qemu")" >"$run/qemu.sha256"
printf '%s\n' "$disk_aio" >"$run/disk-aio.txt"
cp "$0" aros/engine/qemu-pool-test.c "$run/"
cp "$startup" "$run/Startup-QEMU-Pool-RAIDZ"
printf '%s\n' "$parity" >"$run/raidz-parity.txt"
printf '%s\n' "$timeout_seconds" >"$run/timeout-seconds.txt"
cp "$binary" "$run/OpenZFSQEMUPoolTest"
sha256sum "$base" "$binary" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" "$posix" "$fs" "$partition" >"$run/inputs.sha256"
drives=();trace_args=()
if [[ $ide_trace == 1 ]];then
    trace_args=(-trace enable=ide_bus_exec_cmd -trace enable=bmdma_cmd_writeb
        -trace enable=ide_dma_cb -trace enable=bmdma_addr_write
        -trace enable=dma_blk_io -trace enable=dma_blk_cb
        -trace enable=dma_complete -trace enable=dma_map_wait
        -trace "file=$run/ide-trace.log")
fi
if [[ $worker_trace == 1 ]];then
    "$qemu" -trace help >"$run/qemu-trace-events.txt"
    for event in thread_pool_worker_start thread_pool_worker_return thread_pool_worker_notified thread_pool_worker_delivered;do
        rg -Fxq "$event" "$run/qemu-trace-events.txt"
        trace_args+=(-trace "enable=$event")
    done
    trace_args+=(-trace "file=$run/ide-trace.log")
    cp aros/engine/qemu-worker-trace.patch "$run/"
fi
for unit in "${units[@]}";do
    python3 aros/engine/pool-partition-fixture.py create "$run/disk$unit.raw" --table gpt >"$run/partition$unit.log" 2>&1
    python3 aros/engine/pool-partition-fixture.py outside "$run/disk$unit.raw" >"$run/outside$unit.before"
    if [[ $unit == 4 ]];then
        drives+=(-device ich9-ahci,id=ozahci
            -drive "file=$run/disk$unit.raw,format=raw,if=none,id=ozfour$aio_option"
            -device ide-hd,drive=ozfour,bus=ozahci.0)
    else
        drives+=(-drive "file=$run/disk$unit.raw,format=raw,if=ide,index=$unit$aio_option")
    fi
done
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$fs" /boot/aros-fs.pkg.xz -map "$partition" /Libs/partition.library \
    -map "$posix" /Libs/posixc.library \
    -map build-aros/hosted/Libs/z1.library /Libs/z1.library \
    -map aros/engine/security-fixture /Security \
    -map "$run/OpenZFSQEMUPoolTest" /C/OpenZFSQEMUPoolTest \
    -map "$run/Startup-QEMU-Pool-RAIDZ" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack.log" 2>&1
"$qemu" "${trace_args[@]}" -name "openzfs-raidz-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" "${drives[@]}" \
    -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" >"$run/qemu.log" 2>&1 &
pid=$!
trap 'if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi' EXIT
failure() {
    printf '%s\n' "$1" | tee "$run/failure.txt" >&2
    if kill -0 "$pid" 2>/dev/null;then
        printf 'info status\ninfo blockstats\ninfo registers\nscreendump %s/failure.ppm\n' "$run" |
            timeout 5 socat - "UNIX-CONNECT:$run/monitor" >"$run/failure-monitor.txt" 2>&1 || true
    fi
    exit 1
}
deadline=$((SECONDS+timeout_seconds))
while :;do
    if [[ -f "$run/serial.log" ]];then
        # Notifications can interrupt RawPutChar mid-marker. Preserve raw
        # bytes and remove only complete, identified notification records.
        perl -0777 -pe 's/[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| \[security.library\] secTaskNotify: [^\r\n]*\r?\n//g' \
            "$run/serial.log" >"$run/serial-completion.log"
        if rg -q 'RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$run/serial.log" "$run/serial-completion.log";then failure 'Guest reported failure';fi
        if rg -q 'blank-label preflight=[1-9][0-9]*|RAIDZ native binding failed' "$run/serial-completion.log";then failure 'Native fixture admission failed';fi
        if rg -q "^RESULT QEMU native $device_count-device raidz$parity PASS" "$run/serial-completion.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null;then failure 'QEMU exited before completion';fi
    if ((SECONDS>=deadline));then failure "RAIDZ ${timeout_seconds}s deadline reached with QEMU live";fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
for unit in "${units[@]}";do
    python3 aros/engine/pool-partition-fixture.py outside "$run/disk$unit.raw" >"$run/outside$unit.after"
    cmp "$run/outside$unit.before" "$run/outside$unit.after"
done
sha256sum "$run"/disk*.raw >"$run/created.sha256"
if ((single_leaf_test));then
    for phase in TXG ZPL;do
        rg -q "^DIAGNOSTIC persistent leaf $phase failures=[1-9][0-9]* healthy=[1-9][0-9]* healthy-errors=0 error=0" "$run/serial-completion.log"
    done
    rg -q '^DIAGNOSTIC TXG flush suspended=1 .* resume=0 synced=0 payload=1 error=0' "$run/serial-completion.log"
    rg -q '^DIAGNOSTIC ZIL flush failed=[1-9][0-9]* .* suspended=1 resume=0 synced=0 retry=0 payload=1 error=0' "$run/serial-completion.log"
fi
echo "PASS: native $device_count-device RAIDZ$parity fixture and DOS tests: $run"
