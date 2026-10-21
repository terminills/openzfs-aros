#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
export AROS_ZPL_BUILD=1
export AROS_ENGINE_OBJECT_DIR="$repo/build-aros/zpl-objects"
run=$(mktemp -d "$repo/build-aros/zpl-compile.XXXXXXXX")
echo "ZPL compile evidence: $run"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
tar -czf "$run/native-interfaces.tar.gz" aros/zpl aros/engine/include
pass=0;fail=0
for source in module/zfs/zfs_vnops.c module/zfs/zfs_log.c module/zfs/zfs_quota.c \
    module/zfs/sa.c module/zfs/zfs_sa.c module/zfs/zfs_znode.c module/zfs/zfs_fuid.c module/zfs/zfs_replay.c; do
    log="$run/${source//\//_}.log"
    if bash aros/engine/compile-object.sh "$source" >"$log" 2>&1; then
        echo "PASS $source";pass=$((pass+1))
    else
        echo "FAIL $source";fail=$((fail+1))
        rg -m 2 'error:' "$log" || true
    fi
done
echo "ZPL COMPILE ONLY pass=$pass fail=$fail; no link or runtime claim."
[[ $fail == 0 ]]
