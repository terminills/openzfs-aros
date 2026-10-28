#!/usr/bin/env bash
# Private hosted copy and X server. No user session, network or device I/O.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
: "${AROS_CC:?}" "${AROS_SDK:?}" "${AROS_ARTIFACT_ROOT:?}"
: "${AROS_TEST_HOSTED_RUNTIME:?}" "${AROS_TEST_ZDB_BUILD:?}"
: "${AROS_TEST_ZDB_MEMBER:?}" "${AROS_TEST_HOST_ZDB:?}" "${AROS_TEST_ZDB_L2ARC_LIBNVPAIR:?}"
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "$AROS_ARTIFACT_ROOT")
runtime=$(realpath -e "$AROS_TEST_HOSTED_RUNTIME")
build=$(realpath -e "$AROS_TEST_ZDB_BUILD")
for binary in "$AROS_TEST_HOST_ZDB" "$build/stage/Libs/openzfs.library";do
    strings "$binary" | grep -F 'Invalid L2ARC log entry count:' >/dev/null
done
run=$(mktemp -d "$root/hosted-zdb.XXXXXXXX")
echo "Evidence: $run"
cp "$0" "$run/runner.sh"
for directory in boot C Classes Devs Fonts L Libs Locale Prefs S System;do
    # Dereference copied links so subsequent private edits cannot touch source.
    cp -aL --reflink=auto "$runtime/$directory" "$run/$directory"
done
cp "$runtime/AROS.boot" "$run/AROS.boot"
for spec in 'AROS_TEST_STDC_LIBRARY:stdc.library' 'AROS_TEST_STDCIO_LIBRARY:stdcio.library' 'AROS_TEST_DOS64_LIBRARY:dos64.library' 'AROS_TEST_POSIX_LIBRARY:posixc.library' 'AROS_TEST_DOS_LIBRARY:dos.library';do
    variable=${spec%%:*};name=${spec#*:}
    if [[ -n ${!variable:-} ]];then
        cp "$(realpath -e "${!variable}")" "$run/Libs/$name"
    fi
done
if [[ -e $run/Libs/security.library ]];then unlink "$run/Libs/security.library";fi
cp "$build/stage/Libs/openzfs.library" "$run/Libs/"
cp "$build/stage/C/zdb" "$run/C/ZdbAudit"
cp --reflink=auto "$AROS_TEST_ZDB_MEMBER" "$run/zdb-member"
python3 aros/cli/make-zdb-l2arc.py "$AROS_TEST_ZDB_L2ARC_LIBNVPAIR" "$run/zdb-l2arc" --populated --bounds
flags=(--sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -O2 -Wall -Wextra -Werror
       -I"$build/gen/openzfs/aros/library/openzfs/include")
"$AROS_CC" "${flags[@]}" -DOZ_ZDB_HOSTED -DOZ_ZDB_SHARED -DOZ_ZDB_DEVICE \
    -DOZ_ZDB_MISSING_SECURITY -DOZ_ZDB_L2ARC -DOZ_ZDB_L2ARC_LOGS -DOZ_ZDB_L2ARC_BOUNDS \
    aros/cli/zdb-label-native-test.c -o "$run/C/OpenZFSZdbLabelsTest" >"$run/build.log" 2>&1
"$AROS_CC" "${flags[@]}" -DOZ_ZDB_MISSING_SECURITY aros/cli/zdb-unprivileged-test.c \
    -o "$run/C/OpenZFSZdbUserTest" >>"$run/build.log" 2>&1
"$AROS_CC" "${flags[@]}" aros/library/zdb-lifetime-test.c \
    -o "$run/C/OpenZFSZdbLifetimeTest" >>"$run/build.log" 2>&1
"$AROS_TEST_HOST_ZDB" -l "$run/zdb-member" >"$run/host-labels.log" 2>&1
for name in little big missing log-little log-big log-bad count-little count-big;do
    verbosity=-l;[[ $name != log-* && $name != count-* ]] || verbosity=-lll
    expected=0;[[ $name != log-bad && $name != count-* ]] || expected=1
    status=0
    "$AROS_TEST_HOST_ZDB" "$verbosity" "$run/zdb-l2arc/$name" >"$run/host-l2arc-$name.log" 2>&1 || status=$?
    [[ $status == "$expected" ]]
done
printf '%s\n' hosted_runtime=1 missing_security=1 l2arc_log_iso=1 l2arc_bounds_iso=1 >"$run/mode.txt"
sha256sum "$run/zdb-member" "$run/zdb-l2arc/"{little,big,missing,log-*,count-*} \
    "$run/Libs/openzfs.library" "$run/Libs/posixc.library" "$run/Libs/dos.library" \
    "$run/Libs/stdc.library" "$run/Libs/stdcio.library" "$run/Libs/dos64.library" \
    "$run/boot/linux/kernel" "$run/C/"OpenZFSZdb* "$run/C/ZdbAudit" >"$run/inputs.sha256"
sed "s|^logfile .*|logfile $run/serial.log|" aros/engine/hosted.conf >"$run/hosted.conf"
printf '%s\n' 'FailAt 21' 'MakeDir RAM:T RAM:ENV' 'Assign T: RAM:T' \
    'Assign ENV: RAM:ENV' 'Assign LIBS: SYS:Classes ADD' 'Stack 1048576' \
    'C:OpenZFSZdbLabelsTest >SYS:client.log' 'Echo finished >SYS:finished.log' \
    'EndCLI' >"$run/S/Startup-Sequence"
xpid=;guest=
cleanup(){
    if [[ -n $guest ]];then kill -TERM -- "-$guest" 2>/dev/null || true;wait "$guest" || true;fi
    if [[ -n $xpid ]];then kill "$xpid" 2>/dev/null || true;wait "$xpid" || true;fi
}
trap cleanup EXIT
Xvfb -displayfd 3 -screen 0 1280x960x24 -nolisten tcp 3>"$run/display.txt" >"$run/xvfb.log" 2>&1 &
xpid=$!
for attempt in {1..50};do [[ -s $run/display.txt ]] && break;sleep .1;done
display=$(<"$run/display.txt");[[ $display =~ ^[0-9]+$ ]]
cd "$run"
DISPLAY=":$display" setsid ./boot/linux/AROSBootstrap -c "$run/hosted.conf" >"$run/bootstrap.log" 2>&1 &
guest=$!;printf '%s\n' "$guest" >"$run/guest.pid"
deadline=$((SECONDS+180))
while [[ ! -f $run/finished.log ]];do
    if ! kill -0 "$guest" 2>/dev/null || ((SECONDS>=deadline));then
        echo 'FAIL bounded private hosted debugger run: exit/deadline' >&2;exit 1
    fi
    sleep 1
done
python3 "$repo/aros/cli/verify-zdb-labels.py" "$run/serial.log" "$run/host-labels.log" --hosted >"$run/labels-comparison.log"
for cycle in 0 1;do
    grep -Fx "RESULT shared zdb lifecycle PASS cycle=$cycle invalid=5 repeated=10 service=10" "$run/serial.log" >/dev/null
done
grep -Fx 'RESULT shared zdb caller exit PASS status=7' "$run/serial.log" >/dev/null
python3 "$repo/aros/cli/verify-zdb-device.py" "$run" >"$run/denial-comparison.log"
python3 "$repo/aros/cli/verify-zdb-l2arc.py" "$run" >"$run/l2arc-comparison.log"
sha256sum -c "$run/inputs.sha256" >"$run/unchanged.log"
echo 'PASS private hosted labels, lifecycle, absent-security denial and L2ARC; no physical/native ATA test.'
