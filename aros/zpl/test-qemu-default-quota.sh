#!/usr/bin/env bash
# No writable host disks, NIC, shared hosted session or DUT.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
binary=${1:?quota test executable required}
base=${AROS_TEST_ISO:-/opt/aros-workspace/aros-amiwest-nightly-release/distfiles/aros-smp-pc-x86_64.iso}
dos=${AROS_TEST_DOS_OVERLAY:?private DOS overlay required}
ata=${AROS_TEST_ATA_OVERLAY:?private ATA overlay required}
posix=${AROS_TEST_POSIX_LIBRARY:?private POSIX library required}
zlib=${AROS_TEST_ZLIB_LIBRARY:-$repo/build-aros/hosted/Libs/z1.library}
[[ -f $zlib ]]
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo" "${AROS_TEST_ARTIFACT_ROOT:-}")
if [[ -z ${AROS_TEST_ARTIFACT_ROOT:-} ]];then
    artifact_root="$repo/build-aros"
    mkdir -p "$artifact_root"
fi
run=$(mktemp -d "$artifact_root/default-quota-run.XXXXXXXX")
echo "Evidence: $run"
exec > >(tee "$run/runner.log") 2>&1
cp "$binary" "$run/OpenZFSDefaultQuotaTest"
cp aros/zpl/Startup-Default-Quota "$run/Startup-Sequence"
cp "$0" "$run/"
git rev-parse HEAD > "$run/source-head.txt"
git diff --binary > "$run/source.patch"
tar -czf "$run/aros-source.tar.gz" aros/engine aros/zpl aros/cli \
    module/zfs/spa.c module/zfs/dmu_send.c include/sys/zfs_context.h
sha256sum "$base" "$run/OpenZFSDefaultQuotaTest" "$dos/aros-base.pkg.xz" \
    "$ata/aros-bsp.pkg.xz" "$posix" "$zlib" > "$run/inputs.sha256"
xorriso -indev "$base" -outdev "$run/test.iso" \
    -map aros/engine/grub-qemu-test.cfg /boot/grub/grub.cfg \
    -map "$dos/aros-base.pkg.xz" /boot/aros-base.pkg.xz \
    -map "$ata/aros-bsp.pkg.xz" /boot/pc-smp/aros-bsp.pkg.xz \
    -map "$posix" /Libs/posixc.library \
    -map aros/engine/security-fixture /Security \
    -map "$zlib" /Libs/z1.library \
    -map "$run/OpenZFSDefaultQuotaTest" /C/OpenZFSDefaultQuotaTest \
    -map aros/engine/public-service-test-only /openzfs-quota-test-only \
    -map "$run/Startup-Sequence" /S/Startup-Sequence \
    -boot_image any discard -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
    -boot_image grub boot_info_table=on -boot_image grub grub2_boot_info=on > "$run/pack.log" 2>&1
qemu-system-x86_64 -name "openzfs-quota-$$" -machine pc,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive "file=$run/test.iso,media=cdrom,if=ide,index=2,readonly=on" \
    -vga std -display none -nic none -boot d -no-reboot -no-shutdown \
    -serial "file:$run/serial.log" -monitor "unix:$run/monitor,server=on,wait=off" > "$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null; then kill "$pid"; wait "$pid" || true; fi; }
trap cleanup EXIT
deadline=$((SECONDS+180))
while :; do
    if [[ -f "$run/serial.log" ]]; then
        perl -0777 -pe 's/[0-9a-f]{16} 0x[0-9a-f]+ \| [0-9]+ \| \[security.library\] secTaskNotify: [^\r\n]*\r?\n//g' "$run/serial.log" >"$run/serial-completion.log"
        if rg -q 'FAIL quota|RESULT native default quota FAIL|\[PF-DBG\]|ASSERT at|went out of stack limits|signal=6 default action=terminate' "$run/serial.log" "$run/serial-completion.log"; then exit 1; fi
        if rg -q '^RESULT native default quota PASS properties=6 phases=4' "$run/serial-completion.log" &&
           rg -q '^RESULT native dispatcher credentials PASS' "$run/serial-completion.log" &&
           rg -q '^RESULT native platform primitives PASS hlist=1 counters=320000 threads=8' "$run/serial-completion.log" &&
           rg -q '^RESULT native file scopes PASS threads=8 exit-cleanup=1 retained-read=2 final-close=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native session files PASS rounds=16 capacity=64 stale=1 retained=1 threads=16' "$run/serial-completion.log" &&
           { [[ ${AROS_TEST_REQUIRE_ZVOL:-0} != 1 ]] ||
             { rg -q '^RESULT native upstream create PASS properties=3 inherited=3 lookup=2' "$run/serial-completion.log" &&
               rg -q '^RESULT native legacy accounting PASS old-pool=1 mounted-conversion=1 user-bytes=1 data-preserved=1' "$run/serial-completion.log" &&
               rg -q '^RESULT native legacy accounting PASS old-pool=1 unmounted-conversion=1 user-bytes=1 data-preserved=1' "$run/serial-completion.log" &&
               rg -q '^RESULT native zvol PASS write=1 reopen=1 readonly=1 resize=1 export-busy=1' "$run/serial-completion.log" &&
               rg -q '^RESULT native zvol lifecycle PASS rename=1 snapshot-readonly=1 recreate=1' "$run/serial-completion.log" &&
               rg -q '^RESULT native dispatcher runtime PASS cycles=80 upstream-stats=1 stopped-denial=1' "$run/serial-completion.log"; }; } &&
           rg -q '^RESULT native UTF8 mount PASS create=1 rename=1 rejected=5 reopen=1' "$run/serial-completion.log" &&
           [[ $(rg -c '^RESULT native normalized mount PASS equivalent=1 duplicate=1 case-sensitive=1 rename=1 reopen=1' "$run/serial-completion.log") == 4 ]] &&
           [[ $(rg -c '^RESULT native case mount PASS lookup=1 duplicate-policy=1 rename=1 reopen=1' "$run/serial-completion.log") == 4 ]] &&
           [[ $(rg -c '^RESULT native DOS alias rename PASS lock=1 handle=1 hardlink=1 enumeration=3 unicode=1' "$run/serial-completion.log") == 9 ]] &&
           rg -q '^RESULT native creation properties PASS cases=3 root-normalization=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native ZPL accounting PASS persisted-user=1 persisted-group=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native caller scopes PASS threads=16' "$run/serial-completion.log" &&
           rg -q '^RESULT native mount ownership PASS phases=4 snapshot=1 rejected=2' "$run/serial-completion.log" &&
           rg -q '^RESULT native filesystem deleteq wait PASS pending=1 drained=1 waited=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native filesystem deleteq cancel PASS pending=1 interrupted=1 republished=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native filesystem deleteq suspend PASS pending=1 interrupted=1 resumed=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native node resume PASS identity=3 refs=2' "$run/serial-completion.log" &&
           rg -q '^RESULT native mounted rollback PASS rounds=2 failed-resume=1 owner-task=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native mounted end PASS stale-io=1 cleanup=1 disown=1 reopen=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native DOS identity PASS cases=3 stale-parent=1 persistent=1 cleanup=1 reopen=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native send caller PASS complete=1 failed=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native snapshot unmount PASS busy=1 detached=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native temporary holds PASS cycles=16' "$run/serial-completion.log" &&
           rg -q '^RESULT native legacy versions PASS versions=1-4 write=1 remount=1 unchanged-format=1' "$run/serial-completion.log" &&
           rg -q '^RESULT native filesystem version PASS legacy=4 upgraded=5' "$run/serial-completion.log"; then break; fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline)); then echo "Timeout/exit: $run"; exit 1; fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
echo "PASS: default quota persistence. Evidence: $run"
