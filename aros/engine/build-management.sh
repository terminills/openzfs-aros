#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
output_dir=${AROS_OUTPUT_DIR:-build-aros}
sdk_flags=()
test_flags=()
if [[ ${AROS_FULL_DISPATCHER:-0} == 1 ]];then test_flags+=(-DOZ_NATIVE_DISPATCHER);fi
if [[ -n ${AROS_SDK:-} ]];then sdk_flags=(--sysroot="$AROS_SDK" -B"$AROS_SDK/lib/");fi
# Requires the verified full engine and nvpair object builds. Diagnostic static
# packaging only; the production shared engine and DOS handler are separate work.
bash aros/engine/link-engine-test.sh aros/ui/service.c
cp "${AROS_ENGINE_OBJECT_DIR:-build-aros/engine}/engine-metadata.h" \
    "${AROS_ENGINE_OBJECT_DIR:-build-aros/engine}/engine-metadata.sha256" "$output_dir/"
"$cc" "${sdk_flags[@]}" -Wall -Wextra -O2 aros/ui/api.c aros/engine/management-test.c \
    -o "$output_dir/OpenZFSManagementTest"
"$cc" "${sdk_flags[@]}" -Wall -Wextra -O2 aros/ui/api.c aros/engine/write-test.c \
    -o "$output_dir/OpenZFSWriteTest"
"$cc" "${sdk_flags[@]}" -Wall -Wextra -O2 aros/ui/api.c aros/engine/mount-test.c \
    -o "$output_dir/OpenZFSMountTest"
bash aros/ui/build-manager-native.sh "$output_dir/OpenZFSManagerNative"
test_objects=()
if [[ -n ${AROS_PUBLIC_INIT_OBJECT:-} ]];then
    test_flags+=(-DOZ_TEST_UPSTREAM_INIT)
    test_objects+=("$AROS_PUBLIC_INIT_OBJECT")
fi
if [[ -n ${AROS_PUBLIC_MOUNT_CACHE_OBJECT:-} ]];then
    test_flags+=(-DOZ_TEST_UPSTREAM_MOUNT_CACHE)
    test_objects+=("$AROS_PUBLIC_MOUNT_CACHE_OBJECT")
fi
if [[ ${AROS_FULL_DISPATCHER:-0} == 1 ]];then
    AROS_ENGINE_OBJECT_DIR="$output_dir/client" bash aros/engine/compile-object.sh aros/cli/ioctl-public-test.c
    AROS_ENGINE_OBJECT_DIR="$output_dir/client" bash aros/engine/compile-object.sh aros/cli/control-client.c
    AROS_ENGINE_OBJECT_DIR="$output_dir/client" bash aros/engine/compile-object.sh aros/cli/stream-client.c
    AROS_ENGINE_OBJECT_DIR="$output_dir/client" bash aros/engine/compile-object.sh aros/cli/stream-channel.c
    AROS_ENGINE_OBJECT_DIR="$output_dir/client" bash aros/engine/compile-object.sh lib/libzfs_core/libzfs_core.c
    AROS_ENGINE_OBJECT_DIR="$output_dir/client" bash aros/engine/compile-object.sh lib/libnvpair/libnvpair.c
    test_objects+=("$output_dir/client/aros_cli_ioctl-public-test.c.o"
        "$output_dir/client/aros_cli_control-client.c.o"
        "$output_dir/client/aros_cli_stream-client.c.o"
        "$output_dir/client/aros_cli_stream-channel.c.o"
        "$output_dir/client/lib_libzfs_core_libzfs_core.c.o"
        "$output_dir/client/lib_libnvpair_libnvpair.c.o"
        "${AROS_NVPAIR_ARCHIVE:-build-aros/libnvpair-adt-consistent.a}"
        "${AROS_PRIVATE_PTHREAD:-build-aros/pthread-consistent.s3r1CHla/libpthread.a}")
fi
# Keep the standalone acceptance client aligned with
# OPENZFS_PUBLIC_TEST_FILES in mmakefile.src: its current fixtures exercise
# the real asynchronous command worker and persistent startup records.
"$cc" "${sdk_flags[@]}" "${test_flags[@]}" \
    -I"${AROS_PTHREAD_INCLUDE:?matching private pthread headers required}" \
    -DOZ_NATIVE_ONEXIT -Wall -Wextra -O2 \
    aros/ui/api.c aros/ui/admission-client.c aros/ui/project-worker.c \
    aros/cli/process.c aros/cli/arguments.c aros/cli/output.c \
    aros/ui/startup-settings.c aros/ui/startup-settings-file.c \
    aros/ui/preferences.c aros/ui/preferences-file.c aros/engine/hostid-record.c \
    aros/cli/version.c aros/engine/public-service-test.c \
    "${test_objects[@]}" "${AROS_PRIVATE_PTHREAD:?private pthread archive required}" \
    -liffparse -o "$output_dir/OpenZFSPublicServiceTest"
printf 'service_test_modes=%s\nfull_dispatcher=%s\nstatus_trace=%s\n' \
    "${AROS_SERVICE_TEST_MODES:-0}" "${AROS_FULL_DISPATCHER:-0}" "${AROS_STATUS_TRACE:-0}" >"$output_dir/service-build-options.txt"
sha256sum "$output_dir/OpenZFSServiceNative" >"$output_dir/service-build.sha256"
sha256sum "${AROS_LINKED_OBJECT:-build-aros/full-engine.o}" >"$output_dir/engine-object.sha256"
