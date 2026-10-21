#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
bash aros/ui/build-manager-native.sh build-aros/typed-import-service/OpenZFSManager
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra \
    aros/ui/preferences.c aros/ui/preferences-file.c aros/ui/preferences-gui-test.c \
    aros/ui/startup-settings.c aros/ui/startup-settings-file.c \
    -liffparse -o build-aros/OpenZFSPreferencesGuiTest
sha256sum aros/ui/manager.c aros/ui/preferences*.c aros/ui/preferences.h \
    aros/ui/startup-settings*.c aros/ui/startup-settings.h aros/ui/startup-editor.h aros/ui/settings-file.h \
    build-aros/typed-import-service/OpenZFSManager build-aros/OpenZFSPreferencesGuiTest \
    >build-aros/preferences-gui-build-inputs.sha256
