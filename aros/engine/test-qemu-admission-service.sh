#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
baseline=$(realpath -- "${1:?verified mirror evidence directory required}")
[[ $baseline == "$repo/build-aros/qemu-run."* ]]
rg -a -q '^RESULT QEMU native two-device mirror PASS' "$baseline/create-serial.log"
rg -a -q '^RESULT cold writable reopen PASS' "$baseline/reopen-serial.log"
sha256sum -c "$baseline/disks-before.sha256"
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private security/ATA overlay required}
service=${AROS_TEST_SERVICE_BINARY:-build-aros/OpenZFSAdmissionServiceHarness}
client=${AROS_TEST_CLIENT_BINARY:-build-aros/OpenZFSAdmissionServiceTest}
run=$(mktemp -d "$repo/build-aros/admission-service-run.XXXXXXXX")
echo "Evidence: $run"
iso_dir=$run
if [[ -n ${AROS_TEST_ISO_TMPDIR:-} ]];then
    echo 'AROS_TEST_ISO_TMPDIR is retired; ISO evidence stays in the private repo run directory' >&2;exit 2
fi
printf '%s\n' "$iso_dir" >"$run/iso-location.txt"
# Freeze both executables before the first boot. A concurrent next build must
# not silently replace the executable packed into the second-phase ISO.
cp -- "$service" "$run/OpenZFSAdmissionServiceHarness"
cp -- "$client" "$run/OpenZFSAdmissionServiceTest"
for manifest in crypto-inputs.sha256 crypto-members.sha256; do
    if [[ -f "$(dirname "$service")/$manifest" ]]; then
        cp -- "$(dirname "$service")/$manifest" "$run/"
    fi
done
service="$run/OpenZFSAdmissionServiceHarness"
client="$run/OpenZFSAdmissionServiceTest"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
tar -czf "$run/aros-source.tar.gz" aros/engine aros/ui aros/zpl
sha256sum "$base" "$service" "$client" \
    "$dos/aros-base.pkg.xz" "$ata/aros-bsp.pkg.xz" >"$run/inputs.sha256"
extra_maps=()
if [[ -n ${AROS_TEST_POSIX_LIBRARY:-} ]];then
    cp -- "$AROS_TEST_POSIX_LIBRARY" "$run/posixc.library"
    sha256sum "$run/posixc.library" >>"$run/inputs.sha256"
    extra_maps+=(-map "$run/posixc.library" /Libs/posixc.library)
fi
cp "$baseline/export.nvlist" "$run/export.nvlist"
for disk in 0 1; do cp --reflink=auto --sparse=always "$baseline/disk$disk.raw" "$run/disk$disk.raw"; done
pid=
cleanup() { if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null; then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
phases=(write verify)
case ${AROS_TEST_SUITE:-admission} in
    admission) phases=(write verify replication) ;;
    initialize) phases=(write verify resume) ;;
    exec) ;;
    device) ;;
    create) phases=(create) ;;
    add) phases=(add) ;;
    remove-cancel) phases=(cancel) ;;
    split) phases=(split) ;;
    *) echo 'Unknown suite' >&2;exit 2 ;;
esac
create_service_mode=${AROS_TEST_CREATE_SERVICE_MODE:-WRITE}
case "$create_service_mode" in
    WRITE) ;;
    CREATE|WRITE-INITIALIZE) [[ ${AROS_TEST_SUITE:-admission} == create ]] ;;
    *) echo 'AROS_TEST_CREATE_SERVICE_MODE must be WRITE, CREATE or WRITE-INITIALIZE' >&2;exit 2 ;;
esac
printf '%s\n' "${AROS_TEST_SUITE:-admission}" >"$run/suite.txt"
printf '%s\n' "$create_service_mode" >"$run/create-service-mode.txt"
for phase in "${phases[@]}"; do
    startup=aros/engine/Startup-Admission-Write
    if [[ $phase == replication ]];then
        sha256sum -c "$run/readonly-before.sha256"
        startup=aros/engine/Startup-Replication-Resume
    fi
    if [[ $phase == resume ]];then
        # Verify RO evidence before a deliberately writable third boot.
        sha256sum -c "$run/readonly-before.sha256"
        startup=aros/engine/Startup-Initialize-Resume
    fi
    if [[ $phase == create ]];then
        startup=aros/engine/Startup-Admission-Create
        if [[ $create_service_mode != WRITE ]];then
            sed "s/OpenZFSAdmissionServiceHarness WRITE/OpenZFSAdmissionServiceHarness $create_service_mode/" \
                "$startup" >"$run/create-startup"
            startup="$run/create-startup"
        fi
    fi
    if [[ $phase == split ]];then
        sed 's/OpenZFSAdmissionServiceTest CREATE/OpenZFSAdmissionServiceTest SPLIT/' aros/engine/Startup-Admission-Create >"$run/split-startup"
        startup="$run/split-startup"
    fi
    if [[ $phase == add ]];then
        sed 's/OpenZFSAdmissionServiceTest CREATE/OpenZFSAdmissionServiceTest ADD/' aros/engine/Startup-Admission-Create >"$run/add-startup"
        startup="$run/add-startup"
    fi
    if [[ $phase == cancel ]];then
        sed -e 's/OpenZFSAdmissionServiceTest CREATE/OpenZFSAdmissionServiceTest CANCEL/' \
            -e 's/OpenZFSAdmissionServiceHarness WRITE/OpenZFSAdmissionServiceHarness WRITE-CANCEL/' \
            aros/engine/Startup-Admission-Create >"$run/cancel-startup"
        startup="$run/cancel-startup"
    fi
    if [[ $phase == verify ]]; then
        startup=aros/engine/Startup-Admission-Read
        sha256sum "$run/disk0.raw" "$run/disk1.raw" >"$run/readonly-before.sha256"
    fi
    if [[ ${AROS_TEST_SUITE:-admission} == initialize && $phase == write ]];then
        sed 's/OpenZFSAdmissionServiceHarness WRITE/OpenZFSAdmissionServiceHarness WRITE-INITIALIZE/' aros/engine/Startup-Admission-Write >"$run/initialize-startup"
        startup="$run/initialize-startup"
    fi
    if [[ ${AROS_TEST_SUITE:-admission} == initialize && $phase == verify ]];then
        sed 's/OpenZFSAdmissionServiceHarness VERIFY/OpenZFSAdmissionServiceHarness VERIFY-INITIALIZE/' aros/engine/Startup-Admission-Read >"$run/initialize-verify-startup"
        startup="$run/initialize-verify-startup"
    fi
    if [[ ${AROS_TEST_SUITE:-admission} == exec ]];then
        startup=aros/engine/Startup-Exec-Write
        if [[ $phase == verify ]];then startup=aros/engine/Startup-Exec-Read;fi
    fi
    if [[ ${AROS_TEST_SUITE:-admission} == device ]];then
        startup=aros/engine/Startup-Device-Write
        if [[ $phase == verify ]];then startup=aros/engine/Startup-Device-Read;fi
    fi
    xorriso -indev "$base" -outdev "$iso_dir/$phase.iso" \
        -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
        -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
        -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
        -map build-aros/hosted/Libs/z1.library /Libs/z1.library \
        -map aros/engine/security-fixture /Security \
        -map "$service" /C/OpenZFSAdmissionServiceHarness \
        -map "$client" /C/OpenZFSAdmissionServiceTest \
        "${extra_maps[@]}" \
        -map "$run/export.nvlist" /export.nvlist -map "$startup" /S/Startup-Sequence \
        -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on >"$run/pack-$phase.log" 2>&1
    sha256sum "$iso_dir/$phase.iso" >"$run/$phase-iso.sha256"
    throttle=
    if [[ ${AROS_TEST_SUITE:-admission} == initialize && $phase == write ]];then throttle=,bps_wr=8388608;fi
    member1=(-drive "file=$run/disk1.raw,format=raw,if=ide,index=1$throttle")
    if [[ ${AROS_TEST_SUITE:-admission} == device && $phase == verify ]];then
        member1=() # prove the repaired disk0 alone supplies the cold payload
    fi
    qemu-system-x86_64 -name "openzfs-admission-service-$phase-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
        -drive "file=$iso_dir/$phase.iso,media=cdrom,if=ide,index=2,readonly=on" \
        -drive "file=$run/disk0.raw,format=raw,if=ide,index=0$throttle" \
        "${member1[@]}" \
        -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
        -serial "file:$run/$phase-serial.log" -monitor "unix:$run/$phase.monitor,server=on,wait=off" >"$run/$phase-qemu.log" 2>&1 &
    duration=180
    if [[ $phase == create ]];then duration=600;fi # five creation/payload/export layouts
    if [[ ${AROS_TEST_SUITE:-admission} == device ]];then duration=600;fi
    if [[ ${AROS_TEST_SUITE:-admission} == initialize ]];then duration=600;fi
    pid=$!;deadline=$((SECONDS+duration));checked="$run/$phase-checked.log"
    while :; do
        if [[ -f "$run/$phase-serial.log" ]]; then
            # Security tracing can interrupt any RawPutChar marker, not only
            # task notifications. Keep raw serial intact and join around the
            # complete prefixed trace records in this marker-checking view.
            perl -0777 -pe 's/[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| \[security.library\] [^\r\n]*\r?\n//g' \
                "$run/$phase-serial.log" >"$checked"
            if rg -a -q '^RESULT .*FAIL|\[PF-DBG\]|^ASSERT at' "$checked" "$run/$phase-serial.log"; then
                echo "FAIL $phase: $run" >&2;exit 1
            fi
            expected='^RESULT standalone admission PASS'
            if [[ $phase == resume ]];then expected='^RESULT native initialization resume PASS';fi
            if [[ $phase == replication ]];then expected='^RESULT native replication resume PASS';fi
            if rg -a -q "$expected" "$checked"; then break;fi
        fi
        if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then echo "Timeout/exit $phase: $run" >&2;exit 1;fi
        sleep 1
    done
    if [[ $phase == replication ]];then
        rg -q '^Native send/receive cold token continuation completed errno=0' "$checked"
        printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
        wait "$pid";pid=
        continue
    fi
    if [[ $phase == resume ]];then
        rg -q '^Native upstream initialization progress/uninitialize write=1 result=0 stage=status' "$checked"
        [[ $(rg -c '^Initialize status .* state=COMPLETE .* progress=1000 .* errors=0 ' "$checked") == 2 ]]
        printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
        wait "$pid";pid=
        continue
    fi
    rg -q '^Native authenticated admission ready' "$checked"
    resilver_write=1
    if [[ $phase == verify ]];then resilver_write=0;fi
    rg -q "^Native resilver restart validation/no-work write=$resilver_write result=0" "$checked"
    if [[ ${AROS_TEST_SUITE:-admission} == initialize ||
        ( ${AROS_TEST_SUITE:-admission} == create && $create_service_mode != WRITE ) ]];then
        : # Focused service fixtures have no replication datasets.
    elif [[ $phase == verify ]];then
        rg -q '^Native send/receive VERIFY cold full\+incremental errno=0' "$checked"
        rg -q '^Native replication delegation write=0 result=0' "$checked"
    else
        rg -q '^Native send/receive WRITE full\+incremental errno=0' "$checked"
        rg -q '^Native replication delegation write=1 result=0' "$checked"
    fi
    if [[ ${AROS_TEST_SUITE:-admission} == initialize && $phase == write ]];then
        rg -q '^Native initialization active suspend/resume/cancel PASS' "$checked"
    fi
    if [[ ${AROS_TEST_SUITE:-admission} == initialize && $phase == verify ]];then
        rg -q '^Native initialization cold suspended state PASS' "$checked"
    fi
    if [[ $phase == split ]];then
        rg -q '^Native named/GUID vdev policy intersection result=0' "$checked"
        rg -q '^Native mirror split explicit leaves/export/import/independent DOS result=0' "$checked"
        printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
        wait "$pid";pid=
        sha256sum -c "$baseline/disks-before.sha256"
        echo "PASS native mirror split and independent DOS payload: $run"
        exit 0
    fi
    if [[ ${AROS_TEST_SUITE:-admission} == device ]];then
        mode=1;if [[ $phase == verify ]];then mode=0;fi
        rg -q "^Native degraded payload write=$mode result=0" "$checked"
        printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
        wait "$pid";pid=
        if [[ $phase == verify ]];then sha256sum -c "$run/readonly-before.sha256";fi
        continue
    fi
    if [[ ${AROS_TEST_SUITE:-admission} == exec ]];then
        mode=1;if [[ $phase == verify ]];then mode=0;fi
        rg -q "^Native exec policy write=$mode result=0" "$checked"
        rg -q "^Native DOS exec property write=$mode result=0" "$checked"
        printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
        wait "$pid";pid=
        if [[ $phase == verify ]];then sha256sum -c "$run/readonly-before.sha256";fi
        continue
    fi
    if [[ $phase == add || $phase == cancel ]];then
        rg -q '^Native pool add capacity/topology/preflight/reimport/DOS PASS' "$checked"
        rg -q '^Native pool add log/cache/spare and disabled allocation-class refusal PASS' "$checked"
        rg -q '^Native pool remove cache/spare/log, refusals and DOS payload PASS' "$checked"
        if [[ $phase == cancel ]];then
            rg -q '^Native pool remove cancellation, source retained and reimport DOS payload PASS' "$checked"
            rg -q 'test-only removal progress pause enabled' "$checked"
        else
            rg -q '^Native pool remove populated mirror, finished evacuation and reimport DOS payload PASS' "$checked"
        fi
        printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
        wait "$pid";pid=
        sha256sum -c "$baseline/disks-before.sha256"
        echo "PASS native pool expansion, reimport and DOS payload: $run"
        exit 0
    fi
    if [[ $phase == create ]];then
        for layout in 0 1 2 3 4;do
            disks=2;if ((layout>2));then disks=$layout;fi
            rg -q "^Native authenticated file pool layout=$layout disks=$disks result=0" "$checked"
        done
        printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
        wait "$pid";pid=
        sha256sum -c "$baseline/disks-before.sha256"
        echo "PASS: authenticated file-pool creation, DOS payload and refusal checks in disposable RAM files. Not cold-persistence evidence. Evidence: $run"
        exit 0
    fi
    rg -q '^Native authenticated pool export result=0' "$checked"
    if [[ ${AROS_TEST_SUITE:-admission} == admission ]];then
        rg -q '^Native locked stream file caller access result=0' "$checked"
        rg -q '^Native exclusive empty stream output result=0' "$checked"
        rg -q '^Native staged stream output creation result=0' "$checked"
        rg -q '^Native stream directory search authorization result=0' "$checked"
        rg -q '^Native stream namespace authorization result=0' "$checked"
        if [[ $phase == write ]];then
            [[ $(rg -c '^Native send/receive authorized native handle receive errno=0' "$checked") == 4 ]]
            [[ $(rg -c '^Native send/receive authorized native handle send errno=0' "$checked") == 2 ]]
        fi
    fi
    rg -q '^Native DOS multi-waiter timeout=0 result=0' "$checked"
    rg -q '^Native DOS multi-waiter timeout=1 result=0' "$checked"
    rg -q '^Native DOS close waiting record result=0' "$checked"
    rg -q '^Native DOS record locks result=0' "$checked"
    notify_write=1
    if [[ $phase == verify ]]; then notify_write=0; fi
    if [[ ${AROS_TEST_SUITE:-admission} == admission ]];then
        if [[ $phase == write ]];then
            rg -q '^Native stream representations advanced=0 compressed=0 large=0 embedded=0 errno=0' "$checked"
            rg -q '^Native stream representations advanced=1 compressed=[1-9][0-9]* large=[1-9][0-9]* embedded=[1-9][0-9]* errno=0' "$checked"
            rg -q '^Native send/receive WRITE advanced representations errno=0' "$checked"
            rg -q '^Native send/receive WRITE bookmark after snapshot deletion errno=0' "$checked"
            rg -q '^Native send/receive WRITE resumable receive errno=0' "$checked"
            rg -q '^Native send/receive WRITE incremental token resume errno=0' "$checked"
            rg -q '^Native encryption WRITE key lifecycle errno=0' "$checked"
            rg -q '^Native encryption replace key errno=0' "$checked"
            rg -q '^Native encryption old key observed errno=13' "$checked"
        else
            rg -q '^Native send/receive VERIFY cold advanced representations errno=0' "$checked"
            rg -q '^Native send/receive VERIFY cold bookmark after snapshot deletion errno=0' "$checked"
            rg -q '^Native send/receive VERIFY cold resumable receive errno=0' "$checked"
            rg -q '^Native send/receive VERIFY cold incremental token resume errno=0' "$checked"
            rg -q '^Native encryption VERIFY cold key lifecycle errno=0' "$checked"
            rg -q '^Native encryption cold old key observed errno=13' "$checked"
            rg -q '^Native encryption cold load errno=0' "$checked"
        fi
        rg -q "^Native authenticated receive API write=$notify_write result=0" "$checked"
        rg -q '^Native authenticated incremental receive content PASS' "$checked"
        rg -q '^Native own-volume full/incremental receive content PASS' "$checked"
        rg -q "^Native direct stream original-caller policy write=$notify_write result=0" "$checked"
        rg -q '^Native direct stream upstream ACL denial result=0' "$checked"
        rg -q '^Native detach DOS-list contention observed=16 expected=16 result=0' "$checked"
        rg -q '^Native mount DOS-entry contention observed=16 expected=16 result=0' "$checked"
        rg -q "^Native own-volume send roundtrip write=$notify_write result=0" "$checked"
        rg -q '^Native authenticated bookmark incremental content PASS' "$checked"
        rg -q '^Native complete receive resume token API PASS' "$checked"
        rg -q '^Native authenticated token continuation content PASS' "$checked"
        rg -q '^Native authenticated receive abort PASS' "$checked"
        rg -q '^Native authenticated key unload PASS' "$checked"
        rg -q '^Native authenticated key load PASS' "$checked"
        rg -q '^Native delegated and own-volume key load PASS' "$checked"
        rg -q '^Native authenticated key replacement PASS' "$checked"
        rg -q '^Native hexadecimal key lifecycle PASS' "$checked"
        rg -q '^Native passphrase key lifecycle PASS' "$checked"
        if [[ $phase == verify ]]; then
            rg -q '^Native encryption cold passphrase metadata errno=0' "$checked"
        fi
    fi
    rg -q "^Native authenticated scrub write=$notify_write result=0" "$checked"
    rg -q "^Native IO statistics write=$notify_write leaves=2 result=0" "$checked"
    rg -q "^Native DOS notifications write=$notify_write result=0" "$checked"
    rg -q '^Native DOS detached notification lifetime result=0' "$checked"
    rg -q "^Native authenticated user properties write=$notify_write result=0" "$checked"
    rg -q "^Native authenticated snapshot controls write=$notify_write result=0" "$checked"
    rg -q "^Native authenticated rollback write=$notify_write result=0" "$checked"
    rg -q "^Native authenticated promotion write=$notify_write result=0" "$checked"
    rg -q "^Native authenticated bookmarks write=$notify_write result=0" "$checked"
    rg -q "^Native authenticated space properties write=$notify_write result=0" "$checked"
    rg -q "^Native authenticated compression write=$notify_write result=0" "$checked"
    rg -q "^Native authenticated recordsize write=$notify_write result=0" "$checked"
    rg -q "^Native authenticated readonly write=$notify_write result=0" "$checked"
    rg -q "^Native authenticated timestamps write=$notify_write result=0" "$checked"
    rg -q "^Native timestamp callbacks write=$notify_write result=0" "$checked"
    rg -q "^Native exec policy write=$notify_write result=0" "$checked"
    rg -q "^Native device controls write=$notify_write result=0" "$checked"
    rg -q "^Native count limits write=$notify_write admin=0 result=0" "$checked"
    rg -q "^Native count limits write=$notify_write admin=1 result=0" "$checked"
    if [[ $phase == write ]];then rg -q '^Native count-limit disabled feature refusal PASS' "$checked";fi
    if [[ $phase == write ]];then rg -q '^Native concurrent engine count limits PASS rounds=16' "$checked";fi
    if [[ $phase == write ]];then rg -q '^Native compression disabled feature refusal PASS' "$checked";fi
    rg -q "^Native DOS quota enforcement write=$notify_write full=$((notify_write*3)) result=0" "$checked"
    rg -q '^Service remains resident: mounted or retired DOS endpoints still require servicing.' "$checked"
    if rg -q '^OpenZFS service engine shutdown complete' "$checked";then
        echo 'FAIL: service shut down with retired native endpoints' >&2;exit 1
    fi
    printf 'info blockstats\nscreendump %s/%s.ppm\n' "$run" "$phase" |
        socat - "UNIX-CONNECT:$run/$phase.monitor" >"$run/$phase-monitor.txt"
    if [[ $phase == verify ]];then
        [[ $(rg -c '^ide0-hd[01]: .*wr_bytes=0 ' "$run/$phase-monitor.txt") == 2 ]]
    else
        [[ $(rg -c '^ide0-hd[01]: .*flush_operations=[1-9][0-9]* ' "$run/$phase-monitor.txt") == 2 ]]
    fi
    printf 'quit\n' | socat - "UNIX-CONNECT:$run/$phase.monitor" >/dev/null
    wait "$pid";pid=
done
sha256sum -c "$baseline/disks-before.sha256"
# Initialization and admission have an intentional writable third boot.
# Their read-only hashes are checked at the start of that boot, before writes.
if [[ ${AROS_TEST_SUITE:-admission} != initialize && ${AROS_TEST_SUITE:-admission} != admission ]];then
    sha256sum -c "$run/readonly-before.sha256"
fi
partitioned=0
if [[ -f $baseline/partition0-layout.log ]];then partitioned=1;fi
bash aros/engine/verify-admission-offline.sh "$run" "$partitioned"
if [[ ${AROS_TEST_SUITE:-admission} == device ]];then
    echo "PASS: degraded native DOS writes, reported resilver completion and cold payload verification. Evidence: $run"
    exit 0
fi
if [[ ${AROS_TEST_SUITE:-admission} == exec ]];then
    echo "PASS: native exec callbacks, authenticated DOS protection and cold persistence. Evidence: $run"
    exit 0
fi
echo "PASS: separate service/client, authenticated jobs, mounted protection, cold persistence, safe resident/shutdown lifecycle. Evidence: $run"
