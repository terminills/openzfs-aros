#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
cd "$repo"
objects=()
for file in lib/libzpool/kernel.c aros/platform/cpu.c lib/libspl/page.c lib/libspl/atomic.c lib/libzutil/zutil_device_path.c aros/engine/platform-test.c; do
    bash aros/engine/compile-object.sh "$file"
    objects+=("build-aros/engine/${file//\//_}.o")
done
# Select only the adapters exercised by this test, before the normal AROS
# startup/library link. This is not a substitute for linking the full engine.
"${cc%gcc}ld" -r --gc-sections -u main "${objects[@]}" -o build-aros/platform-test.o
"$cc" build-aros/platform-test.o build-aros/libnvpair-aros.a -lpthread -o build-aros/OpenZFSPlatformTest
