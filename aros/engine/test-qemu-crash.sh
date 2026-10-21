#!/usr/bin/env bash
# Only owned file-backed VMs. Retain the pre-recovery crash disks unchanged.
set -euo pipefail
if [[ ${OZ_CRASH_RUNNER_FROZEN:-0} != 1 ]];then
    export OZ_CRASH_RUNNER_FROZEN=1
    exec bash -c "$(< "$0")" "$0" "$@"
fi
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
[[ -x build-control/zdb ]] || {
    echo 'Independent zdb verifier is required for crash qualification.' >&2
    exit 2
}
qemu=${AROS_TEST_QEMU:-qemu-system-x86_64}
command -v "$qemu" >/dev/null
disk_aio=${AROS_TEST_DISK_AIO:-default}
case "$disk_aio" in default) aio_option= ;; threads|io_uring) aio_option=",aio=$disk_aio" ;; *) exit 2;; esac
ide_trace=${AROS_TEST_IDE_TRACE:-0}
[[ $ide_trace == 0 || $ide_trace == 1 ]] || exit 2
timeout_seconds=${AROS_TEST_TIMEOUT_SECONDS:-180}
[[ $timeout_seconds =~ ^[1-9][0-9]*$ && ${#timeout_seconds} -le 5 ]] || exit 2
artifact_root="$repo/build-aros"
if [[ -n ${AROS_TEST_ARTIFACT_ROOT:-} ]];then
    artifact_root=$(realpath -e "$AROS_TEST_ARTIFACT_ROOT")
    [[ -d $artifact_root && -w $artifact_root ]]
    [[ $(stat -c %d "$artifact_root") != $(stat -c %d "$repo/build-aros") ]] || {
        echo 'External artifact root must reside on a separate mounted filesystem' >&2;exit 2;
    }
fi
source "$repo/aros/engine/storage-preflight.sh"
oz_storage_preflight "$repo/build-aros"
oz_storage_preflight "$artifact_root"
baseline=$(realpath -- "${1:?validated five-boot evidence directory required}")
[[ $baseline == "$artifact_root/qemu-run."* && $(dirname -- "$baseline") == "$artifact_root" ]]
for disk in 0 1; do test -f "$baseline/disk$disk.raw"; done
rg -a -q '^RESULT QEMU native two-device mirror PASS' "$baseline/create-serial.log"
rg -a -q '^RESULT cold writable reopen PASS' "$baseline/reopen-serial.log"
sha256sum -c "$baseline/disks-before.sha256"
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?ATA flush overlay required}
binary=${AROS_TEST_CRASH_BINARY:-$repo/build-aros/OpenZFSQEMUCrashTest}
legacy=${AROS_TEST_CRASH_LEGACY_VERSION:-0}
[[ $legacy =~ ^[0-4]$ ]]
slog=${AROS_TEST_CRASH_SLOG:-0}
[[ $slog == 0 || $slog == 1 ]]
if ((slog));then
    [[ $legacy == 0 ]]
    strings "$binary" | rg -F 'crash dedicated SLOG anchor' >/dev/null
fi
run=$(mktemp -d "$artifact_root/crash-run.XXXXXXXX")
echo "Evidence: $run"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
"$qemu" --version >"$run/qemu-version.txt"
sha256sum "$(command -v "$qemu")" >"$run/qemu.sha256"
printf '%s\n' "$disk_aio" >"$run/disk-aio.txt"
printf '%s\n' "$timeout_seconds" >"$run/timeout-seconds.txt"
if ((ide_trace));then
    # Limit tracing to command/transfer transitions, not per-byte port I/O.
    printf '%s\n' ide_bus_exec_cmd ide_dma_cb ide_sector_write \
        bmdma_cmd_writeb >"$run/ide-trace-events.txt"
    "$qemu" -trace help >"$run/qemu-trace-help.txt" 2>&1
    while read -r event;do
        rg -x -F "$event" "$run/qemu-trace-help.txt" >/dev/null
    done <"$run/ide-trace-events.txt"
fi
tar -czf "$run/aros-source.tar.gz" aros/engine aros/zpl aros/patches
cp "$binary" "$run/OpenZFSQEMUCrashTest"
sha256sum "$base" "$run/OpenZFSQEMUCrashTest" "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" >"$run/inputs.sha256"
extra_maps=()
members=(0 1)
if ((slog));then
    members+=(3)
    printf '1\n' >"$run/slog-test"
    extra_maps+=(-map "$run/slog-test" /slog-test)
    python3 aros/engine/pool-partition-fixture.py create "$run/crash3.raw" --table gpt >"$run/slog-layout.log" 2>&1
    python3 aros/engine/pool-partition-fixture.py outside "$run/crash3.raw" >"$run/slog-outside-before.txt"
fi
if [[ $legacy != 0 ]];then
    printf '%s\n' "$legacy" > "$run/legacy-version"
    extra_maps+=(-map "$run/legacy-version" /legacy-version)
fi
if [[ -n ${AROS_TEST_POSIX_LIBRARY:-} ]];then
    cp "$AROS_TEST_POSIX_LIBRARY" "$run/posixc.library"
    sha256sum "$run/posixc.library" >> "$run/inputs.sha256"
    extra_maps+=(-map "$run/posixc.library" /Libs/posixc.library)
fi
if [[ -n ${AROS_TEST_PARTITION_LIBRARY:-} ]];then
    cp "$AROS_TEST_PARTITION_LIBRARY" "$run/partition.library"
    sha256sum "$run/partition.library" >> "$run/inputs.sha256"
    extra_maps+=(-map "$run/partition.library" /Libs/partition.library)
fi
cp "$baseline/export.nvlist" "$run/export.nvlist"
for disk in 0 1; do cp --reflink=auto --sparse=always "$baseline/disk$disk.raw" "$run/crash$disk.raw"; done
pid=
cleanup() { if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null; then kill "$pid"; wait "$pid" || true; fi; }
trap cleanup EXIT
phases=(stage recover verify)
if [[ ${AROS_TEST_PARTIAL_REBOOT:-0} == 1 ]]; then phases=(stage partial resume verify); fi
for phase in "${phases[@]}"; do
    startup=aros/engine/Startup-QEMU-Crash-Stage
    prefix=crash
    if [[ $phase == recover || $phase == partial ]]; then
        startup=aros/engine/Startup-QEMU-Crash-Recover;prefix=recover
        [[ $phase != partial ]] || startup=aros/engine/Startup-QEMU-Crash-Partial
        sha256sum "$run/crash0.raw" "$run/crash1.raw" >"$run/crash-before.sha256"
        if ((slog));then sha256sum "$run/crash3.raw" >>"$run/crash-before.sha256";fi
        for disk in "${members[@]}"; do cp --reflink=auto --sparse=always "$run/crash$disk.raw" "$run/recover$disk.raw"; done
    fi
    if [[ $phase == resume ]]; then
        startup=aros/engine/Startup-QEMU-Crash-Resume;prefix=recover
        for disk in "${members[@]}"; do cp --reflink=auto --sparse=always "$run/recover$disk.raw" "$run/partial$disk.raw"; done
        sha256sum "$run/partial0.raw" "$run/partial1.raw" >"$run/partial-before.sha256"
        if ((slog));then sha256sum "$run/partial3.raw" >>"$run/partial-before.sha256";fi
    fi
    if [[ $phase == verify ]]; then
        startup=aros/engine/Startup-QEMU-Crash-Verify;prefix=recover
    fi
    current_config="$run/export.nvlist"
    if ((slog)) && [[ $phase != stage ]];then current_config="$run/slog-config.nvlist";fi
    slog_drive=()
    if ((slog));then slog_drive=(-drive "file=$run/${prefix}3.raw,format=raw,if=ide,index=3$aio_option");fi
    xorriso -indev "$base" -outdev "$run/$phase.iso" \
        -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
        -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
        -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
        -map build-aros/hosted/Libs/z1.library /Libs/z1.library \
        -map "$run/OpenZFSQEMUCrashTest" /C/OpenZFSQEMUCrashTest "${extra_maps[@]}" \
        -map "$current_config" /export.nvlist -map "$startup" /S/Startup-Sequence \
        -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack-$phase.log" 2>&1
    trace_options=()
    if ((ide_trace));then
        trace_options=(-trace "events=$run/ide-trace-events.txt,file=$run/$phase-ide-trace.log")
    fi
    "$qemu" "${trace_options[@]}" -name "openzfs-crash-$phase-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
        -drive "file=$run/$phase.iso,media=cdrom,if=ide,index=2,readonly=on" \
        -drive "file=$run/${prefix}0.raw,format=raw,if=ide,index=0$aio_option" \
        -drive "file=$run/${prefix}1.raw,format=raw,if=ide,index=1$aio_option" \
        "${slog_drive[@]}" \
        -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
        -serial "file:$run/$phase-serial.log" \
        -monitor "unix:$run/$phase.monitor,server=on,wait=off" >"$run/$phase-qemu.log" 2>&1 &
    pid=$!
    marker='^CRASH READY committed ZIL without clean close'
    [[ $phase == stage ]] || marker='^RESULT crash recovery PASS'
    [[ $phase != partial ]] || marker='^CRASH READY partial replay persisted without export'
    deadline=$((SECONDS+timeout_seconds))
    until rg -a -q "$marker" "$run/$phase-serial.log" 2>/dev/null; do
        if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline)) ||
            rg -a -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at|signal=6 default action=terminate' "$run/$phase-serial.log" 2>/dev/null; then
            if kill -0 "$pid" 2>/dev/null; then
                printf 'info blockstats\nscreendump %s/%s-failure.ppm\n' "$run" "$phase" |
                    timeout 5 socat - "UNIX-CONNECT:$run/$phase.monitor" >"$run/$phase-failure-monitor.txt" || true
            fi
            echo "$phase failed or timed out; evidence $run" >&2;exit 1
        fi
        sleep 1
    done
    if [[ $legacy != 0 ]];then rg -a -q "^crash legacy version=$legacy" "$run/$phase-serial.log";fi
    printf 'info blockstats\nscreendump %s/%s.ppm\n' "$run" "$phase" |
        socat - "UNIX-CONNECT:$run/$phase.monitor" >"$run/$phase-monitor.txt"
    if [[ $phase == stage || $phase == partial ]]; then
        if [[ $phase == stage ]]; then
            rg -a -q '^crash namespace staged=0' "$run/$phase-serial.log"
            if ((slog));then
                rg -a -q '^crash dedicated SLOG anchor=0' "$run/$phase-serial.log"
                rg -a -q '^SLOG_CONFIG_END=0' "$run/$phase-serial.log"
                python3 aros/engine/decode-crash-config.py "$run/$phase-serial.log" "$run/slog-config.nvlist"
                sha256sum "$run/slog-config.nvlist" >"$run/slog-config.sha256"
                rg -q '^ide1-hd1: .*flush_operations=[1-9][0-9]* ' "$run/$phase-monitor.txt"
            fi
        else
            rg -a -q '^crash failed replay retained log=0' "$run/$phase-serial.log"
            rg -a -q '^crash partial replay returned=5' "$run/$phase-serial.log"
            rg -a -q '^crash partial replay cleanup=0' "$run/$phase-serial.log"
        fi
        [[ $(rg -c '^ide0-hd[01]: .*flush_operations=[1-9][0-9]* ' "$run/$phase-monitor.txt") == 2 ]]
        # Hard power loss of this exact owned VM: no guest close/export/fini.
        kill -KILL "$pid"
        status=0;wait "$pid" || status=$?
        [[ $status == 137 ]];pid=
    else
        rg -a -q '^crash recovered committed bytes=0' "$run/$phase-serial.log"
        rg -a -q '^crash namespace recovered=0' "$run/$phase-serial.log"
        if [[ $phase == recover ]]; then
            rg -a -q '^crash failed replay retained log=0' "$run/$phase-serial.log"
            rg -a -q '^crash partial replay returned=5' "$run/$phase-serial.log"
            rg -a -q '^crash partial replay cleanup=0' "$run/$phase-serial.log"
        elif [[ $phase == verify ]]; then
            rg -a -q '^crash ZIL replay needed=0' "$run/$phase-serial.log"
        else
            rg -a -q '^crash ZIL replay needed=1' "$run/$phase-serial.log"
        fi
        printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
        wait "$pid";pid=
    fi
done
sha256sum -c "$run/crash-before.sha256"
if [[ -f $run/partial-before.sha256 ]]; then sha256sum -c "$run/partial-before.sha256"; fi
sha256sum -c "$baseline/disks-before.sha256"
# Independent checksum/leak verification is mandatory, never a silent skip.
    verify0="$run/recover0.raw";verify1="$run/recover1.raw"
    if [[ -f $baseline/partition0-layout.log ]];then
        for disk in 0 1;do
            python3 aros/engine/pool-partition-fixture.py outside "$baseline/disk$disk.raw" > "$run/outside$disk-before.txt"
            python3 aros/engine/pool-partition-fixture.py outside "$run/recover$disk.raw" > "$run/outside$disk-after.txt"
            cmp "$run/outside$disk-before.txt" "$run/outside$disk-after.txt"
            python3 aros/engine/pool-partition-fixture.py extract "$run/recover$disk.raw" "$run/recover-partition$disk.raw"
        done
        verify0="$run/recover-partition0.raw";verify1="$run/recover-partition1.raw"
    fi
    extra_verify=()
    if ((slog));then
        python3 aros/engine/pool-partition-fixture.py outside "$run/recover3.raw" >"$run/slog-outside-after.txt"
        cmp "$run/slog-outside-before.txt" "$run/slog-outside-after.txt"
        python3 aros/engine/pool-partition-fixture.py extract "$run/recover3.raw" "$run/recover-partition3.raw"
        extra_verify=(-p "$run/recover-partition3.raw")
    fi
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        build-control/zdb -e -p "$verify0" -p "$verify1" "${extra_verify[@]}" -bcc qemu_mirror >"$run/recovery-zdb.log" 2>&1
    rg -q 'No leaks' "$run/recovery-zdb.log"
echo "PASS: committed intent-log bytes recovered after private VM power loss. Evidence: $run"
