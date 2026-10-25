#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
if [[ -n ${AROS_PTHREAD_CORE:-} ]];then
    echo 'Single pthread core overrides can corrupt ThreadInfo; use AROS_PRIVATE_PTHREAD with a complete consistent archive.' >&2
    exit 2
fi
if [[ -n ${AROS_PRIVATE_PTHREAD:-} ]];then
    : "${AROS_PTHREAD_INCLUDE:?private pthread archive requires matching headers}"
    cmp "$(dirname "$AROS_PRIVATE_PTHREAD")/source/pthread.h" "$AROS_PTHREAD_INCLUDE/pthread.h"
fi
zlib_import=${AROS_ZLIB_IMPORT:-/opt/aros-workspace/aros-adt-hosted-clean/bin/linux-x86_64/AROS/Developer/lib/libz1.a}
cd "$repo"
object_dir=${AROS_ENGINE_OBJECT_DIR:-build-aros/engine}
output_dir=${AROS_OUTPUT_DIR:-build-aros}
mkdir -p "$output_dir"
link_flags=()
if [[ -n ${AROS_SDK:-} ]]; then
    link_flags+=(--sysroot="$AROS_SDK" -B"$AROS_SDK/lib/")
fi
nvpair=${AROS_NVPAIR_ARCHIVE:-build-aros/libnvpair-aros.a}
linked_object=${AROS_LINKED_OBJECT:-build-aros/full-engine.o}
test_source=${1:-aros/engine/engine-init-test.c}
adapter=()
wrap_flags=()
if [[ ${AROS_TEST_DOS_ALLOC:-0} == 1 ]];then
    case "$test_source" in
        aros/engine/qemu-pool-test.c|aros/engine/qemu-import-test.c|aros/engine/qemu-reopen-test.c) ;;
        *) echo 'DOS allocation injection is restricted to private mirror diagnostics' >&2;exit 2 ;;
    esac
    printf 'dos_allocation_injection=1\n' >"$output_dir/allocation-test-options.txt"
fi
case "$test_source" in
    aros/zpl/default-quota-test.c) output=OpenZFSDefaultQuotaTest; startup=(-nix); adapter=(aros/engine/zpl-fixture.c aros/engine/cred-test.c aros/ui/api.c aros/ui/broker.c aros/engine/dos-handler.c aros/engine/properties.c aros/engine/management.c) ;;
    aros/engine/engine-init-test.c) output=OpenZFSEngineInitTest; startup=() ;;
    aros/engine/pool-test.c) output=OpenZFSPoolTest; startup=(-nix); adapter=(aros/engine/zpl-fixture.c) ;;
    aros/engine/qemu-pool-test.c) output=OpenZFSQEMUPoolTest; startup=(-nix); adapter=(aros/engine/zpl-fixture.c aros/engine/dos-handler.c aros/engine/dos-handler-test.c aros/engine/uio-test.c aros/zpl/dio-test.c aros/zpl/buffered-test.c aros/engine/cred-test.c aros/zpl/fuid-test.c aros/zpl/timestamps-test.c aros/zpl/atime-persistence-test.c) ;;
    aros/engine/qemu-import-test.c) output=OpenZFSQEMUImportTest; startup=(-nix); adapter=(aros/engine/zpl-fixture.c aros/engine/dos-handler.c aros/engine/dos-handler-test.c aros/zpl/atime-persistence-test.c aros/zpl/dio-test.c) ;;
    aros/engine/snapshot-read-test.c) output=OpenZFSSnapshotReadTest; startup=(-nix) ;;
    aros/engine/raid-read-test.c) output=OpenZFSRAIDReadTest; startup=(-nix); adapter=(aros/engine/zpl-fixture.c aros/engine/dos-handler.c) ;;
    aros/engine/qemu-reopen-test.c) output=OpenZFSQEMUReopenTest; startup=(-nix); adapter=(aros/engine/zpl-fixture.c) ;;
    aros/engine/qemu-management-test.c) output=OpenZFSQEMUManagementTest; startup=(-nix); adapter=(aros/ui/api.c aros/ui/broker.c aros/engine/zpl-fixture.c aros/engine/dos-handler.c aros/engine/properties.c aros/engine/management.c) ;;
    aros/engine/qemu-crash-test.c) output=OpenZFSQEMUCrashTest; startup=(-nix); adapter=(aros/engine/zpl-fixture.c aros/zpl/create-fs.c); wrap_flags=(--wrap=zil_replay_checked --wrap=zfs_file_pwrite) ;;
    aros/ui/service.c) output=OpenZFSServiceNative; startup=(-nix); adapter=(aros/ui/api.c aros/ui/broker.c aros/engine/zpl-fixture.c aros/engine/dos-handler.c aros/engine/properties.c aros/engine/management.c) ;;
    aros/engine/admission-service-harness.c) output=OpenZFSAdmissionServiceHarness; startup=(-nix); adapter=(aros/ui/api.c aros/ui/broker.c aros/engine/zpl-fixture.c aros/engine/dos-handler.c aros/engine/properties.c aros/engine/management.c) ;;
    *) echo 'Unknown engine test source' >&2; exit 2 ;;
esac
case "$test_source" in
    aros/engine/qemu-pool-test.c|aros/engine/qemu-import-test.c|aros/engine/qemu-reopen-test.c)
        adapter+=(aros/engine/legacy-cold-test.c) ;;
esac
if [[ ${AROS_TEST_RUNTIME_FLUSH:-0} == 1 ]];then
    [[ $test_source == aros/engine/qemu-pool-test.c ]] || {
        echo 'Runtime flush injection requires private pool diagnostic' >&2;exit 2;
    }
    adapter+=(aros/engine/runtime-flush-test.c)
    wrap_flags+=(--wrap=zfs_file_fsync --wrap=oz_async_config_suspend_test)
fi
if [[ ${AROS_TEST_TXG_FLUSH_VERIFY:-0} == 1 ]];then
    case "$test_source" in
        aros/engine/qemu-import-test.c|aros/engine/qemu-reopen-test.c) ;;
        *) echo 'Cold flush verification requires private import diagnostic' >&2;exit 2 ;;
    esac
    adapter+=(aros/engine/runtime-flush-cold-test.c)
    wrap_flags+=(--wrap=spa_import)
fi
if [[ $test_source == aros/engine/qemu-pool-test.c || $test_source == aros/engine/qemu-import-test.c ]];then
    adapter+=(aros/engine/special-file-test.c)
    if [[ ${AROS_TEST_DOS_ALLOC:-0} == 1 ]];then
        adapter+=(aros/engine/dos-allocation-test.c)
    fi
fi
case "$test_source" in
    aros/engine/qemu-import-test.c|aros/engine/qemu-reopen-test.c)
        adapter+=(aros/zpl/create-fs.c) ;;
esac
if [[ ${AROS_TEST_ERROR_LOG_FIXTURE:-0} == 1 ]];then
    [[ $test_source == aros/ui/service.c ]] || exit 2
    adapter+=(aros/engine/error-log-test.c)
    wrap_flags+=(--wrap=spa_get_errlog)
fi
if [[ ${AROS_TEST_FAILED_MOUNT:-0} == 1 ]];then
    [[ $test_source == aros/engine/qemu-pool-test.c ]] || exit 2
    adapter+=(aros/engine/failed-mount-test.c)
    wrap_flags+=(--wrap=zfs_zget --wrap=oz_read_mount_open_dataset --wrap=oz_zpl_fixture_create)
fi
if [[ ${AROS_TEST_DOS_PUBLICATION_FAILURE:-0} == 1 ]];then
    [[ $test_source == aros/ui/service.c ]] || exit 2
    adapter+=(aros/engine/dos-publication-failure-test.c)
    wrap_flags+=(--wrap=oz_dos_mount_shared --wrap=oz_zpl_close)
fi
if [[ ${AROS_TEST_MOUNT_REGISTRY_ALLOC:-0} == 1 ]];then
    [[ $test_source == aros/ui/service.c ]] || {
        echo 'Mount-registry allocation injection requires a private service build' >&2;exit 2;
    }
    printf 'mount_registry_allocation_injection=1\n' >"$output_dir/mount-registry-allocation-test-options.txt"
    adapter+=(aros/engine/mount-registry-allocation-test.c)
    wrap_flags+=(--wrap=oz_mount_registry_calloc --wrap=oz_mount_registry_realloc)
fi
if [[ ${AROS_TEST_SPLIT_RETAINED:-0} == 1 ]];then
    [[ $test_source == aros/ui/service.c ]] || { echo 'Split injection requires private service build' >&2; exit 2; }
    adapter+=(aros/engine/split-retained-test.c)
    wrap_flags+=(--wrap=spa_vdev_split_mirror)
fi
if [[ ${AROS_TEST_WAIT_PENDING:-0} == 1 ]];then
    [[ $test_source == aros/ui/service.c ]] || { echo 'Wait injection requires private service build' >&2; exit 2; }
    adapter+=(aros/engine/wait-pending-test.c)
    wrap_flags+=(--wrap=spa_scan_range --wrap=spa_scan_stop --wrap=spa_wait --wrap=oz_read_mount_remove --wrap=dsl_dir_wait --wrap=oz_mount_unpublish --wrap=zfs_suspend_fs)
fi
if [[ ${AROS_TEST_RAW_MOUNT_TRACE:-0} == 1 ]];then
    [[ $test_source == aros/ui/service.c ]] || { echo 'Raw mount tracing requires private service build' >&2; exit 2; }
    adapter+=(aros/engine/raw-mount-test.c)
    wrap_flags+=(--wrap=dmu_objset_own --wrap=zil_replay_checked)
fi
adapter+=(aros/cli/device-path.c)
# Keep the real upstream GPT implementation and its native transport together
# in the engine. Public destructive admission is separate; no success stubs
# or discarded unresolved sections stand in for these operations.
adapter+=(lib/libefi/rdwr_efi.c aros/engine/efi-native.c aros/engine/efi-uuid.c aros/engine/efi-label.c)
adapter+=(aros/zpl/default-quota.c aros/zpl/temporary-hold.c aros/zpl/set-version.c aros/engine/caller.c aros/engine/file-scope.c aros/platform/cpu.c aros/platform/partitions.c aros/platform/storage-inventory.c aros/engine/partition-io.c)
if [[ $test_source == aros/zpl/default-quota-test.c ]]; then
    adapter+=(aros/engine/caller-test.c)
fi
if [[ $test_source == aros/engine/pool-test.c && ${AROS_TEST_DISCOVERY:-0} == 1 ]];then
    adapter+=(aros/engine/dos-handler.c aros/engine/dos-handler-test.c)
fi
if [[ ${AROS_IMPORT_TRACE:-0} == 1 ]];then
    adapter+=(aros/engine/import-trace.c)
    wrap_flags+=(--wrap=__zfs_dbgmsg)
fi
if [[ ${AROS_IMPORT_DISCOVERY_LINK:-0} == 1 || $test_source == aros/engine/qemu-pool-test.c || $test_source == aros/engine/qemu-management-test.c || $test_source == aros/ui/service.c || $test_source == aros/engine/admission-service-harness.c || $test_source == aros/zpl/default-quota-test.c ]];then
    # Link the entire upstream discovery implementation, not selected sections.
    # This gate does not expose an unverified public import operation.
    adapter+=(lib/libzutil/zutil_import.c lib/libtpool/thread_pool.c
        aros/engine/import-platform.c aros/cli/import-files.c)
fi
if [[ $test_source == aros/engine/qemu-management-test.c || $test_source == aros/ui/service.c || $test_source == aros/engine/admission-service-harness.c || $test_source == aros/zpl/default-quota-test.c ]]; then
    adapter+=(aros/engine/replication.c aros/engine/stream-file.c aros/engine/stream-output.c)
    adapter+=(aros/engine/status.c aros/engine/scan.c aros/engine/pool-create.c aros/engine/pool-import.c aros/engine/pool-split.c aros/engine/pool-clear.c aros/engine/pool-initialize.c aros/engine/pool-trim.c aros/engine/split-config.c aros/engine/history.c aros/zpl/create-fs.c aros/engine/admission.c)
fi
if [[ $test_source == aros/engine/admission-service-harness.c ]]; then
    adapter+=(aros/engine/resilver-test.c)
    adapter+=(aros/engine/send-receive-test.c)
    adapter+=(aros/engine/encryption-test.c)
    adapter+=(aros/engine/replication-perms-test.c)
    adapter+=(aros/engine/dos-stream-test.c)
    adapter+=(aros/engine/packet-cred-test.c)
    adapter+=(aros/zpl/timestamp-policy-test.c aros/engine/exec-policy-test.c aros/engine/vdev-policy-test.c aros/engine/pool-clear-test.c aros/engine/pool-initialize-test.c aros/engine/pool-trim-test.c)
fi
if [[ $test_source == aros/engine/qemu-pool-test.c || $test_source == aros/engine/qemu-import-test.c ]]; then
    wrap_flags+=(--wrap=sa_lookup --wrap=zil_commit)
    adapter+=(aros/engine/mount-lifecycle-test.c)
    adapter+=(aros/zpl/write-persistence-test.c aros/zpl/dirlock-test.c aros/zpl/link-test.c aros/zpl/mknode-test.c aros/zpl/symlink-test.c aros/zpl/create-test.c aros/zpl/xattr-dir-test.c aros/zpl/rmdir-test.c aros/zpl/remove-test.c aros/zpl/rename-test.c aros/zpl/setattr-test.c)
fi
if [[ $test_source == aros/engine/qemu-pool-test.c || $test_source == aros/engine/qemu-import-test.c || $test_source == aros/engine/qemu-reopen-test.c || $test_source == aros/engine/qemu-crash-test.c ]]; then
    # vdev property setters now call the real retained-binding import adapter.
    adapter+=(aros/engine/pool-import.c)
fi
if [[ $test_source == aros/engine/qemu-import-test.c || $test_source == aros/engine/qemu-reopen-test.c || $test_source == aros/engine/qemu-crash-test.c ]]; then
    # The whole import adapter retains its discovery, identity, cache-policy
    # and file-lock dependencies even when this diagnostic only imports an
    # already-packed configuration. Keep the same real implementation.
    if [[ ${AROS_IMPORT_DISCOVERY_LINK:-0} != 1 ]]; then
        adapter+=(lib/libzutil/zutil_import.c lib/libtpool/thread_pool.c
            aros/engine/import-platform.c aros/cli/import-files.c)
    fi
    adapter+=(aros/engine/cache-policy.c aros/engine/stream-file.c
        aros/engine/stream-output.c aros/ui/api.c)
fi
if [[ $test_source == aros/engine/qemu-pool-test.c ]]; then
    adapter+=(aros/engine/vdev-policy-test.c)
    adapter+=(aros/engine/async-config-test.c)
    wrap_flags+=(--wrap=txg_wait_synced --wrap=txg_wait_synced_flags)
    # Standalone diagnostic only: optional worker allocation failure/recovery.
    wrap_flags+=(--wrap=zk_thread_create_optional)
    adapter+=(aros/engine/import-topology-test.c)
    adapter+=(aros/engine/stream-file.c aros/engine/stream-output.c)
    adapter+=(aros/engine/packet-cred-test.c)
    adapter+=(aros/engine/delegation-test.c aros/zpl/create-fs.c aros/engine/admission.c aros/ui/api.c aros/ui/admission-client.c aros/engine/admission-test.c)
fi
# Admission now dispatches real user-property transactions as well as create.
# Every admission-bearing executable needs the property adapter, not just the
# management service cases above. Keep one object per source in the final link.
if [[ " ${adapter[*]} " == *" aros/engine/admission.c "* &&
      " ${adapter[*]} " != *" aros/engine/properties.c "* ]]; then
    adapter+=(aros/engine/properties.c)
fi
if [[ " ${adapter[*]} " == *" aros/engine/properties.c "* &&
      " ${adapter[*]} " != *" aros/engine/management.c "* ]]; then
    # Standalone filesystem diagnostics also link property validation, which
    # calls the real cache policy even without the management service.
    adapter+=(aros/engine/cache-policy.c)
fi
objects=()
if [[ ${AROS_FULL_DISPATCHER:-0} == 1 ]]; then
    adapter+=(aros/engine/control-file.c aros/engine/ioctl-platform.c aros/engine/zvol-native.c aros/engine/ioctl-request.c aros/cli/stream-channel.c)
    # Link whole upstream units. Never add these beside the userspace fm/zvol
    # substitutes or hide unresolved OS adapters with garbage collection.
    for source in module/zfs/zfs_ioctl.c module/zfs/zfs_onexit.c module/zfs/zvol.c; do
        AROS_ZPL_BUILD=1 AROS_ENGINE_OBJECT_DIR="$object_dir/dispatcher" bash aros/engine/compile-object.sh "$source"
        objects+=("$object_dir/dispatcher/${source//\//_}.o")
    done
fi
if [[ " ${adapter[*]} " == *" aros/engine/admission.c "* ]]; then
    # Use actual upstream state/callbacks for native sessions. The existing
    # libzpool FD/minor diagnostic path is separate and is NOT a CLI transport.
    if [[ ${AROS_FULL_DISPATCHER:-0} != 1 ]]; then
        AROS_ZPL_BUILD=1 AROS_ONEXIT_STATE_ONLY=1 bash aros/engine/compile-object.sh module/zfs/zfs_onexit.c
        objects+=("$object_dir/module_zfs_zfs_onexit.c.o")
    fi
    AROS_ZPL_BUILD=1 bash aros/engine/compile-object.sh aros/cli/session-cleanup.c
    objects+=("$object_dir/aros_cli_session-cleanup.c.o")
fi
crypto_lib=()
if [[ " ${adapter[*]} " == *" aros/engine/management.c "* ]]; then
    adapter+=(aros/engine/mount-registry-allocation.c)
    adapter+=(aros/engine/cache-policy.c aros/engine/hostid-policy.c aros/engine/hostid-record.c)
    crypto_include=${AROS_CRYPTO_INCLUDE:-/opt/aros-workspace/chromium-aros/src/third_party/boringssl/src/include}
    crypto_archive=${AROS_CRYPTO_ARCHIVE:-/opt/aros-workspace/chromium-aros/src/out/aros-chrome/obj/third_party/boringssl/libboringssl.a}
    "$cc" "${link_flags[@]}" -I"$crypto_include" -O2 -g -Wall -Wextra \
        -c aros/engine/passphrase.c -o "$object_dir/aros_engine_passphrase.c.o"
    objects+=("$object_dir/aros_engine_passphrase.c.o")
    crypto_lib+=("$crypto_archive")
    sha256sum aros/engine/passphrase.c aros/engine/passphrase.h \
        "$crypto_include/openssl/evp.h" "$crypto_archive" >"$output_dir/crypto-inputs.sha256"
    if file "$crypto_archive" | rg -q 'thin archive'; then
        while IFS= read -r member; do sha256sum "$member"; done < <(ar t "$crypto_archive") \
            >"$output_dir/crypto-members.sha256"
    else
        sha256sum "$crypto_archive" >"$output_dir/crypto-members.sha256"
    fi
fi
while read -r source; do
    if [[ $source == module/zfs/dmu_send.c || $source == module/zfs/zil.c || $source == module/zfs/dsl_dir.c || $source == module/zfs/spa.c || $source == module/zfs/spa_config.c || $source == module/zfs/spa_misc.c || $source == lib/libzpool/kernel.c || $source == lib/libzpool/taskq.c || $source == module/zfs/vdev_file.c || $source == module/zfs/vdev.c ]]; then
        # Include checked replay and the current native device adapter,
        # not stale engine objects from the initial upstream build.
        bash aros/engine/compile-object.sh "$source"
    fi
    if [[ ${AROS_FULL_DISPATCHER:-0} == 1 && ( $source == module/zfs/fm.c || $source == module/zfs/zfs_fm.c ) ]]; then
        AROS_ZPL_BUILD=1 AROS_ENGINE_OBJECT_DIR="$object_dir/dispatcher" bash aros/engine/compile-object.sh "$source"
        objects+=("$object_dir/dispatcher/${source//\//_}.o")
    elif [[ $source == module/zfs/spa_errlog.c || $source == module/zfs/zfs_fuid.c || $source == module/zfs/dmu_direct.c || $source == module/zfs/dmu.c || $source == module/zfs/sa.c || $source == module/zfs/zfs_sa.c || $source == module/zfs/zfs_znode.c || $source == module/zfs/zfs_byteswap.c ]]; then
        # Integrate this complete upstream ZPL unit, not two overlapping
        # libzpool/kernel versions or a garbage-collected subset.
        AROS_ZPL_BUILD=1 AROS_ENGINE_OBJECT_DIR="$object_dir/zpl" bash aros/engine/compile-object.sh "$source"
        objects+=("$object_dir/zpl/${source//\//_}.o")
    else
        objects+=("$object_dir/${source//\//_}.o")
    fi
done < <(awk '/^[[:space:]]+[^#]*\.c([[:space:]]|$)/ {gsub(/%D%/,"lib/libzpool",$1);print $1}' lib/libzpool/Makefile.am)
for source in module/zfs/zfs_log.c module/zfs/zfs_vnops.c module/zfs/zfs_quota.c module/zfs/zfs_replay.c; do
    AROS_ZPL_BUILD=1 AROS_ENGINE_OBJECT_DIR="$object_dir/zpl" bash aros/engine/compile-object.sh "$source"
    objects+=("$object_dir/zpl/${source//\//_}.o")
done
if [[ ${AROS_ZPL_AUDIT:-0} == 1 ]]; then
    # Diagnostic output name only. Common vnode I/O/quota/log units now also
    # serve normal builds; OS namespace/mapping integration remains separate.
    # Never drop unresolved sections or install this audit candidate.
    [[ $output == OpenZFSEngineInitTest ]] || { echo 'ZPL audit requires engine-init-test' >&2; exit 2; }
    output=OpenZFSZPLLinkAudit
    # Full replay translation unit is now included in normal builds too.
    # Runtime coverage still requires record-specific crash tests.
fi
while read -r source; do
    objects+=("$object_dir/${source//\//_}.o")
done < <(awk '/^[[:space:]]+module\/.*\.(c|S)([[:space:]]|$)/ {print $1}' \
    lib/libicp/Makefile.am lib/libavl/Makefile.am lib/libunicode/Makefile.am lib/libzstd/Makefile.am | \
    awk '!/asm-(aarch64|arm|ppc64)\//')
for source in aros/engine/ioctl-copy.c lib/libspl/atomic.c lib/libspl/getexecname.c lib/libspl/list.c \
    lib/libspl/page.c lib/libspl/strlcat.c lib/libspl/strlcpy.c lib/libspl/tunables.c \
    lib/libzutil/zutil_device_path.c lib/libzutil/zutil_pool.c lib/libzutil/zutil_nicenum.c \
    aros/zpl/setattr.c aros/zpl/rename.c aros/zpl/remove.c aros/zpl/rmdir.c aros/zpl/unlinked.c aros/zpl/xattr-dir.c aros/zpl/create.c aros/zpl/symlink.c aros/zpl/mkdir.c aros/zpl/mknode.c aros/zpl/dir-mutation.c aros/zpl/dirlock.c aros/zpl/space.c aros/zpl/write-simple.c aros/zpl/mapping-policy.c aros/zpl/dataset-stats.c aros/zpl/acl-core.c aros/zpl/acl-core-test.c aros/zpl/acl-storage-test.c \
    aros/zpl/native-protection-test.c aros/zpl/native-protection.c aros/zpl/xattr.c aros/zpl/xvattr.c aros/zpl/znode-cache.c aros/zpl/znode-cache-test.c aros/zpl/read-mount.c \
    aros/zpl/legacy-znode-test.c aros/zpl/online-receive.c aros/engine/packet-cred.c \
    aros/engine/vdev-policy.c aros/engine/spl-platform.c aros/engine/device-io.c aros/engine/uio.c aros/engine/abd-pages.c aros/engine/layout-check.c aros/zpl/layout-test.c aros/engine/cred.c aros/engine/idmap.c aros/zpl/timestamps.c aros/zpl/mount-state.c aros/zpl/blocksize.c aros/zpl/file-limits.c aros/zpl/file-limits-test.c "${adapter[@]}" "$test_source"; do
    if [[ $source == aros/zpl/* || $source == aros/engine/failed-mount-test.c || $source == aros/engine/wait-pending-test.c || $source == aros/engine/special-file-test.c || $source == aros/engine/legacy-cold-test.c || $source == aros/engine/control-file.c || $source == aros/engine/ioctl-platform.c || $source == aros/engine/zvol-native.c || $source == aros/engine/ioctl-request.c ]]; then
        AROS_ZPL_BUILD=1 bash aros/engine/compile-object.sh "$source"
    else
        bash aros/engine/compile-object.sh "$source"
    fi
    objects+=("$object_dir/${source//\//_}.o")
done
# Keep the whole upstream engine in this diagnostic. No --gc-sections hiding
# unresolved engine paths; final packaging as a shared service is separate.
"${cc%gcc}ld" -r "${wrap_flags[@]}" -T aros/engine/tunables.ld "${objects[@]}" -o "$linked_object"
pthread_lib=(-lpthread)
if [[ -n ${AROS_PRIVATE_PTHREAD:-} ]];then pthread_lib=("$AROS_PRIVATE_PTHREAD");fi
if [[ $test_source == aros/engine/pool-test.c ]];then pthread_lib+=(-ldos64);fi
"$cc" "${link_flags[@]}" "${startup[@]}" "$linked_object" "$nvpair" "$zlib_import" "${crypto_lib[@]}" "${pthread_lib[@]}" -lm \
    -o "$output_dir/$output"
