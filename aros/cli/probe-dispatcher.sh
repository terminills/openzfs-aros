#!/usr/bin/env bash
# Inventory the real upstream service dispatcher, not a GUI-command substitute.
# No service is launched and no installed engine objects are overwritten.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d "$repo/build-aros/ioctl-probe.XXXXXXXX")
export AROS_ENGINE_OBJECT_DIR="$out/objects"
export AROS_ZPL_BUILD=1
export AROS_SDK=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
export AROS_PTHREAD_INCLUDE=${AROS_PTHREAD_INCLUDE:-$repo/build-aros/pthread-consistent.s3r1CHla/source}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
git -C "$repo" rev-parse HEAD > "$out/source-head.txt"
git -C "$repo" diff --binary > "$out/source.patch"
cp "$0" "$repo/aros/engine/compile-object.sh" "$out/"
tar -C "$repo" -cf "$out/native-headers.tar" aros/engine/include aros/zpl/include \
    aros/engine/config.h aros/zpl/context.h
"$cc" --version > "$out/compiler.txt"
printf 'SDK=%s\nPTHREAD_INCLUDE=%s\n' "$AROS_SDK" "$AROS_PTHREAD_INCLUDE" > "$out/inputs.txt"
failed=0
# A compiling ioctl unit is not a linked dispatcher. These real upstream
# event/zvol units supply required paths excluded by the libzpool build.
for source in module/zfs/zfs_ioctl.c module/zfs/zfs_onexit.c \
    module/zfs/fm.c module/zfs/zfs_fm.c module/zfs/zvol.c; do
    stem=${source//\//_}
    cp "$repo/$source" "$out/$stem"
    sha256sum "$repo/$source" >> "$out/sources.sha256"
    if bash -x "$repo/aros/engine/compile-object.sh" "$source" > "$out/$stem.log" 2>&1; then
        "${cc%gcc}nm" -u "$out/objects/$stem.o" > "$out/$stem.undefined.txt"
        "${cc%gcc}nm" --defined-only "$out/objects/$stem.o" > "$out/$stem.defined.txt"
        printf 'PASS %s\n' "$source" | tee -a "$out/summary.txt"
    else
        printf 'FAIL %s\n' "$source" | tee -a "$out/summary.txt"
        failed=1
    fi
done
# libzpool supplies diagnostic on-exit substitutes. Keep the competing source
# with this inventory: a compiled upstream unit must not simply be added to
# that link without replacing the old definitions and supplying session state.
cp "$repo/lib/libzpool/kernel.c" "$out/lib_libzpool_kernel.c"
sha256sum "$repo/lib/libzpool/kernel.c" >> "$out/sources.sha256"
printf 'Evidence: %s\n' "$out"
exit "$failed"
