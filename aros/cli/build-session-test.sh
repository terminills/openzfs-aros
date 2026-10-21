#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
out=$(mktemp -d "$repo/build-aros/cli-session.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
export AROS_ENGINE_OBJECT_DIR="$out/objects"
export AROS_NATIVE_ONEXIT=1
export AROS_SDK=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
export AROS_PTHREAD_INCLUDE=${AROS_PTHREAD_INCLUDE:-$repo/build-aros/pthread-consistent.s3r1CHla/source}
pthread=${AROS_PRIVATE_PTHREAD:-$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
git rev-parse HEAD > "$out/source-head.txt"
git diff --binary > "$out/source.patch"
cp "$0" "$out/"
cp aros/cli/session-cleanup.h "$out/"
cp aros/cli/stream-channel.h "$out/"
tar -cf "$out/native-headers.tar" aros/engine/include aros/zpl/include \
    aros/engine/config.h aros/engine/ioctl-copy.h aros/engine/file-scope.h \
    aros/engine/device-io.h aros/engine/control-file.h aros/zpl/context.h \
    include/sys/zfs_onexit.h include/sys/zfs_ioctl_impl.h include/sys/zfs_ioctl.h
cp aros/engine/compile-object.sh "$out/"
sha256sum "$pthread" "$AROS_PTHREAD_INCLUDE/pthread.h" \
    build-aros/adt-nvpair/assert.o build-aros/adt-nvpair/backtrace.o > "$out/dependencies.sha256"
objects=()
libraries=()
case ${1:-session} in
    control) specific=(lib/libspl/atomic.c module/zfs/zfs_ioctl.c module/zfs/zfs_onexit.c module/zfs/fm.c module/zfs/zfs_fm.c module/avl/avl.c aros/engine/control-file.c aros/engine/ioctl-platform.c aros/engine/device-io.c aros/engine/file-scope.c aros/cli/session-cleanup.c aros/cli/control-file-test.c)
        specific+=(aros/cli/stream-channel.c aros/cli/stream-client.c)
        binary=OpenZFSControlFileTest
        libraries+=("$repo/build-aros/libnvpair-adt-consistent.a") ;;
    session) specific=(module/zfs/zfs_onexit.c aros/engine/device-io.c aros/engine/file-scope.c aros/cli/session-cleanup.c aros/cli/session-cleanup-test.c)
        binary=OpenZFSSessionCleanupTest ;;
    mount-reference) specific=(module/zfs/dmu_objset.c aros/zpl/mount-state.c aros/cli/mount-reference-test.c)
        binary=OpenZFSMountReferenceTest ;;
    ioctl-copy) specific=(aros/engine/ioctl-copy.c aros/cli/ioctl-copy-test.c)
        binary=OpenZFSIoctlCopyTest
        gcc -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
            aros/engine/ioctl-copy.c aros/cli/ioctl-copy-test.c -pthread -o "$out/host-copy-test"
        "$out/host-copy-test" > "$out/host-test.log" 2>&1 ;;
    *) echo 'Usage: build-session-test.sh [session|control|mount-reference|ioctl-copy]' >&2; exit 2 ;;
esac
if ((${#libraries[@]})); then
    sha256sum "${libraries[@]}" >> "$out/dependencies.sha256"
fi
for source in aros/engine/caller.c lib/libzpool/kernel.c lib/libzpool/zfs_debug.c lib/libspl/list.c \
    lib/libzutil/zutil_device_path.c module/zfs/spa_misc.c "${specific[@]}"; do
    zpl=1
    case "$source" in lib/*|module/zfs/spa_misc.c|module/zfs/dmu_objset.c) zpl=0 ;; esac
    AROS_ZPL_BUILD=$zpl bash -x aros/engine/compile-object.sh "$source"
    stem=${source//\//_}
    cp "$source" "$out/$stem"
    sha256sum "$source" >> "$out/sources.sha256"
    objects+=("$out/objects/$stem.o")
done
# Focused lifecycle fixture, not a full dispatcher link. The control case
# exercises the actual upstream
# minor, on-exit and event state, not ioctl dispatch. Retain selected symbols.
"${cc%gcc}ld" -r --gc-sections -u main "${objects[@]}" -o "$out/selected.o"
"${cc%gcc}nm" "$out/selected.o" > "$out/symbols.txt"
"$cc" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" "$out/selected.o" \
    "$repo/build-aros/adt-nvpair/assert.o" "$repo/build-aros/adt-nvpair/backtrace.o" \
    "${libraries[@]}" "$pthread" -o "$out/$binary"
sha256sum "$out/$binary" "$pthread" > "$out/binaries.sha256"
