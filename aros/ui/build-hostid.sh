#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
out=$(mktemp -d "$root/hostid-tool.XXXXXXXX")
echo "Output: $out"
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
sources=(aros/ui/hostid-cli.c aros/ui/hostid-settings.c aros/engine/hostid-record.c
    aros/ui/preferences.c aros/ui/preferences-file.c)
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -Wall -Wextra "${sources[@]}" -liffparse \
    -o "$out/OpenZFSHostIdentity" >"$out/build.log" 2>&1
sha256sum "${sources[@]}" aros/ui/hostid-settings.h aros/engine/hostid-record.h \
    aros/ui/settings-file.h "$out/OpenZFSHostIdentity" >"$out/inputs.sha256"
git rev-parse HEAD >"$out/source-head.txt"
git diff --binary >"$out/source.patch"
echo "PASS native OpenZFSHostIdentity build"
