#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
out=${1:?output directory required}
mkdir -p "$out"
flags=()
if [[ ${AROS_ZVOL_DEVICE_DIAGNOSTICS:-0} == 1 ]];then flags+=(-DOZ_DEVICE_TRACE);fi
if [[ ${AROS_TEST_ZVOL_DEVICE_RACE:-0} == 1 ]];then flags+=(-DOZ_DEVICE_STARTUP_TEST);fi
printf 'diagnostics=%s\n' "${AROS_ZVOL_DEVICE_DIAGNOSTICS:-0}" >"$out/build-options.txt"
printf 'startup-test=%s\n' "${AROS_TEST_ZVOL_DEVICE_RACE:-0}" >>"$out/build-options.txt"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra \
    "${flags[@]}" aros/device/zvol-device.c aros/ui/admission-client.c -o "$out/OpenZFSDevice"
sha256sum aros/device/zvol-device.c aros/device/mapping.h aros/device/mountfile.h aros/ui/admission-client.c aros/ui/zvol-api.h "$out/OpenZFSDevice" >"$out/inputs.sha256"
