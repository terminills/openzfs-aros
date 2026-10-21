#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
out=${AROS_NVPAIR_OUTPUT_DIR:-$repo/build-aros}
tirpc=${AROS_TIRPC_SOURCE:-$repo/build-aros/libtirpc-1.3.7}
archive=${AROS_TIRPC_ARCHIVE:-$repo/build-aros/libtirpc-1.3.7.tar.bz2}
object_dir=${AROS_NVPAIR_OBJECT_DIR:-$out/nvpair}
nvpair=${AROS_NVPAIR_ARCHIVE:-$out/libnvpair-aros.a}
mkdir -p "$object_dir"
mkdir -p "$out"
pthread_lib=(-lpthread)
if [[ -n ${AROS_PRIVATE_PTHREAD:-} ]];then
    : "${AROS_PTHREAD_INCLUDE:?private pthread archive requires matching headers}"
    cmp "$(dirname "$AROS_PRIVATE_PTHREAD")/source/pthread.h" "$AROS_PTHREAD_INCLUDE/pthread.h"
    pthread_lib=("$AROS_PRIVATE_PTHREAD")
fi
sdk_flags=()
if [[ -n ${AROS_PTHREAD_INCLUDE:-} ]];then
    sdk_flags+=(-I"$AROS_PTHREAD_INCLUDE")
fi
if [[ -n ${AROS_SDK:-} ]]; then
    sdk_flags+=(--sysroot="$AROS_SDK" -B"$AROS_SDK/lib/")
fi
if [[ ! -f "$archive" ]]; then
    curl -fL --max-time 90 https://downloads.sourceforge.net/project/libtirpc/libtirpc/1.3.7/libtirpc-1.3.7.tar.bz2 -o "$archive"
fi
echo "b47d3ac19d3549e54a05d0019a6c400674da716123858cfdb6d3bdd70a66c702  $archive" | sha256sum -c -
if [[ ! -d "$tirpc" ]];then
    [[ $(basename "$tirpc") == libtirpc-1.3.7 ]]
    mkdir -p "$(dirname "$tirpc")"
    tar -xjf "$archive" -C "$(dirname "$tirpc")"
fi
# Match upstream's aliasing and diagnostic policy for its portable sources.
flags=(-O2 -g -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter
    -include "$repo/aros/engine/config.h"
    -Wno-sign-compare -Wno-missing-field-initializers -Werror=implicit-function-declaration
    -I"$repo/lib/libspl/include" -I"$repo/lib/libspl/include/os/linux"
    -I"$repo/include" -I"$tirpc/tirpc" -include sys/sysmacros.h)
objects=()
for file in module/nvpair/nvpair.c module/nvpair/fnvpair.c module/nvpair/nvpair_alloc_fixed.c \
    lib/libnvpair/nvpair_alloc_system.c lib/libspl/assert.c lib/libspl/backtrace.c; do
    obj="$object_dir/$(basename "$file" .c).o"
    "$cc" "${sdk_flags[@]}" "${flags[@]}" -c "$repo/$file" -o "$obj"
    objects+=("$obj")
done
# Only libtirpc's memory XDR codec, not its RPC/socket/authentication stack.
for file in xdr xdr_mem xdr_array xdr_float; do
    obj="$object_dir/$file.o"
    "$cc" "${sdk_flags[@]}" -O2 -g -Wall -Wno-unused-variable -I"$tirpc/tirpc" -c "$tirpc/src/$file.c" -o "$obj"
    objects+=("$obj")
done
"${cc%gcc}ar" rcs "$nvpair" "${objects[@]}"
"$cc" "${sdk_flags[@]}" "${flags[@]}" "$repo/aros/engine/nvpair-test.c" "$nvpair" "${pthread_lib[@]}" -o "$out/OpenZFSNvpairTest"
