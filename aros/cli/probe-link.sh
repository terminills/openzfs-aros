#!/usr/bin/env bash
# Link whole real commands. Missing native implementations must remain failures.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
probe=${1:?common compile probe required}
deps=${2:?init dependency probe required}
pipe=${3:?native pipe overlay or - for SDK-only imports required}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
pthread=${AROS_PTHREAD_BUILD:-$repo/build-aros/pthread-consistent.s3r1CHla}
case ${AROS_PTHREAD_BACKEND:-private} in
    sdk)
        [[ -z ${AROS_PTHREAD_BUILD:-}${AROS_PRIVATE_PTHREAD:-}${AROS_PTHREAD_INCLUDE:-} ]] || {
            echo 'SDK pthread mode cannot be combined with private pthread inputs' >&2; exit 1;
        }
        pthread_include="$sdk/include"
        pthread_archive="$sdk/lib/libpthread.a"
        ;;
    private)
        pthread_include="$pthread/source"
        pthread_archive="$pthread/libpthread.a"
        ;;
    *) echo 'Unsupported pthread backend' >&2; exit 1 ;;
esac
[[ -f $pthread_include/pthread.h && -f $pthread_archive ]]
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
if [[ $pipe == - ]];then
    python3 aros/platform/check-sdk-links.py --cc "$cc" --sdk "$sdk" \
        --output-parent "$artifact_root" --profile cli
fi
out=$(mktemp -d "$artifact_root/cli-link.XXXXXXXX")
echo "Evidence: $out"
cp "$0" "$out/"
git rev-parse HEAD > "$out/source-head.txt"
flags=(--sysroot="$sdk" -B"$sdk/lib/" -O2 -g -ffunction-sections -fdata-sections
    -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DOZ_NATIVE_LABEL_CLIENT -include "$repo/aros/engine/config.h"
    -I"$pthread_include" -I"$repo/aros/cli/include" -I"${AROS_GENERATED_INCLUDE:-$repo/build-aros/include}"
    -I"$repo/aros/engine/include" -I"$repo/lib/libspl/include"
    -I"$repo/lib/libspl/include/os/linux" -I"$repo/include" -I"$repo/include/os/linux/zfs")
native=()
if [[ ${AROS_CLI_IMPORT_TRACE:-0} == 1 ]];then flags+=(-DOZ_IMPORT_DIAGNOSTICS);fi
if [[ ${AROS_CLI_IMPORT_TINY_REPLY:-0} == 1 ]];then flags+=(-DOZ_TEST_IMPORT_TINY_REPLY);fi
history_flags=()
if [[ ${AROS_CLI_HISTORY_MEGARECORD:-0} == 1 ]];then
    [[ ${AROS_CLI_HISTORY_TEST:-0} == 1 ]]
    history_flags+=(-DOZ_TEST_HISTORY_MEGARECORD)
fi
printf 'import_trace=%s\nimport_tiny_reply=%s\nhistory_megarecord=%s\n' "${AROS_CLI_IMPORT_TRACE:-0}" \
    "${AROS_CLI_IMPORT_TINY_REPLY:-0}" "${AROS_CLI_HISTORY_MEGARECORD:-0}" >"$out/build-options.txt"
native_sources=(aros/cli/key-prompt.c aros/cli/gpt.c aros/cli/share-unavailable.c aros/cli/project.c aros/cli/rewrite.c aros/cli/stream-client.c aros/cli/stream-channel.c)
trace_flags=()
if [[ ${AROS_CLI_STARTUP_TRACE:-0} == 1 ]];then
    native_sources+=(aros/cli/startup-trace.c)
    trace_flags+=(-Wl,--wrap=main -Wl,--wrap=exit)
fi
for src in aros/cli/arguments.c aros/cli/process.c aros/cli/output.c aros/cli/identity.c \
    aros/cli/progress-wait.c aros/cli/title.c aros/cli/version.c aros/cli/lifecycle.c aros/cli/platform-hooks.c aros/cli/shares-object.c aros/cli/control-client.c \
    aros/ui/api.c aros/ui/admission-client.c aros/cli/import-files.c aros/cli/file-vdev.c aros/cli/device-path.c aros/cli/native-device.c aros/cli/device-wait.c aros/cli/native-label.c aros/platform/cpu.c \
    lib/libuutil/uu_list.c lib/libuutil/uu_string.c lib/libspl/list.c lib/libspl/page.c \
    lib/libnvpair/libnvpair.c lib/libnvpair/libnvpair_json.c lib/libspl/timestamp.c \
    module/zcommon/zfs_valstr.c lib/libtpool/thread_pool.c "${native_sources[@]}"; do
    stem=${src//\//_}
    sha256sum "$src" >> "$out/sources.sha256"
    "$cc" "${flags[@]}" -c "$src" -o "$out/$stem.o" > "$out/$stem.log" 2>&1
    native+=("$out/$stem.o")
done
pipe_objects=()
pipe_inputs=()
if [[ $pipe != - ]]; then
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" -c "$pipe/stubs/posixc_pipe2_stub.c" -o "$out/pipe2.o"
    pipe_objects+=("$out/pipe2.o")
    pipe_inputs+=("$pipe/posixc.library" "$pipe/stubs/posixc_pipe2_stub.c")
fi
printf 'pthread_backend=%s\npipe_overlay=%s\n' "${AROS_PTHREAD_BACKEND:-private}" "$pipe" > "$out/platform-inputs.txt"
common=("$probe"/lib_libzfs_libzfs_*.o "$probe"/lib_libzfs_core_*.o "$probe"/lib_libzutil_*.o
    "$probe/lib_libshare_libshare.c.o"
    "$deps"/module_*.o "$deps"/lib_libuutil_*.o
    "$deps/lib_libspl_atomic.c.o" "$deps/aros_cli_mount.c.o"
    "$deps/aros_cli_zone.c.o" "$deps/aros_cli_hostid.c.o"
    "${native[@]}" "${pipe_objects[@]}")
# The native streaming path changes the upstream core wrapper. Never silently
# select a cached core object from the original compile probe.
"$cc" "${flags[@]}" -I"${AROS_CRYPTO_INCLUDE:-/opt/aros-workspace/chromium-aros/src/third_party/boringssl/src/include}" \
    '-DTEXT_DOMAIN="zfs"' -include "$probe/metadata.h" -c lib/libzfs/libzfs_crypto.c \
    -o "$out/current-libzfs-crypto.o" >"$out/current-libzfs-crypto.log" 2>&1
sha256sum lib/libzfs/libzfs_crypto.c aros/cli/key-prompt.h >>"$out/sources.sha256"
crypto_replaced=0
for i in "${!common[@]}";do
    if [[ ${common[$i]} == "$probe/lib_libzfs_libzfs_crypto.c.o" ]];then
        common[$i]="$out/current-libzfs-crypto.o";crypto_replaced=$((crypto_replaced+1))
    fi
done
if ((crypto_replaced!=1));then echo 'Expected exactly one upstream crypto object' >&2;exit 1;fi
"$cc" "${flags[@]}" -c lib/libzfs_core/libzfs_core.c -o "$out/current-libzfs-core.o" > "$out/current-libzfs-core.log" 2>&1
sha256sum lib/libzfs_core/libzfs_core.c >> "$out/sources.sha256"
core_replaced=0
for i in "${!common[@]}"; do
    if [[ ${common[$i]} == "$probe/lib_libzfs_core_libzfs_core.c.o" ]];then
        common[$i]="$out/current-libzfs-core.o"
        core_replaced=$((core_replaced+1))
    fi
done
if ((core_replaced!=1));then echo 'Expected exactly one upstream core object' >&2;exit 1;fi
# Native policy errors must use the current reporter, not the cached probe.
"$cc" "${flags[@]}" '-DTEXT_DOMAIN="zfs"' '-DSYSCONFDIR="ENV:OpenZFS"' \
    '-DZFSEXECDIR="SYS:Tools/OpenZFS"' '-DPKGDATADIR="SYS:Storage/OpenZFS"' \
    -include "$probe/metadata.h" -c lib/libzfs/libzfs_util.c \
    -o "$out/current-libzfs-util.o" >"$out/current-libzfs-util.log" 2>&1
sha256sum lib/libzfs/libzfs_util.c >>"$out/sources.sha256"
util_replaced=0
for i in "${!common[@]}";do
    if [[ ${common[$i]} == "$probe/lib_libzfs_libzfs_util.c.o" ]];then
        common[$i]="$out/current-libzfs-util.o"
        util_replaced=$((util_replaced+1))
    fi
done
if ((util_replaced!=1));then echo 'Expected exactly one upstream util object' >&2;exit 1;fi
# Rebuild native host-identity diagnostics; do not reuse cached Linux guidance.
for src in lib/libzfs/libzfs_pool.c cmd/zpool/zpool_main.c;do
    stem=${src//\//_}
    "$cc" "${flags[@]}" '-DTEXT_DOMAIN="zfs"' '-DSYSCONFDIR="ENV:OpenZFS"' \
        '-DZFSEXECDIR="SYS:Tools/OpenZFS"' '-DPKGDATADIR="SYS:Storage/OpenZFS"' \
        -include "$probe/metadata.h" -c "$src" -o "$out/current-$stem.o" >"$out/current-$stem.log" 2>&1
    sha256sum "$src" >>"$out/sources.sha256"
done
pool_replaced=0
for i in "${!common[@]}";do
    if [[ ${common[$i]} == "$probe/lib_libzfs_libzfs_pool.c.o" ]];then
        common[$i]="$out/current-lib_libzfs_libzfs_pool.c.o"
        pool_replaced=$((pool_replaced+1))
    fi
done
if ((pool_replaced!=1));then echo 'Expected exactly one upstream pool object' >&2;exit 1;fi
"$cc" "${flags[@]}" '-DTEXT_DOMAIN="zfs"' -c lib/libzutil/zutil_import.c \
    -o "$out/current-zutil-import.o" >"$out/current-zutil-import.log" 2>&1
sha256sum lib/libzutil/zutil_import.c >>"$out/sources.sha256"
import_replaced=0
for i in "${!common[@]}";do
    if [[ ${common[$i]} == "$probe/lib_libzutil_zutil_import.c.o" ]];then
        common[$i]="$out/current-zutil-import.o"
        import_replaced=$((import_replaced+1))
    fi
done
if ((import_replaced!=1));then echo 'Expected exactly one upstream import object' >&2;exit 1;fi
archives=("${AROS_NVPAIR_ARCHIVE:-$repo/build-aros/libnvpair-adt-consistent.a}" "$pthread_archive"
    "$sdk/lib/libz.static.a"
    "${AROS_CRYPTO_ARCHIVE:-/opt/aros-workspace/chromium-aros/src/out/aros-chrome/obj/third_party/boringssl/libboringssl.a}")
sha256sum "${common[@]}" "${archives[@]}" "${pipe_inputs[@]}" "$probe/metadata.h" "$pthread_include/pthread.h" > "$out/inputs.sha256"
for archive in "${archives[@]}"; do
    if file "$archive" | grep -q 'thin archive'; then
        while IFS= read -r member; do sha256sum "$member"; done < <(ar t "$archive") \
            >> "$out/thin-members.sha256"
    fi
done
failed=0
# Path admission changes must not silently reuse the original probe object.
"$cc" "${flags[@]}" -c cmd/zpool/zpool_vdev.c -o "$out/current-zpool-vdev.o" > "$out/current-zpool-vdev.log" 2>&1
sha256sum cmd/zpool/zpool_vdev.c >> "$out/sources.sha256"
# Native device preflight references this upstream zpool helper. Preserve its
# real implementation for zfs too, without pulling in zpool's command main.
# Section selection is safe on this intermediate helper, never on final AROS
# startup/autoinit objects.
"${cc%gcc}ld" -r --gc-sections -u check_file_generic \
    "$out/current-zpool-vdev.o" -o "$out/check-file-generic.o"
sha256sum "$out/current-zpool-vdev.o" "$out/check-file-generic.o" >> "$out/inputs.sha256"
if [[ ${AROS_CLI_HISTORY_TEST:-0} == 1 ]];then
    "$cc" "${flags[@]}" "${history_flags[@]}" -c aros/cli/command-history-test.c -o "$out/command-history-test.o" >"$out/command-history-test.build.log" 2>&1
    sha256sum aros/cli/command-history-test.c "$out/command-history-test.o" >>"$out/inputs.sha256"
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" -nix "$out/command-history-test.o" "$out/check-file-generic.o" \
        "${common[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group \
        -o "$out/OpenZFSCommandHistoryTest" >"$out/command-history-test.link.log" 2>&1
    echo 'PASS OpenZFSCommandHistoryTest link' | tee -a "$out/summary.txt"
fi
"$cc" "${flags[@]}" -c aros/cli/startup-mount.c -o "$out/startup-mount.o" >"$out/startup-mount.log" 2>&1
sha256sum aros/cli/startup-mount.c >> "$out/sources.sha256"
"$cc" "${flags[@]}" -DOZ_STARTUP_MOUNT_LIBRARY -c aros/cli/startup-mount.c -o "$out/startup-mount-library.o" >"$out/startup-mount-library.log" 2>&1
startup_objects=("$out/startup-mount-library.o")
for src in aros/cli/startup.c aros/ui/startup-import.c aros/ui/startup-settings.c aros/ui/startup-settings-file.c aros/ui/preferences.c aros/ui/preferences-file.c;do
    stem=${src//\//_}
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -Wall -Wextra -c "$src" -o "$out/startup-$stem.o" >"$out/startup-$stem.log" 2>&1
    startup_objects+=("$out/startup-$stem.o")
    sha256sum "$src" >>"$out/sources.sha256"
done
"$cc" "${flags[@]}" -c aros/cli/runtime-check.c -o "$out/runtime-check.o" >"$out/runtime-check.log" 2>&1
sha256sum aros/cli/runtime-check.c >>"$out/sources.sha256"
for cmd in zfs zpool OpenZFSStartupMount OpenZFSStartup; do
    objects=("$probe"/cmd_"$cmd"_*.o)
    if [[ $cmd == zpool ]];then
        main_replaced=0
        for i in "${!objects[@]}";do
            if [[ ${objects[$i]} == "$probe/cmd_zpool_zpool_main.c.o" ]];then
                objects[$i]="$out/current-cmd_zpool_zpool_main.c.o"
                main_replaced=$((main_replaced+1))
            fi
            if [[ ${objects[$i]} == "$probe/cmd_zpool_zpool_vdev.c.o" ]];then
                objects[$i]="$out/current-zpool-vdev.o"
            fi
        done
        if ((main_replaced!=1));then echo 'Expected exactly one zpool main object' >&2;exit 1;fi
    fi
    extras=()
    if [[ $cmd == OpenZFSStartup ]];then objects=("${startup_objects[@]}" "$out/check-file-generic.o");extras=(-liffparse);fi
    if [[ $cmd == OpenZFSStartupMount ]];then objects=("$out/startup-mount.o" "$out/check-file-generic.o");fi
    if [[ $cmd == zfs ]];then objects+=("$out/check-file-generic.o");fi
    sha256sum "${objects[@]}" >> "$out/inputs.sha256"
    # AROS startup/autoinit tables are loader roots, not references from main.
    # ELF section GC here discards startup and can yield a silent no-op binary.
    if "$cc" --sysroot="$sdk" -B"$sdk/lib/" -nix "${trace_flags[@]}" -Wl,-Map,"$out/$cmd.map" \
        "${objects[@]}" "${common[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group "${extras[@]}" \
        -o "$out/$cmd" > "$out/$cmd.link.log" 2>&1; then
        echo "PASS $cmd link" | tee -a "$out/summary.txt"
    else
        echo "FAIL $cmd link: $out/$cmd.link.log" | tee -a "$out/summary.txt"
        failed=1
    fi
done
if "$cc" --sysroot="$sdk" -B"$sdk/lib/" -nix "$out/runtime-check.o" \
    "$out/aros_cli_process.c.o" "$out/aros_cli_arguments.c.o" "$out/aros_cli_output.c.o" \
    -o "$out/OpenZFSRuntimeCheck" >"$out/OpenZFSRuntimeCheck.link.log" 2>&1;then
    echo 'PASS OpenZFSRuntimeCheck link' | tee -a "$out/summary.txt"
else
    echo "FAIL OpenZFSRuntimeCheck link: $out/OpenZFSRuntimeCheck.link.log" | tee -a "$out/summary.txt"
    failed=1
fi
hostid_sources=(aros/ui/hostid-cli.c aros/ui/hostid-settings.c aros/engine/hostid-record.c
    aros/ui/preferences.c aros/ui/preferences-file.c)
sha256sum "${hostid_sources[@]}" aros/ui/hostid-settings.h aros/engine/hostid-record.h \
    aros/ui/settings-file.h >>"$out/sources.sha256"
if "$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -Wall -Wextra "${hostid_sources[@]}" \
    -liffparse -o "$out/OpenZFSHostIdentity" >"$out/OpenZFSHostIdentity.link.log" 2>&1;then
    sha256sum "$out/OpenZFSHostIdentity" >>"$out/inputs.sha256"
    echo 'PASS OpenZFSHostIdentity link' | tee -a "$out/summary.txt"
else
    echo "FAIL OpenZFSHostIdentity link: $out/OpenZFSHostIdentity.link.log" | tee -a "$out/summary.txt"
    failed=1
fi
exit "$failed"
