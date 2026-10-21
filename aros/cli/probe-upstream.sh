#!/usr/bin/env bash
# Compile the real upstream CLI/client units; do not link the pool engine into
# commands or replace missing platform functions with success-returning stubs.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
crypto_include=${AROS_CRYPTO_INCLUDE:-/opt/aros-workspace/chromium-aros/src/third_party/boringssl/src/include}
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
out=$(mktemp -d "$artifact_root/cli-probe.XXXXXXXX")
echo "Evidence: $out"
cp "$repo/META" "$repo/aros/cli/generate-metadata.py" "$out/"
python3 "$out/generate-metadata.py" "$out/META" "$out/metadata.h"
sha256sum "$out/META" "$out/generate-metadata.py" "$out/metadata.h" > "$out/metadata.sha256"
python3 "$repo/aros/cli/metadata-test.py" > "$out/metadata-test.log" 2>&1
flags=(--sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra
    -ffunction-sections -fdata-sections
    -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DOZ_NATIVE_LABEL_CLIENT '-DTEXT_DOMAIN="zfs"'
    '-DSYSCONFDIR="ENV:OpenZFS"' '-DZFSEXECDIR="SYS:Tools/OpenZFS"'
    '-DPKGDATADIR="SYS:Storage/OpenZFS"'
    -include "$repo/aros/engine/config.h"
    -include "$out/metadata.h"
    -I"$repo/aros/cli/include"
    -I"$crypto_include"
    -I"$repo/build-aros/include" -I"$repo/aros/engine/include"
    -I"$repo/lib/libspl/include" -I"$repo/lib/libspl/include/os/linux"
    -I"$repo/include" -I"$repo/include/os/linux/zfs"
    -I"$repo/cmd/zpool" -I"$repo/cmd/zfs" -I"$repo/cmd"
    -I"$repo/build-aros/libtirpc-1.3.7/tirpc")
if [[ -n ${AROS_PTHREAD_INCLUDE:-} ]]; then
    flags=(-I"$AROS_PTHREAD_INCLUDE" "${flags[@]}")
fi
if [[ ${AROS_IMPORT_TRACE:-0} == 1 ]]; then flags+=(-DOZ_IMPORT_DIAGNOSTICS);fi
sources=(cmd/zfs/zfs_main.c cmd/zfs/zfs_iter.c cmd/zfs/zfs_project.c
    cmd/zpool/zpool_main.c cmd/zpool/zpool_iter.c cmd/zpool/zpool_util.c
    cmd/zpool/zpool_vdev.c lib/libzfs/libzfs_util.c
    lib/libzfs_core/libzfs_core.c aros/cli/gpt.c)
case ${1:-} in
    '') ;;
    --all)
        # Inventory every common client unit, not just the initial nine.
        # OS-specific implementations need native replacements, not accidental
        # selection of Linux sources because their headers happen to compile.
        mapfile -t sources < <(cd "$repo" &&
            rg --files --no-ignore cmd/zfs cmd/zpool lib/libzfs lib/libzfs_core lib/libzutil lib/libshare |
            rg '^(cmd/(zfs|zpool)|lib/(libzfs|libzfs_core|libzutil))/[^/]+\.c$|^lib/libshare/libshare\.c$' |
            LC_ALL=C sort)
        ;;
    *) echo "Usage: $0 [--all]" >&2; exit 2 ;;
esac
git -C "$repo" rev-parse HEAD > "$out/source-head.txt"
cp "$repo/aros/cli/probe-upstream.sh" "$out/"
sha256sum "$crypto_include/openssl/evp.h" > "$out/crypto-header.sha256"
git -C "$repo" diff --binary > "$out/source.patch"
"$cc" --version > "$out/compiler.txt"
printf '%q ' "$cc" "${flags[@]}" > "$out/flags.txt"
printf '\n' >> "$out/flags.txt"
failed=0
for src in "${sources[@]}"; do
    stem=${src//\//_}
    sha256sum "$repo/$src" >> "$out/sources.sha256"
    if "$cc" "${flags[@]}" -c "$repo/$src" -o "$out/$stem.o" > "$out/$stem.log" 2>&1; then
        printf 'PASS %s\n' "$src" | tee -a "$out/summary.txt"
    else
        printf 'FAIL %s\n' "$src" | tee -a "$out/summary.txt"
        failed=1
    fi
done
if [[ ${AROS_TEST_BUILD_INIT:-0} == 1 ]]; then
    cp "$repo/aros/cli/upstream-init-test.c" "$out/"
    init_objects=()
    for src in aros/cli/upstream-init-test.c aros/cli/upstream-mount-test.c aros/cli/property-ioctl-test.c aros/cli/import-files.c aros/cli/import-files-test.c aros/cli/file-vdev.c aros/cli/file-vdev-test.c aros/cli/mount.c aros/cli/lifecycle.c aros/cli/shares-object.c aros/cli/zone.c aros/cli/hostid.c module/avl/avl.c \
        aros/cli/device-path.c aros/cli/native-device.c aros/cli/native-device-test.c aros/cli/device-wait.c aros/cli/device-wait-test.c aros/cli/native-label.c aros/cli/native-label-test.c aros/cli/platform-hooks.c lib/libtpool/thread_pool.c \
        module/zcommon/zfs_namecheck.c module/zcommon/zfs_comutil.c \
        module/zcommon/zfs_prop.c module/zcommon/zpool_prop.c \
        module/zcommon/zprop_common.c module/zcommon/zfeature_common.c \
        module/zcommon/zfs_fletcher.c module/zcommon/zfs_fletcher_superscalar.c \
        module/zcommon/zfs_fletcher_superscalar4.c \
        lib/libuutil/uu_avl.c lib/libuutil/uu_alloc.c lib/libuutil/uu_misc.c \
        lib/libuutil/uu_ident.c lib/libspl/atomic.c lib/libspl/page.c aros/platform/cpu.c; do
        stem=${src//\//_}
        sha256sum "$repo/$src" >> "$out/init-sources.sha256"
        "$cc" "${flags[@]}" -c "$repo/$src" -o "$out/$stem.o"
        init_objects+=("$out/$stem.o")
    done
    "${cc%gcc}ld" -r --gc-sections -u oz_upstream_init_test -u oz_upstream_mount_test -u oz_upstream_promotion_readonly_test -u oz_upstream_import_files_test -u oz_upstream_file_vdev_test -u oz_upstream_device_test -u oz_upstream_device_wait_test -u oz_upstream_native_label_test \
        "${init_objects[@]}" "$out/lib_libzfs_libzfs_util.c.o" \
        "$out/lib_libzfs_libzfs_dataset.c.o" "$out/lib_libzfs_libzfs_config.c.o" \
        "$out/lib_libzfs_libzfs_iter.c.o" \
        "$out/lib_libzfs_libzfs_import.c.o" "$out/cmd_zpool_zpool_vdev.c.o" \
        "$out/lib_libzfs_libzfs_mount.c.o" "$out/lib_libzfs_libzfs_crypto.c.o" \
        "$out/lib_libzfs_libzfs_status.c.o" \
        "$out/lib_libzfs_libzfs_pool.c.o" "$out/lib_libzutil_zutil_nicenum.c.o" "$out/lib_libzutil_zutil_import.c.o" "$out/lib_libzutil_zutil_device_path.c.o" \
        -o "$out/upstream-init-selected.o"
    "${cc%gcc}nm" -u "$out/upstream-init-selected.o" > "$out/init-undefined.txt"
    sha256sum "$out/upstream-init-selected.o" > "$out/init-object.sha256"
fi
if [[ ${AROS_TEST_BUILD_MOUNTCACHE:-0} == 1 ]]; then
    "$cc" "${flags[@]}" -c "$repo/aros/cli/upstream-mount-cache-test.c" -o "$out/upstream-mount-cache-test.o"
    "$cc" "${flags[@]}" -c "$repo/module/avl/avl.c" -o "$out/avl.o"
    "${cc%gcc}ld" -r --gc-sections -u oz_upstream_mount_cache_test -u oz_upstream_mount_cache_finish \
        "$out/upstream-mount-cache-test.o" "$out/lib_libzfs_libzfs_dataset.c.o" \
        "$out/lib_libzfs_libzfs_util.c.o" "$out/avl.o" -o "$out/upstream-mount-cache-selected.o"
    cp "$repo/aros/cli/upstream-mount-cache-test.c" "$out/"
fi
if [[ ${AROS_TEST_BUILD_PROCESS:-0} == 1 ]]; then
    cp "$repo/aros/cli/upstream-process-test.c" "$out/"
    "$cc" "${flags[@]}" -c "$out/upstream-process-test.c" -o "$out/upstream-process-test.o"
    "${cc%gcc}ld" -r --gc-sections -u main "$out/upstream-process-test.o" \
        "$out/lib_libzfs_libzfs_util.c.o" -o "$out/upstream-process-selected.o"
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" "$out/upstream-process-selected.o" \
        "$repo/aros/cli/process.c" "$repo/aros/cli/arguments.c" "$repo/aros/cli/output.c" \
        -o "$out/OpenZFSUpstreamProcessTest"
fi
if [[ ${AROS_TEST_BUILD_COMPAT:-0} == 1 ]]; then
    cp "$repo/aros/cli/compatibility-test.c" "$out/"
    "$cc" "${flags[@]}" -c "$out/compatibility-test.c" -o "$out/compatibility-test.o"
    for input in "$repo/build-aros/adt-consistent-engine/module_zcommon_zfeature_common.c.o" \
        "$repo/build-aros/adt-nvpair/assert.o" "$repo/build-aros/adt-nvpair/backtrace.o"; do
        cp "$input" "$out/"
    done
    "${cc%gcc}ld" -r --gc-sections -u main "$out/compatibility-test.o" \
        "$out/lib_libzfs_libzfs_pool.c.o" "$out/module_zcommon_zfeature_common.c.o" \
        "$out/assert.o" "$out/backtrace.o" -o "$out/compatibility-selected.o"
    pthread=${AROS_PRIVATE_PTHREAD:-$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a}
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" "$out/compatibility-selected.o" "$pthread" \
        -o "$out/OpenZFSCompatibilityTest"
    sha256sum "$out/"*.o "$pthread" "$out/OpenZFSCompatibilityTest" > "$out/compatibility.sha256"
fi
printf 'Evidence: %s\n' "$out"
exit "$failed"
