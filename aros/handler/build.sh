#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
gen=${AROS_GENMODULE:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/linux-x86_64/tools/genmodule}
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
out=$(mktemp -d "$artifact_root/handler-build.XXXXXXXX")
echo "Handler: $out"
mkdir -p "$out/gen"
for operation in writefiles writelibdefs;do
    "$gen" -c aros/handler/openzfsboot.conf -d "$out/gen" "$operation" openzfsboot handler
done
flags=(--sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra -I"$out/gen")
"$cc" "${flags[@]}" -c "$out/gen/openzfsboot_start.c" -o "$out/start.o"
"$cc" "${flags[@]}" -c "$out/gen/openzfsboot_end.c" -o "$out/end.o"
"$cc" "${flags[@]}" -ffreestanding -fno-builtin -c aros/handler/handler.c -o "$out/handler.o"
# Bootstrap must not acquire accidental libc/CRT requirements.
if [[ -n $(nm -u "$out/handler.o") ]];then
    nm -u "$out/handler.o" >&2
    echo 'Unexpected external dependency in native handler' >&2;exit 2
fi
"$cc" "${flags[@]}" -nostartfiles "$out/start.o" "$out/handler.o" "$out/end.o" -o "$out/OpenZFS-Handler"
cp aros/handler/* "$out/"
git rev-parse HEAD >"$out/source-head.txt"
git diff --binary >"$out/source.patch"
sha256sum "$out/OpenZFS-Handler" aros/ui/handler-api.h >"$out/inputs.sha256"
sha256sum "$out/OpenZFS-Handler" >"$out/handler.sha256"
