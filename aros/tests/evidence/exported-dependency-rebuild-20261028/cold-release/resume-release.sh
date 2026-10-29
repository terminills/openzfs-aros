#!/usr/bin/env bash
set -euo pipefail
run=/run/media/terminills/Amiwest/openzfs-source-replay-20261028/build/public-service-run.IiTq2LX2
[[ ${AROS_TEST_OWNED_QEMU_SLOT:-} == 'qemu boot slot' ]]
[[ ! -e $run/release-serial.log ]]
[[ $(findmnt -n -o UUID --target "$run") == 05930f9c-5f05-476d-81eb-205389db82ad ]]
if pgrep -f '^qemu-system-.*-name openzfs-|^/usr/bin/qemu-system-.*-name openzfs-' >/dev/null;then exit 1;fi
python3 /mnt/short-term/openzfs-bootenv-candidate-20261025/aros/engine/check-qemu-sockets.py "$run"
qemu-system-x86_64 -name openzfs-release-resume -machine pc,accel=kvm -cpu host -smp 8 -m 2048 \
 -drive "file=$run/release.iso,media=cdrom,if=ide,index=2,readonly=on" \
 -drive "file=$run/backing.raw,format=raw,if=ide,index=0" \
 -drive "file=$run/startup-archive.raw,format=raw,if=ide,index=1" \
 -display none -nic none -boot d -no-reboot -no-shutdown \
 -serial "file:$run/release-serial.log" \
 -monitor "unix:$run/r-monitor,server=on,wait=off" >"$run/release-resume-qemu.log" 2>&1 &
pid=$!
cleanup() { kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; }
trap cleanup EXIT
deadline=$((SECONDS+600))
while :;do
 if [[ -f $run/release-serial.log ]];then
  sed -E 's/^[[:xdigit:]]{16} 0x[[:xdigit:]]{16} \| [0-9]+ \| //' "$run/release-serial.log" >"$run/release-results.log"
  if rg -q '^RESULT .*FAIL|Public service test line=[1-9][0-9]*|\[PF-DBG\]|^ASSERT at' "$run/release-results.log";then exit 1;fi
  if rg -q '^RESULT public service storage PASS' "$run/release-results.log";then break;fi
 fi
 kill -0 "$pid" 2>/dev/null && ((SECONDS < deadline)) || { echo 'Release timeout/exit';exit 1; }
 sleep 1
done
printf 'screendump %s/release-resume.ppm\nquit\n' "$run" | socat - "UNIX-CONNECT:$run/r-monitor" >"$run/release-resume-monitor.log"
wait "$pid"
trap - EXIT
rg -q '^Public cold writable release PASS pending-before=1 recursive-release=1 parent-absent=1 child-absent=1 reimport-absent=1 exact-data=1 exported=1' "$run/release-results.log"
rg -q '^Public backing clean detach PASS' "$run/release-results.log"
fsck.fat -n -v "$run/backing.raw" >"$run/fat-after-release.log" 2>&1
sha256sum -c "$run/archive-before.sha256" >"$run/archive-after-release.log"
mkdir "$run/members-release"
for member in 0 1;do mcopy -i "$run/backing.raw" "::OZPublicPool/disk$member" "$run/members-release/disk$member";done
sha256sum "$run/members-release/"* >"$run/members-release.sha256"
zdb=/home/terminills/Documents/GitHub/openzfs-aros/build-control/zdb
"$zdb" -e -p "$run/members-release" -bcc public_mirror >"$run/release-zdb-check.log" 2>&1
rg -q 'No leaks' "$run/release-zdb-check.log"
"$zdb" -e -p "$run/members-release" -dddd public_mirror >"$run/release-zdb-objects.log" 2>&1
if rg -q 'MOS object .* leaked|refcount mismatch' "$run/release-zdb-objects.log";then exit 1;fi
sha256sum -c "$run/members-release.sha256" >"$run/release-zdb-unchanged.log"
echo 'PASS resumed cold release, persisted deletion and independent zdb; original launch failure retained'
