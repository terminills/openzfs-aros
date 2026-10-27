#!/usr/bin/env bash
# Compile upstream diagnostic sources only; does not link or run a debugger.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
: "${AROS_CC:?explicit cross compiler required}"
: "${AROS_SDK:?explicit matching SDK required}"
: "${AROS_PTHREAD_INCLUDE:?matching pthread headers required}"
: "${AROS_CRYPTO_INCLUDE:?matching crypto headers required}"
: "${AROS_TIRPC_SOURCE:?matching tirpc source required}"
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
out=$(mktemp -d "$root/zdb-probe.XXXXXXXX")
echo "Evidence: $out"
flags=(--sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -O2 -g
    -fno-strict-aliasing -ffunction-sections -fdata-sections
    -DLIB_ZPOOL_BUILD -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
    -include "$repo/aros/engine/config.h" -include sys/sysmacros.h
    -I"$AROS_PTHREAD_INCLUDE" -I"$AROS_CRYPTO_INCLUDE"
    -I"$repo/aros/zpl/include" -include "$repo/aros/zpl/context.h"
    -I"$repo/aros/engine/include" -I"$repo/lib/libspl/include"
    -I"$repo/lib/libspl/include/os/linux" -I"$repo/include"
    -I"$repo/include/os/linux/zfs" -I"$repo/module/icp/include"
    -I"$AROS_TIRPC_SOURCE/tirpc")
git rev-parse HEAD >"$out/source-head.txt"
git diff --binary >"$out/source.patch"
cp "$0" "$out/"
status=0
for source in cmd/zdb/zdb.c cmd/zdb/zdb_il.c lib/libzdb/libzdb.c;do
    name=${source//\//_}
    sha256sum "$source" >>"$out/sources.sha256"
    printf '%q ' "$AROS_CC" "${flags[@]}" -MD -MF "$out/$name.d" -c "$source" -o "$out/$name.o" >>"$out/commands.sh"
    printf '\n' >>"$out/commands.sh"
    if "$AROS_CC" "${flags[@]}" -MD -MF "$out/$name.d" -c "$source" -o "$out/$name.o" >"$out/$name.log" 2>&1;then
        echo "PASS compile $source"
    else
        echo "FAIL compile $source (see $out/$name.log)"
        status=1
    fi
done
echo 'Compile audit only: native linking, read-only admission and runtime integrity checks remain required.'
exit "$status"
