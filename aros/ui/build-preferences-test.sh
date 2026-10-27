#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
output=${1:-build-aros/OpenZFSPreferencesTest}
manifest="$output.inputs.sha256"
if (( $# == 0 )); then manifest=build-aros/preferences-build-inputs.sha256; fi
mkdir -p "$(dirname "$output")"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra -DOZ_SETTINGS_FAULT_TEST \
    aros/ui/preferences.c aros/ui/preferences-file.c aros/ui/preferences-test.c \
    aros/ui/startup-settings.c aros/ui/startup-settings-file.c aros/ui/startup-settings-test.c -liffparse \
    -o "$output"
sha256sum aros/ui/preferences.c aros/ui/preferences-file.c aros/ui/preferences.h aros/ui/preferences-test.c \
    aros/ui/startup-settings.c aros/ui/startup-settings.h aros/ui/startup-settings-test.c \
    aros/ui/startup-settings-file.c aros/ui/settings-file.h \
    "$sdk/include/proto/iffparse.h" "$sdk/include/libraries/iffparse.h" \
    "$output" >"$manifest"
