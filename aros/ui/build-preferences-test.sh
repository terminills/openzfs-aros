#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra \
    aros/ui/preferences.c aros/ui/preferences-file.c aros/ui/preferences-test.c \
    aros/ui/startup-settings.c aros/ui/startup-settings-file.c aros/ui/startup-settings-test.c -liffparse \
    -o build-aros/OpenZFSPreferencesTest
sha256sum aros/ui/preferences.c aros/ui/preferences-file.c aros/ui/preferences.h aros/ui/preferences-test.c \
    aros/ui/startup-settings.c aros/ui/startup-settings.h aros/ui/startup-settings-test.c \
    aros/ui/startup-settings-file.c aros/ui/settings-file.h \
    "$sdk/include/proto/iffparse.h" "$sdk/include/libraries/iffparse.h" \
    build-aros/OpenZFSPreferencesTest >build-aros/preferences-build-inputs.sha256
