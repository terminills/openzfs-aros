#!/usr/bin/env bash
# Private FAT dependency overlay; never alter shared AROS artifacts.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
build=/opt/aros-workspace/aros-amiwest-nightly-release
source=/opt/aros-workspace/AROS-amiwest-nightly-src/rom/filesys/fat
sdk="$build/bin/pc-x86_64-smp/AROS/Developer"
gen="$build/bin/pc-x86_64-smp/gen"
objects="$gen/rom/filesys/fat/fat"
cc=/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc
out=$(mktemp -d "$repo/build-aros/fat-zvol-diagnostic.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
mkdir "$out/source" "$out/objects"
cp "$source"/*.[ch] "$out/source/"
cp "$objects"/*.o "$out/objects/"
cp "$0" "$out/"
sha256sum "$source"/*.[ch] "$objects"/*.o "$objects/include/fat_deflibdefs.h" "$sdk/lib/libfatbpb.a" >"$out/inputs.sha256"
units=(volume disk)
if [[ ${AROS_FAT_READONLY_FIX:-0} == 1 ]];then
    cp "$repo/aros/platform/fat-device-readonly.patch" "$out/"
    patch --batch --fuzz=0 -d "$out/source" -p0 <"$out/fat-device-readonly.patch"
    units+=(packet)
fi
if [[ ${AROS_FAT_DIAGNOSTICS:-1} == 1 ]];then
patch --batch --fuzz=0 -d "$out/source" -p0 <<'PATCH'
--- fat_fs.h
+++ fat_fs.h
@@ -25 +25 @@
-#define DEBUG_MISC          0
+#define DEBUG_MISC          1
PATCH
fi
flags=(--sysroot "$sdk" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
for unit in "${units[@]}";do
    "$cc" "${flags[@]}" -iquote "$out/source" -O2 -fno-asynchronous-unwind-tables \
        -fno-omit-frame-pointer -Wno-pointer-sign -Wno-parentheses \
        -DAROS_BUILD_TYPE=AROS_BUILD_TYPE_PERSONAL -DUSE_EXEC_DEBUG -D__NOLIBBASE__ \
        -I"$objects/include" -include "$objects/include/fat_deflibdefs.h" \
        "-D__SRCFILENAME__=\"rom/filesys/fat/$unit.c\"" \
        -c "$out/source/$unit.c" -o "$out/objects/$unit.o"
done
parts=()
for object in "$out/objects"/*.o;do
    case "$object" in */fat_start.o|*/fat_end.o) continue;; esac
    parts+=("$object")
done
"$cc" "${flags[@]}" -nostartfiles -Wl,-Map,"$out/fat.map" \
    "$out/objects/fat_start.o" "${parts[@]}" "$out/objects/fat_end.o" \
    -o "$out/fat-handler" -L"$gen/lib" -static \
    -Wl,--start-group -lfatbpb -ldebug -larossupport -Wl,--end-group
sha256sum "$out/fat-handler" >"$out/output.sha256"
python3 "$repo/aros/cli/package-environment-overlay.py" \
    "${AROS_FAT_BASE_PACKAGE:?existing private filesystem package required}" "$out" fat-handler aros-fs
sha256sum -c "$out/inputs.sha256"
