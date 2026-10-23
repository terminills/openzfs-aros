#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:?explicit AROS compiler required}
sdk=${AROS_SDK:?explicit matching AROS SDK required}
sdk=$(realpath -e "$sdk")
[[ -f $sdk/include/exec/types.h && -d $sdk/lib ]]
out=${AROS_NVPAIR_OUTPUT_DIR:-$repo/build-aros}
tirpc=${AROS_TIRPC_SOURCE:-$repo/build-aros/libtirpc-1.3.7}
archive=${AROS_TIRPC_ARCHIVE:-$repo/build-aros/libtirpc-1.3.7.tar.bz2}
object_dir=${AROS_NVPAIR_OBJECT_DIR:-$out/nvpair}
nvpair=${AROS_NVPAIR_ARCHIVE:-$out/libnvpair-aros.a}
mkdir -p "$object_dir"
mkdir -p "$out"
source "$repo/aros/engine/storage-preflight.sh"
oz_storage_preflight "$out"
oz_storage_preflight "$object_dir"
provenance=$(mktemp -d "$out/nvpair-inputs.XXXXXXXX")
echo "Nvpair provenance: $provenance"
"$cc" --version >"$provenance/compiler.txt"
printf 'compiler=%s\nsdk=%s\ntirpc=%s\n' "$cc" "$sdk" "$tirpc" >"$provenance/inputs.txt"
cp "$0" "$provenance/build-nvpair.sh"
git -C "$repo" rev-parse HEAD >"$provenance/source-head.txt"
git -C "$repo" diff --binary >"$provenance/source.patch"
pthread_lib=(-lpthread)
if [[ -n ${AROS_PRIVATE_PTHREAD:-} ]];then
    : "${AROS_PTHREAD_INCLUDE:?private pthread archive requires matching headers}"
    cmp "$(dirname "$AROS_PRIVATE_PTHREAD")/source/pthread.h" "$AROS_PTHREAD_INCLUDE/pthread.h"
    pthread_lib=("$AROS_PRIVATE_PTHREAD")
fi
sdk_flags=(--sysroot="$sdk" -B"$sdk/lib/")
if [[ -n ${AROS_PTHREAD_INCLUDE:-} ]];then
    sdk_flags+=(-I"$AROS_PTHREAD_INCLUDE")
fi
if [[ ! -f "$archive" ]]; then
    curl -fL --max-time 90 https://downloads.sourceforge.net/project/libtirpc/libtirpc/1.3.7/libtirpc-1.3.7.tar.bz2 -o "$archive"
fi
echo "b47d3ac19d3549e54a05d0019a6c400674da716123858cfdb6d3bdd70a66c702  $archive" | sha256sum -c -
# A pinned archive alone says nothing about a pre-existing extracted tree.
# Compare every consumed XDR source and header against that archive before use.
tar -xjf "$archive" -C "$provenance"
if [[ ! -d "$tirpc" ]];then
    [[ $(basename "$tirpc") == libtirpc-1.3.7 ]]
    mkdir -p "$(dirname "$tirpc")"
    tar -xjf "$archive" -C "$(dirname "$tirpc")"
fi
for file in xdr xdr_mem xdr_array xdr_float;do
    cmp "$tirpc/src/$file.c" "$provenance/libtirpc-1.3.7/src/$file.c"
done
diff -qr "$tirpc/tirpc" "$provenance/libtirpc-1.3.7/tirpc"
find "$tirpc/tirpc" -type f -exec sha256sum {} + >"$provenance/source.sha256"
sha256sum "$archive" "$tirpc/src/"{xdr,xdr_mem,xdr_array,xdr_float}.c >>"$provenance/source.sha256"
sources=(module/nvpair/nvpair.c module/nvpair/fnvpair.c module/nvpair/nvpair_alloc_fixed.c
    lib/libnvpair/nvpair_alloc_system.c lib/libspl/assert.c lib/libspl/backtrace.c)
# Record the port inputs too, not only the external XDR codec. Include headers
# and SDK files so an identical Git revision cannot hide a changed ABI input.
for file in "${sources[@]}" aros/engine/nvpair-test.c aros/engine/config.h;do
    sha256sum "$repo/$file" >>"$provenance/source.sha256"
done
find "$repo/include" "$repo/lib/libspl/include" "$sdk/include" -type f -print0 |
    LC_ALL=C sort -z | xargs -0 sha256sum >>"$provenance/source.sha256"
if [[ -n ${AROS_PTHREAD_INCLUDE:-} ]];then
    find "$AROS_PTHREAD_INCLUDE" -type f -name '*.h' -print0 |
        LC_ALL=C sort -z | xargs -0 sha256sum >>"$provenance/source.sha256"
fi
if [[ -n ${AROS_PRIVATE_PTHREAD:-} ]];then
    sha256sum "$AROS_PRIVATE_PTHREAD" >>"$provenance/source.sha256"
fi
# Match upstream's aliasing and diagnostic policy for its portable sources.
flags=(-O2 -g -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter
    -include "$repo/aros/engine/config.h"
    -Wno-sign-compare -Wno-missing-field-initializers -Werror=implicit-function-declaration
    -I"$repo/lib/libspl/include" -I"$repo/lib/libspl/include/os/linux"
    -I"$repo/include" -I"$tirpc/tirpc" -include sys/sysmacros.h)
objects=()
for file in "${sources[@]}"; do
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
archiver=$("$cc" -print-prog-name=ar)
"$archiver" rcs "$nvpair" "${objects[@]}"
"$cc" "${sdk_flags[@]}" "${flags[@]}" "$repo/aros/engine/nvpair-test.c" "$nvpair" "${pthread_lib[@]}" -o "$out/OpenZFSNvpairTest"
sha256sum -c "$provenance/source.sha256" >"$provenance/source-after-check.log"
sha256sum "${objects[@]}" "$nvpair" "$out/OpenZFSNvpairTest" >"$provenance/outputs.sha256"
