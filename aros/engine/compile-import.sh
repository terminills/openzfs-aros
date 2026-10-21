#!/usr/bin/env bash
# Compile the complete upstream import unit separately from verified binaries.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
sdk=${AROS_SDK:?native AROS SDK required}
out=${AROS_IMPORT_OBJECT_DIR:-$repo/build-aros/import-audit}
mkdir -p "$out"
"$cc" -c -O2 -g -fno-strict-aliasing -Wall -Wextra \
    -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers \
    -DLIB_ZPOOL_BUILD -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
    --sysroot="$sdk" -B"$sdk/lib/" \
    -include "$repo/aros/engine/config.h" -include sys/sysmacros.h \
    -include "$repo/aros/engine/import-include/largefile.h" \
    '-DTEXT_DOMAIN="zfs"' \
    -I"$repo/aros/engine/import-include" -I"$repo/build-aros/include" \
    -I"$repo/aros/engine/include" -I"$repo/lib/libspl/include" \
    -I"$repo/lib/libspl/include/os/linux" -I"$repo/include" \
    -I"$repo/include/os/linux/zfs" \
    "$repo/lib/libzutil/zutil_import.c" -o "$out/zutil_import.o"
