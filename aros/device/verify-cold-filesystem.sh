#!/usr/bin/env bash
# Read-only verification of a terminal cold guest's disposable GPT images.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(realpath -e "${1:?completed native cold run required}")
[[ $(dirname -- "$run") == "$repo/build-aros" && $(basename -- "$run") == native-import-run.* ]]
rg -q '^PASS: native cold readonly verification of prior DOS write:' "$run/runner.log"
rg -q 'RESULT native cold filesystem on zvol PASS exact-read=1 write-denied=1 shutdown=1' "$run/serial.log"
rg -q 'RESULT native cold zvol device PASS readonly=1 eject=1' "$run/serial.log"
sha256sum -c "$run/backing.sha256"
sha256sum -c "$run/overlays.sha256"
out=$(mktemp -d "$run/zvol-integrity.XXXXXXXX")
exec > >(tee "$out/verify.log") 2>&1
echo "Evidence: $out"
for member in 0 1;do
    # qemu-img refuses an active writer; never bypass its image locking.
    qemu-img convert -f qcow2 -O raw "$run/disk$member.qcow2" "$out/disk$member.raw"
    sgdisk --verify --info=1 "$out/disk$member.raw" >"$out/gpt$member.log" 2>&1
    rg -q 'No problems found' "$out/gpt$member.log"
    # pool-partition-fixture.py uses this exact non-aligned guarded extent.
    rg -q '^First sector: 4097 ' "$out/gpt$member.log"
    rg -q '^Last sector: 528384 ' "$out/gpt$member.log"
    python3 aros/engine/pool-partition-fixture.py extract "$out/disk$member.raw" "$out/member$member.raw"
done
sha256sum "$out/member0.raw" "$out/member1.raw" >"$out/members.sha256"
export LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu"
failed=0
build-control/zdb -e -p "$out/member0.raw" -p "$out/member1.raw" -bcc qemu_mirror >"$out/zdb.log" 2>&1 || failed=1
# zdb may exit zero with allocation discrepancies; require the explicit gate.
if ! rg -q 'No leaks' "$out/zdb.log";then
    echo "FAIL: pool integrity; inspect $out/zdb.log"
    failed=1
fi
build-control/zdb -e -p "$out/member0.raw" -p "$out/member1.raw" -dddd qemu_mirror/device-volume >"$out/zvol.log" 2>&1 || failed=1
if ! rg -q '^Dataset qemu_mirror/device-volume ' "$out/zvol.log";then
    echo "FAIL: retained zvol inspection; inspect $out/zvol.log"
    failed=1
fi
# Verify non-mutation even when the diagnostic above finds a defect.
sha256sum -c "$out/members.sha256"
sha256sum -c "$run/backing.sha256"
sha256sum -c "$run/overlays.sha256"
[[ $failed == 0 ]]
echo 'PASS: cold filesystem-on-zvol guest bytes, readonly refusal, GPT extents and independent upstream zdb integrity'
