#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
sdk=${AROS_SDK:?native AROS SDK required}
export AROS_ENGINE_OBJECT_DIR="$repo/build-aros/import-audit"
pthread=${AROS_PRIVATE_PTHREAD:?consistent private pthread archive required}
: "${AROS_PTHREAD_INCLUDE:?matching private pthread headers required}"
cmp "$(dirname "$pthread")/source/pthread.h" "$AROS_PTHREAD_INCLUDE/pthread.h"
bash aros/engine/compile-object.sh lib/libtpool/thread_pool.c
bash aros/engine/compile-object.sh aros/engine/import-thread-test.c
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -nix \
    "$AROS_ENGINE_OBJECT_DIR/aros_engine_import-thread-test.c.o" \
    "$AROS_ENGINE_OBJECT_DIR/lib_libtpool_thread_pool.c.o" \
    build-aros/libnvpair-native-aros.a \
    "$pthread" \
    -o "$AROS_ENGINE_OBJECT_DIR/OpenZFSImportThreadTest"
