#!/usr/bin/env bash
# Private relink: never updates the shared OS source, objects, SDK or ISO.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
build=${AROS_PARTITION_BUILD:-/opt/aros-workspace/aros-amiwest-nightly-release}
source=${AROS_PARTITION_SOURCE:-/opt/aros-workspace/AROS-amiwest-nightly-src/rom/partition}
sdk="$build/bin/pc-x86_64-smp/AROS/Developer"
gen="$build/bin/pc-x86_64-smp/gen"
objects="$gen/rom/partition/partition"
cc=${AROS_CC:-/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc}
source "$repo/aros/engine/storage-preflight.sh"
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:?}")
out=$(mktemp -d "$root/partition-gpt.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
mkdir "$out/source" "$out/objects"
cp "$source"/*.[ch] "$out/source/"
cp "$objects"/*.o "$out/objects/"
cp "$repo/aros/platform/partition-gpt-gaps.patch" "$out/"
cp "$0" "$out/"
sha256sum "$source"/*.[ch] "$objects"/*.o "$objects/include/partition_deflibdefs.h" \
    "$sdk/../Libs/partition.library" >"$out/inputs.sha256"
patch --batch --fuzz=0 -d "$out/source" -p0 <"$out/partition-gpt-gaps.patch"
flags=(--sysroot "$sdk" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
"$cc" "${flags[@]}" -iquote "$out/source" -iquote "$build/rom" -O2 \
    -fno-asynchronous-unwind-tables -fno-omit-frame-pointer -Wno-pointer-sign -Wno-parentheses \
    -DAROS_BUILD_TYPE=AROS_BUILD_TYPE_PERSONAL -DAROS_USE_LOGRES -DUSE_EXEC_DEBUG -DDEBUG=0 \
    -I"$objects/include" -include "$objects/include/partition_deflibdefs.h" \
    -D__UTILITY_RELLIBBASE__ '-D__SRCFILENAME__="rom/partition/partitiongpt.c"' \
    -c "$out/source/partitiongpt.c" -o "$out/objects/partitiongpt.o"
parts=()
for object in "$out/objects"/*.o;do
    case "$object" in */partition_start.o|*/partition_end.o) continue;; esac
    parts+=("$object")
done
"$cc" "${flags[@]}" -nostartfiles -Wl,-Map,"$out/partition.map" \
    "$gen/compiler/libinit/libentry.o" "$out/objects/partition_start.o" \
    "${parts[@]}" "$out/objects/partition_end.o" -o "$out/partition.library" \
    -L"$gen/lib" -static -Wl,--start-group -ldebug -lloadseg -lfatbpb -lutility_rel -Wl,--end-group
sha256sum "$out/partition.library" >"$out/output.sha256"
python3 "$repo/aros/cli/package-environment-overlay.py" \
    "$sdk/../boot/aros-fs.pkg.xz" "$out" partition.library aros-fs
sha256sum -c "$out/inputs.sha256"
