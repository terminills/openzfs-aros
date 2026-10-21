#!/usr/bin/env bash
# Compile an isolated AROS desktop consumer; never replace the shared runtime.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
sdk=${AROS_SDK:?explicit matching SDK required}
source=${AROS_DISKINFO_SOURCE:?DiskInfo source directory required}
generated=${AROS_DISKINFO_GENERATED:?matching generated DiskInfo headers required}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
test -f "$generated/strings.h"
test -f "$source/catalogs/catalog_version.h"
run=$(mktemp -d "$repo/build-aros/diskinfo-overlay.XXXXXXXX")
echo "Evidence: $run"
cp -a "$source" "$run/source"
cp "$generated/strings.h" "$run/source/strings.h"
sha256sum "$source"/{main,support,locale,diskinfo}.c "$generated/strings.h" \
    aros/patches/diskinfo-large-volume.patch >"$run/inputs.sha256"
patch --batch --fuzz=0 -d "$run/source" -p1 <aros/patches/diskinfo-large-volume.patch
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall \
    "-DADATE=\"$(date +%d.%m.%Y)\"" \
    -I"$run/source" "$run/source/"{main,support,locale,diskinfo}.c \
    -lmuimaster -lworkbench -licon -lintuition -lutility -llocale -larossupport \
    -o "$run/DiskInfo"
sha256sum "$run/DiskInfo" >"$run/output.sha256"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall \
    -I"$run/source" aros/engine/diskinfo-capacity-test.c \
    "$run/source/support.c" "$run/source/locale.c" \
    -lmuimaster -lintuition -lutility -llocale -o "$run/OpenZFSDiskInfoTest"
sha256sum aros/engine/diskinfo-capacity-test.c >>"$run/inputs.sha256"
sha256sum "$run/OpenZFSDiskInfoTest" >>"$run/output.sha256"
echo "PASS: private DiskInfo build $run/DiskInfo"
