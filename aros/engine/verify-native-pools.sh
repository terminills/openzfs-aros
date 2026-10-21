#!/usr/bin/env bash
# Read-only upstream Linux verification of completed native test pools.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
[[ $(uname -s) == Linux ]]
run=$(mktemp -d "$repo/build-aros/linux-verify.XXXXXXXX")
git -C "$repo" rev-parse HEAD > "$run/source.txt"
git -C "$repo" diff --binary > "$run/source.patch"
sha256sum "$repo/build-aros/hosted/C/OpenZFSPoolTest" > "$run/binary.sha256"
for topology in mirror raidz1 raidz2 raidz3; do
    log="$repo/build-aros/hosted/pool-$topology.log"
    [[ $topology != mirror ]] || log="$repo/build-aros/hosted/pool-test.log"
    rg -q "^RESULT native $topology create/open/export PASS$" "$log"
    rg -q "^RESULT native $topology data roundtrip PASS$" "$log"
    dir=$(sed -n 's/^Disposable native pool directory: //p' "$log")
    pool=$(sed -n "s/^Creating upstream $topology //p" "$log")
    [[ $dir =~ ^/SYS/OpenZFS-test-[0-9a-f]{8}$ ]]
    [[ $pool =~ ^aros_${topology}_[0-9a-f]{8}$ ]]
    cp "$log" "$run/$topology-native.log"
    cp "$repo/build-aros/hosted/pool-$topology-errors.log" "$run/$topology-errors.log"
    [[ ! -s "$run/$topology-errors.log" ]]
    LD_LIBRARY_PATH="$repo/build-control/.libs:$repo/build-control/deps/usr/lib/x86_64-linux-gnu" \
        "$repo/build-control/zdb" -e -p "$repo/build-aros/hosted/${dir#/SYS/}" -bcc "$pool" \
        > "$run/$topology-zdb.log" 2>&1
    rg -q 'No leaks' "$run/$topology-zdb.log"
    printf '%s native create/open/export + Linux zdb PASS\n' "$topology" | tee -a "$run/results.txt"
done
echo "Evidence: $run"
