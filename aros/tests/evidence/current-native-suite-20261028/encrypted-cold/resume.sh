#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
run="$PWD/build-audit/public-service-run.XMxcRVST"
[[ ! -e $run/native-cold-serial.log ]]
if pgrep -f '^qemu-system-|^/usr/bin/qemu-system-' >/dev/null;then exit 1;fi
sha256sum -c "$run/native-before-cold.sha256" >"$run/cold-resume-input-check.log"
qemu-system-x86_64 -name openzfs-encrypted-cold-resume -machine pc,accel=kvm -cpu host \
 -smp 8 -m 2048 -drive "file=$run/native-cold.iso,media=cdrom,if=ide,index=2,readonly=on" \
 -drive "file=$run/partition.raw,format=raw,if=ide,index=0" \
 -display none -nic none -boot d -no-reboot -no-shutdown \
 -serial "file:$run/native-cold-serial.log" -monitor "unix:$run/n-monitor,server=on,wait=off" \
 >"$run/native-cold-resume-qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+300))
while :;do
 if [[ -f $run/native-cold-serial.log ]];then
  sed -E 's/^[[:xdigit:]]{16} 0x[[:xdigit:]]{16} \| [0-9]+ \| //' "$run/native-cold-serial.log" >"$run/native-cold-results.log"
  if rg -q '^RESULT .*FAIL|Public service test line=[1-9][0-9]*|\[PF-DBG\]|^ASSERT at' "$run/native-cold-results.log";then exit 1;fi
  if rg -q '^RESULT public service storage PASS' "$run/native-cold-results.log" &&
   rg -q '^Public native cold reader PASS locked=1 mount-denied=1 key-loaded=1 exact-read=1 readonly-write-denied=1 exported=1' "$run/native-cold-results.log";then break;fi
 fi
 if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then exit 1;fi
 sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/n-monitor" >/dev/null
wait "$pid"
trap - EXIT
sha256sum -c "$run/native-before-cold.sha256" >"$run/native-after-cold.log"
echo 'PASS resumed native encrypted cold reader; pool image unchanged'
