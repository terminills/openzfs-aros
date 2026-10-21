#!/usr/bin/env bash
# Own cloned runtime and X server only. Never attach to an existing session.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(mktemp -d "$repo/build-aros/hosted-offset.XXXXXXXX")
echo "Evidence: $run"
for dir in boot C Classes Devs Fonts L Libs Locale Prefs S System;do
    cp -a --reflink=auto "build-aros/hosted/$dir" "$run/$dir"
done
cp build-aros/hosted/AROS.boot "$run/AROS.boot"
# Selected candidates replace only this new private runtime's copies. Never
# update build-aros/hosted or a user's running session to conduct a comparison.
if [[ -n ${AROS_TEST_DOS64_LIBRARY:-} ]];then
    cp -- "$(realpath -e "$AROS_TEST_DOS64_LIBRARY")" "$run/Libs/dos64.library"
fi
if [[ -n ${AROS_TEST_POSIX_LIBRARY:-} ]];then
    cp -- "$(realpath -e "$AROS_TEST_POSIX_LIBRARY")" "$run/Libs/posixc.library"
fi
cp "$0" "$run/test-hosted-offset.sh"
cp build-aros/OpenZFSDos64OffsetTest "$run/C/"
cp "${AROS_TEST_POSIX_PROBE:-build-aros/OpenZFSPosixIOTest}" "$run/C/OpenZFSPosixIOTest"
cp aros/engine/Startup-Offset "$run/S/Startup-Sequence"
sed "s|^logfile .*|logfile $run/serial.log|" aros/engine/hosted.conf >"$run/hosted.conf"
git rev-parse HEAD >"$run/source.txt"
cp aros/engine/dos64-offset-test.c "$run/probe.c"
cp aros/engine/posix-io-test.c "$run/posix-probe.c"
sha256sum "$run/C/OpenZFSDos64OffsetTest" "$run/C/OpenZFSPosixIOTest" "$run/Libs/posixc.library" \
    "$run/Libs/dos64.library" "$run/Libs/dos.library" \
    "$run/boot/linux/L/emul-handler" >"$run/inputs.sha256"
xpid=;guest=
cleanup() {
    if [[ -n $guest ]];then kill -TERM -- "-$guest" 2>/dev/null || true;wait "$guest" || true;fi
    if [[ -n $xpid ]];then kill "$xpid" 2>/dev/null || true;wait "$xpid" || true;fi
}
trap cleanup EXIT
Xvfb -displayfd 3 -screen 0 1280x960x24 -nolisten tcp 3>"$run/display.txt" >"$run/xvfb.log" 2>&1 &
xpid=$!
for attempt in {1..50};do [[ -s "$run/display.txt" ]] && break;sleep .1;done
[[ -s "$run/display.txt" ]]
display=$(<"$run/display.txt")
[[ $display =~ ^[0-9]+$ ]]
cd "$run"
DISPLAY=":$display" setsid ./boot/linux/AROSBootstrap -c "$run/hosted.conf" >"$run/bootstrap.log" 2>&1 &
guest=$!
printf '%s\n' "$guest" >"$run/guest.pid"
deadline=$((SECONDS+120))
while [[ ! -f "$run/offset-finished.log" ]];do
    if ! kill -0 "$guest" 2>/dev/null || ((SECONDS>=deadline));then
        echo 'FAIL: private offset test exit/deadline' >&2;exit 1
    fi
    sleep 1
done
cat "$run/dos64-offset.log"
cat "$run/posix-io-test.log"
rg -q '^RESULT DOS64 offset failures=0' "$run/dos64-offset.log"
rg -q '^RESULT raw POSIX failures=0' "$run/posix-io-test.log"
echo "PASS: hosted DOS64 offset probe: $run"
