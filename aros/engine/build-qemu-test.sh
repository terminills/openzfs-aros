#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
export AROS_SDK=${AROS_NATIVE_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
export AROS_ENGINE_OBJECT_DIR="$repo/build-aros/native-engine"
export AROS_NVPAIR_OBJECT_DIR="$repo/build-aros/native-nvpair"
export AROS_NVPAIR_ARCHIVE="$repo/build-aros/libnvpair-native-aros.a"
export AROS_LINKED_OBJECT="$repo/build-aros/native-full-engine.o"
export AROS_PTHREAD_CORE="$repo/build-aros/native-engine/pthread-native-current.o"
pthread_source=${AROS_PTHREAD_SOURCE:-/opt/aros-workspace/aros-adt-src/compiler/pthread/pthread.c}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
test -f "$AROS_SDK/lib/startup.o"
mkdir -p "$AROS_ENGINE_OBJECT_DIR"
# The old nightly archive predates the upstream stack timer-port spinlock fix.
# Build that existing source privately; do not mutate the shared SDK/archive.
sha256sum "$pthread_source"
"$cc" --sysroot="$AROS_SDK" -O2 -g -c "$pthread_source" -o "$AROS_PTHREAD_CORE"
bash aros/engine/build-nvpair.sh
bash aros/engine/compile-sweep.sh
bash aros/engine/compile-deps.sh
bash aros/engine/link-engine-test.sh aros/engine/qemu-pool-test.c
bash aros/engine/link-engine-test.sh aros/engine/qemu-import-test.c
