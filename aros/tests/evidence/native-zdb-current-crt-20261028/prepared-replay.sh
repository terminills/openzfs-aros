#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
run="$PWD/build-audit/zdb-label-run.LuO2nC45"
member="$PWD/build-audit/native-import-run.NI5sF03m/topology-members/member0"
test -f "$run/prepared-only.txt"
test ! -e "$run/serial.log"
sha256sum -c "$run/prepared-iso.sha256" >"$run/preboot-iso-check.log"
sha256sum -c "$run/inputs.sha256" >"$run/preboot-input-check.log"
qemu-img compare -f qcow2 -F raw "$run/device.qcow2" "$member" >"$run/preboot-device-check.log"
if pgrep -f '^qemu-system-|^/usr/bin/qemu-system-' >/dev/null;then
    echo 'Another QEMU exists; refusing concurrent launch' >&2;exit 1
fi
qemu-system-x86_64 -name openzfs-missing-security-prepared -machine pc,accel=kvm \
    -cpu host -smp 8 -m 2048 -cdrom "$run/test.iso" \
    -drive "file=$run/device.qcow2,format=qcow2,if=ide,index=0" -boot d -nic none \
    -display none -serial "file:$run/serial.log" \
    -monitor "unix:$run/monitor,server=on,wait=off" -no-reboot -no-shutdown >"$run/qemu.log" 2>&1 &
pid=$!
cleanup(){ if kill -0 "$pid" 2>/dev/null;then kill "$pid";wait "$pid" || true;fi; }
trap cleanup EXIT
deadline=$((SECONDS+240))
while :;do
    if [[ -f $run/serial.log ]];then
        if rg -q 'RESULT native zdb labels FAIL|\[PF-DBG\]|ASSERT at|went out of stack limits' "$run/serial.log";then exit 1;fi
        if rg -q '(^| \| )RESULT native zdb labels PASS runs=2 storage=readonly-iso\r?$' "$run/serial.log";then break;fi
    fi
    if ! kill -0 "$pid" 2>/dev/null || ((SECONDS>=deadline));then exit 1;fi
    sleep 1
done
printf 'quit\n' | socat - "UNIX-CONNECT:$run/monitor" >/dev/null
wait "$pid"
trap - EXIT
sha256sum -c "$run/inputs.sha256" >"$run/unchanged-after-native.log"
python3 aros/cli/verify-zdb-labels.py "$run/serial.log" "$run/host-labels.log" >"$run/comparison.log"
for cycle in 0 1;do
    rg -q "RESULT shared zdb lifecycle PASS cycle=$cycle invalid=5 repeated=10 service=10" "$run/serial.log"
done
rg -q 'RESULT shared zdb caller exit PASS status=7' "$run/serial.log"
python3 aros/cli/verify-zdb-bad-labels.py "$run" >"$run/damaged-comparison.log"
python3 aros/cli/verify-zdb-device.py "$run" >"$run/device-comparison.log"
python3 aros/cli/verify-zdb-l2arc.py "$run" >"$run/l2arc-comparison.log"
qemu-img compare -f qcow2 -F raw "$run/device.qcow2" "$member" >"$run/device-unchanged.log"
printf 'PASS native missing-security denial with unchanged device\n' | tee "$run/native-complete.log"
