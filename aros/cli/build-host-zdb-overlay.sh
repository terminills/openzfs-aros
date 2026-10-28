#!/usr/bin/env bash
# Compile current debugger translation units privately against a configured
# host control build. Never rebuild or overwrite that control build.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
: "${OZ_HOST_CONTROL:?}" "${AROS_ARTIFACT_ROOT:?}"
source "$repo/aros/engine/storage-preflight.sh"
root=$(oz_artifact_root "$repo/build-aros" "$AROS_ARTIFACT_ROOT")
control=$(realpath -e "$OZ_HOST_CONTROL")
out=$(mktemp -d "$root/host-zdb-overlay.XXXXXXXX")
echo "Evidence: $out"
cp "$0" "$out/build.sh"
cp "$repo/cmd/zdb/"{zdb.c,zdb.h,zdb_il.c,zdb_label_io.h} "$out/"
sha256sum "$out/"*.c "$out/"*.h "$control/zfs_config.h" \
    "$control/.libs/libzdb.a" "$control/.libs/libzpool.so" \
    "$control/.libs/libzfs_core.so" "$control/.libs/libnvpair.so" >"$out/inputs.sha256"
flags=(-include "$control/zfs_config.h" -I"$control/include"
    -I"$repo/include" -I"$repo/module/icp/include" -I"$repo/lib/libspl/include"
    -I"$repo/lib/libspl/include/os/linux" -D_GNU_SOURCE -D_REENTRANT
    -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DDEBUG -UNDEBUG -DZFS_DEBUG
    '-DTEXT_DOMAIN="zfs-linux-user"'
    -std=gnu99 -Wall -Wextra -Wno-sign-compare -fno-strict-aliasing -g -O2)
for unit in zdb zdb_il;do
    cc "${flags[@]}" -c "$out/$unit.c" -o "$out/$unit.o" >>"$out/build.log" 2>&1
done
cc -o "$out/zdb" "$out/zdb.o" "$out/zdb_il.o" \
    "$control/.libs/libzdb.a" -L"$control/.libs" \
    -Wl,-rpath,"$control/.libs" -lzpool -lzfs_core -lnvpair -lcrypto \
    >>"$out/build.log" 2>&1
sha256sum -c "$out/inputs.sha256" >"$out/unchanged.log"
sha256sum "$out/zdb" >"$out/binary.sha256"
echo "PASS private host debugger build: $out"
