#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
source=${1:?repository relative source required}
extra=()
if [[ ${AROS_TEST_LOG_DATA_FLUSH:-0} == 1 ]];then
    [[ ${AROS_TEST_LOG_FLUSH:-0} == 1 ]] || exit 2
    extra+=(-DOZ_TEST_LOG_DATA_FLUSH)
fi
if [[ ${AROS_TEST_LOG_FLUSH:-0} == 1 ]];then
    [[ ${AROS_TEST_AUX_FLUSH:-0} == 1 && ${AROS_TEST_AUX_SPARE:-0} == 0 ]] || exit 2
    extra+=(-DOZ_TEST_LOG_FLUSH)
fi
if [[ ${AROS_TEST_ACTIVE_SPARE:-0} == 1 ]];then
    [[ ${AROS_TEST_AUX_SPARE:-0} == 1 ]] || exit 2
    extra+=(-DOZ_TEST_ACTIVE_SPARE)
fi
if [[ ${AROS_TEST_AUX_SPARE:-0} == 1 ]];then
    [[ ${AROS_TEST_AUX_FLUSH:-0} == 1 ]] || exit 2
    extra+=(-DOZ_TEST_AUX_SPARE)
fi
if [[ ${AROS_TEST_AUX_FLUSH:-0} == 1 ]];then
    [[ ${AROS_TEST_STRICT_FLUSH:-0} == 1 ]] || exit 2
    extra+=(-DOZ_TEST_AUX_FLUSH)
fi
if [[ ${AROS_TEST_SINGLE_LEAF_FLUSH:-0} == 1 ]];then
    [[ ${AROS_TEST_STRICT_FLUSH:-0} == 1 ]] || exit 2
    extra+=(-DOZ_TEST_SINGLE_LEAF_FLUSH)
fi
if [[ ${AROS_TEST_STRICT_FLUSH:-0} == 1 ]];then
    extra+=(-DOZ_TEST_STRICT_FLUSH)
fi
if [[ ${AROS_TEST_ZIL_FLUSH:-0} == 1 ]];then
    [[ ${AROS_TEST_STRICT_FLUSH:-0} == 1 ]] || exit 2
    extra+=(-DOZ_TEST_ZIL_FLUSH)
fi
if [[ $source == aros/engine/status.c || $source == module/zfs/spa.c ]];then
    case ${AROS_STATUS_TRACE:-0} in
        0) ;;
        1) extra+=(-DOZ_STATUS_TRACE);echo 'Diagnostic build: status lock-stage trace enabled' >&2 ;;
        *) echo 'AROS_STATUS_TRACE must be 0 or 1' >&2;exit 2 ;;
    esac
fi
if [[ ${AROS_TEST_DOS_ALLOC:-0} == 1 ]];then
    if [[ $source == aros/engine/dos-handler.c ]];then
        extra+=(-Dcalloc=oz_dos_test_calloc -Doz_zpl_create_as=oz_dos_test_create_as
            -Doz_zpl_replace_as=oz_dos_test_replace_as)
    elif [[ $source == aros/engine/dos-handler-test.c ]];then
        extra+=(-DOZ_DOS_ALLOC_TEST)
    fi
fi
if [[ $source == aros/engine/admission.c && ${AROS_TEST_QUEUE_HOLD:-0} == 1 ]];then
    extra+=(-DOZ_TEST_QUEUE_HOLD)
fi
if [[ $source == aros/ui/service.c ]];then
    case ${AROS_SERVICE_TEST_MODES:-0} in
        0) ;;
        1) extra+=(-DOZ_SERVICE_TEST_MODES) ;;
        *) echo 'AROS_SERVICE_TEST_MODES must be 0 or 1' >&2;exit 2 ;;
    esac
fi
if [[ $source == lib/libnvpair/libnvpair.c || $source == lib/libefi/rdwr_efi.c || $source == aros/engine/efi-label.c ]]; then
    extra+=(-I"$repo/aros/cli/include")
fi
if [[ ${AROS_FULL_DISPATCHER:-0} == 1 ]]; then
    extra+=(-DOZ_NATIVE_DISPATCHER -DOZ_NATIVE_ONEXIT)
fi
if [[ ${AROS_ONEXIT_STATE_ONLY:-0} == 1 ]]; then
    extra+=(-DOZ_ONEXIT_STATE_ONLY)
fi
if [[ ${AROS_NATIVE_ONEXIT:-0} == 1 ]]; then
    extra+=(-DOZ_NATIVE_ONEXIT)
fi
if [[ $source == aros/engine/pool-test.c && ${AROS_TEST_DISCOVERY:-0} == 1 ]];then
    extra+=(-DOZ_TEST_DISCOVERY)
fi
if [[ ( $source == aros/engine/import-platform.c || $source == aros/cli/import-files.c ) && ${AROS_IMPORT_TRACE:-0} == 1 ]];then
    extra+=(-DOZ_IMPORT_DIAGNOSTICS)
fi
if [[ -n ${AROS_PTHREAD_INCLUDE:-} ]];then
    extra+=(-I"$AROS_PTHREAD_INCLUDE")
fi
if [[ $source == lib/libzutil/zutil_import.c ]];then
    extra+=(-I"$repo/aros/engine/import-include"
        -include "$repo/aros/engine/import-include/largefile.h" '-DTEXT_DOMAIN="zfs"')
fi
if [[ ${AROS_ZPL_BUILD:-0} == 1 ]]; then
    # Separate objects: never replace the verified userspace engine silently.
    extra+=(-I"$repo/aros/zpl/include" -include "$repo/aros/zpl/context.h")
fi
if [[ -n ${AROS_SDK:-} ]]; then
    extra+=(--sysroot="$AROS_SDK" -B"$AROS_SDK/lib/")
fi
object_dir=${AROS_ENGINE_OBJECT_DIR:-$repo/build-aros/engine}
if [[ "$source" == module/zstd/* ]]; then
    extra+=(-include "$repo/module/zstd/include/zstd_compat_wrapper.h" -fno-tree-vectorize)
fi
mkdir -p "$object_dir"
if [[ $source == aros/engine/admission.c ]]; then
    python3 "$repo/aros/cli/generate-metadata.py" "$repo/META" "$object_dir/engine-metadata.h"
    sha256sum "$repo/META" "$repo/aros/cli/generate-metadata.py" "$object_dir/engine-metadata.h" \
        > "$object_dir/engine-metadata.sha256"
    extra+=(-include "$object_dir/engine-metadata.h")
fi
exec "$cc" -c -O2 -g -fno-strict-aliasing -ffunction-sections -fdata-sections -Wall -Wextra \
    -Wno-sign-compare -Wno-missing-field-initializers -Wno-unused-parameter \
    -DLIB_ZPOOL_BUILD -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
    -include "$repo/aros/engine/config.h" "${extra[@]}" -I"${AROS_GENERATED_INCLUDE:-$repo/build-aros/include}" \
    -I"$repo/aros/engine/include" \
    -I"$repo/lib/libspl/include" -I"$repo/lib/libspl/include/os/linux" \
    -I"$repo/include" -I"$repo/include/os/linux/zfs" -I"$repo/module/icp/include" \
    -I"${AROS_TIRPC_SOURCE:-$repo/build-aros/libtirpc-1.3.7}/tirpc" -include sys/sysmacros.h \
    "$repo/$source" -o "$object_dir/${source//\//_}.o"
