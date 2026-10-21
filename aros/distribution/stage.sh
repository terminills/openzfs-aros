#!/usr/bin/env bash
# Build a new staging directory only. Never modify an installed AROS system.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
service=$(realpath -e "${1:?full native service build directory required}")
if ! rg -Fxq 'service_test_modes=0' "$service/service-build-options.txt";then
    echo 'Refusing service without diagnostic-modes-off provenance.' >&2;exit 2
fi
if ! rg -Fxq 'full_dispatcher=1' "$service/service-build-options.txt";then
    echo 'Refusing service without full dispatcher provenance.' >&2;exit 2
fi
if rg '^status_trace=' "$service/service-build-options.txt" | rg -vqx 'status_trace=0';then
    echo 'Refusing status-trace diagnostic build.' >&2;exit 2
fi
# Older diagnostic candidates predate the metadata field. Inspect their
# selected payload too; do not silently treat absent provenance as trace-off.
for trace_binary in "$service/OpenZFSServiceNative" "$service/openzfs.library";do
    if [[ -f $trace_binary ]] && strings "$trace_binary" | rg 'OpenZFS status stage=|OpenZFS async config namespace-enter' >/dev/null;then
        echo 'Refusing status-trace diagnostic build.' >&2;exit 2
    fi
done
if [[ ! -f $service/service-build.sha256 ]] ||
    ! read -r expected_service_hash _ <"$service/service-build.sha256";then
    echo 'Refusing service without readable build hash.' >&2;exit 2
fi
if [[ ! -f $service/OpenZFSServiceNative ]];then
    echo 'Refusing missing service binary.' >&2;exit 2
fi
actual_service_hash=$(sha256sum "$service/OpenZFSServiceNative")
if [[ ! $expected_service_hash =~ ^[0-9a-f]{64}$ || ${actual_service_hash%% *} != "$expected_service_hash" ]];then
    echo 'Refusing service binary/build hash mismatch.' >&2;exit 2
fi
engine="$service/OpenZFSServiceNative"
engine_files=()
if rg -Fxq 'shared_engine=1' "$service/service-build-options.txt";then
    engine="$service/openzfs.library"
    if [[ ! -f $service/engine-library.sha256 ]] ||
        ! read -r expected_engine_hash _ <"$service/engine-library.sha256";then
        echo 'Refusing shared engine without readable library hash.' >&2;exit 2
    fi
    if [[ ! -f $engine ]];then
        echo 'Refusing missing shared engine library.' >&2;exit 2
    fi
    actual_engine_hash=$(sha256sum "$engine")
    if [[ ! $expected_engine_hash =~ ^[0-9a-f]{64}$ || ${actual_engine_hash%% *} != "$expected_engine_hash" ]];then
        echo 'Refusing shared engine library/build hash mismatch.' >&2;exit 2
    fi
    engine_files+=("$engine")
fi
cli=$(realpath -e "${2:?whole upstream CLI link directory required}")
manager=$(realpath -e "${3:-$service/OpenZFSManagerNative}")
# build-manager-native.sh records the output binary last. Compare that hash
# with the selected binary, not an old absolute path in the build manifest.
if [[ ! -s "$manager.inputs.sha256" ]];then
    echo 'Refusing Manager without readable build manifest.' >&2;exit 2
fi
read -r expected_manager_hash manager_record < <(tail -n 1 "$manager.inputs.sha256") || {
    echo 'Refusing Manager without readable build manifest.' >&2;exit 2;
}
actual_manager_hash=$(sha256sum "$manager")
if [[ ! $expected_manager_hash =~ ^[0-9a-f]{64}$ || -z $manager_record ||
      ${actual_manager_hash%% *} != "$expected_manager_hash" ]];then
    echo 'Refusing Manager binary/build hash mismatch.' >&2;exit 2
fi
device=${4:-}
handler=${5:-}
handler_files=()
if [[ -n $handler ]];then
    handler=$(realpath -e "$handler")
    if [[ ! -f $handler/handler.sha256 ]] || ! read -r expected_handler_hash _ <"$handler/handler.sha256";then
        echo 'Refusing handler without readable build hash.' >&2;exit 2
    fi
    if [[ ! -f $handler/OpenZFS-Handler ]];then
        echo 'Refusing missing handler binary.' >&2;exit 2
    fi
    actual_handler_hash=$(sha256sum "$handler/OpenZFS-Handler")
    if [[ ! $expected_handler_hash =~ ^[0-9a-f]{64}$ || ${actual_handler_hash%% *} != "$expected_handler_hash" ]];then
        echo 'Refusing handler binary/build hash mismatch.' >&2;exit 2
    fi
    handler_symbols=$(nm "$handler/OpenZFS-Handler")
    [[ $handler_symbols == *' T OpenZFSBoot_Handler'* && $handler_symbols == *' T openzfs_handler'* ]]
    if [[ $handler_symbols == *'StdCBase'* || $handler_symbols == *'__wrap_'* ||
          $handler_symbols == *' T zfsdev_ioctl_common'* ]];then
        echo 'Refusing CRT-dependent, instrumented or engine-embedded bootstrap.' >&2;exit 2
    fi
    handler_files+=("$handler/OpenZFS-Handler")
fi
device_files=()
if [[ -n $device ]];then
    device=$(realpath -e "$device")
    rg -Fxq 'diagnostics=0' "$device/build-options.txt"
    rg -Fxq 'startup-test=0' "$device/build-options.txt"
    if strings "$device/OpenZFSDevice" | rg 'OpenZFSDevice.StartupTest|ZDEV begin' >/dev/null;then
        echo 'Refusing traced or startup-barrier device binary.' >&2;exit 2
    fi
    device_files+=("$device/OpenZFSDevice")
fi
commands=(zfs zpool OpenZFSStartupMount OpenZFSStartup OpenZFSRuntimeCheck OpenZFSHostIdentity)
cli_files=()
for name in "${commands[@]}"; do
    rg -Fxq "PASS $name link" "$cli/summary.txt"
    cli_files+=("$cli/$name")
done
rg -Fxq 'import_trace=0' "$cli/build-options.txt"
rg -Fxq 'import_tiny_reply=0' "$cli/build-options.txt"
symbols=$(nm "$engine")
# Diagnostic linker wrappers can change successful upstream operations into
# injected failures. Never ship one merely because the CLI itself is clean.
if [[ $symbols == *'__wrap_'* || $symbols == *'oz_test_admission_hold'* ||
      $symbols == *'oz_dos_test_calloc'* ]];then
    echo 'Refusing diagnostic/fault-injected native service (linker wrapper or queue test hook present).' >&2
    exit 2
fi
for symbol in zfsdev_ioctl_common oz_pool_change_guid; do
    [[ $symbols == *" T $symbol"* ]]
done
if [[ -n $handler ]];then [[ $symbols == *' T oz_admission_bind_handler'* ]];fi
if [[ ${#engine_files[@]} != 0 ]];then
    [[ $symbols == *' T OpenZFS_5_OpenZFSRunService'* && $symbols == *' T oz_service_main'* ]]
    launcher_symbols=$(nm "$service/OpenZFSServiceNative")
    if [[ $launcher_symbols == *'__wrap_'* || $launcher_symbols == *' T zfsdev_ioctl_common'* ||
          $launcher_symbols == *' T oz_engine_start'* ]];then
        echo 'Refusing instrumented or engine-embedded shared launcher.' >&2;exit 2
    fi
fi
if [[ -n $device ]];then
    for symbol in oz_zvol_open oz_zvol_io oz_zvol_info oz_zvol_flush oz_zvol_close;do
        [[ $symbols == *" T $symbol"* ]]
    done
fi
for binary in "$service/OpenZFSServiceNative" "$manager" "${cli_files[@]}" "${device_files[@]}" "${engine_files[@]}" "${handler_files[@]}"; do
    description=$(file -b "$binary")
    [[ $description == *'ELF 64-bit LSB relocatable, x86-64'* && $description == *'AROS'* ]]
done
out=$(mktemp -d "$artifact_root/native-bundle.XXXXXXXX")
bundle_dirs=(C Docs Metadata)
mkdir "$out/C" "$out/Docs" "$out/Metadata"
cp "$service/OpenZFSServiceNative" "${cli_files[@]}" "$out/C/"
if [[ ${#engine_files[@]} != 0 ]];then
    mkdir "$out/Libs"
    bundle_dirs+=(Libs)
    cp "$engine" "$out/Libs/openzfs.library"
    cp "$service/engine-library.sha256" "$service/engine-object.sha256" "$out/Metadata/"
    cp aros/library/README.md "$out/Docs/SHARED-ENGINE.md"
fi
cp "$manager" "$out/C/OpenZFSManagerNative"
if [[ -n $handler ]];then
    mkdir "$out/L";bundle_dirs+=(L)
    cp "$handler/OpenZFS-Handler" "$out/L/"
    cp "$handler/handler.sha256" "$out/Metadata/"
    cp aros/handler/README.md "$out/Docs/HANDLER.md"
fi
if [[ -n $device ]];then
    cp "$device/OpenZFSDevice" "$out/C/"
    cp "$device/build-options.txt" "$out/Metadata/device-build-options.txt"
    cp "$device/inputs.sha256" "$out/Metadata/device-inputs.sha256"
    cp aros/engine/ZVOL-DEVICE.md "$out/Docs/"
fi
cp aros/distribution/README.md "$out/Docs/README.md"
cp aros/ui/PREFERENCES.md "$out/Docs/"
cp aros/ui/REQUESTS.md "$out/Docs/"
cp aros/engine/ENGINE-POLICY.md "$out/Docs/"
cp aros/engine/EXEC-POLICY.md "$out/Docs/"
for guide in EVENTS REDACT PROGRAM DDTPRUNE DIFF LABELCLEAR PREFETCH UPGRADE \
    DATASET-UPGRADE DATASET-REWRITE WAIT IMPORT-ERRORS;do
    cp "aros/cli/$guide.md" "$out/Docs/"
done
cp LICENSE COPYRIGHT NOTICE "$out/Docs/"
cp "$cli/summary.txt" "$cli/build-options.txt" "$out/Metadata/"
# Retain build-time identity separately from packaging-time checkout identity.
# Older development CLI builds may not have all these provenance records.
for record in source-head.txt sources.sha256 inputs.sha256 thin-members.sha256;do
    if [[ -f "$cli/$record" ]];then
        cp "$cli/$record" "$out/Metadata/cli-$record"
    fi
done
cp "$service/engine-metadata.h" "$service/engine-metadata.sha256" "$out/Metadata/"
cp "$service/service-build-options.txt" "$service/service-build.sha256" "$out/Metadata/"
# Packaging-time checkout identity is not a claim about binary build identity.
git rev-parse HEAD >"$out/Metadata/packaging-head.txt"
git diff --binary >"$out/Metadata/packaging-worktree.patch"
sha256sum "$service/OpenZFSServiceNative" "$manager" "${cli_files[@]}" "${device_files[@]}" "${engine_files[@]}" "${handler_files[@]}" >"$out/Metadata/source-binaries.sha256"
if [[ -f "$manager.inputs.sha256" ]];then cp "$manager.inputs.sha256" "$out/Metadata/manager-inputs.sha256";fi
cp "$0" "$out/Metadata/stage.sh"
(
    cd "$out"
    find "${bundle_dirs[@]}" -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum >SHA256SUMS
    sha256sum -c SHA256SUMS
)
tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -C "$out" -cJf "$out.tar.xz" "${bundle_dirs[@]}" SHA256SUMS
sha256sum "$out.tar.xz" >"$out.tar.xz.sha256"
printf 'Bundle: %s\nArchive: %s.tar.xz\n' "$out" "$out"
