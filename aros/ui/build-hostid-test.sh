#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
: "${AROS_SDK:?Set AROS_SDK to the SDK under test}"
: "${AROS_CC:?Set AROS_CC to the cross compiler}"
output=${1:?Supply an output path on the artifact drive}
mkdir -p "$(dirname "$output")"
sources=(aros/ui/hostid-native-test.c aros/ui/hostid-settings.c
    aros/engine/hostid-record.c aros/ui/preferences.c aros/ui/preferences-file.c)
"$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" \
    -Wall -Wextra -Werror -O2 "${sources[@]}" -liffparse -o "$output"
sha256sum "${sources[@]}" aros/ui/hostid-settings.h aros/engine/hostid-record.h \
    aros/ui/settings-file.h "$output" > "$output.inputs.sha256"
