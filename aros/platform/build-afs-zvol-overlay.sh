#!/usr/bin/env bash
# Private dependency overlay: never change shared AROS source/build artifacts.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
build=/opt/aros-workspace/aros-amiwest-nightly-release
source=/opt/aros-workspace/AROS-amiwest-nightly-src/rom/filesys/afs
sdk="$build/bin/pc-x86_64-smp/AROS/Developer"
gen="$build/bin/pc-x86_64-smp/gen"
objects="$gen/rom/filesys/afs/afs"
cc=/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc
out=$(mktemp -d "$repo/build-aros/afs-zvol.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
mkdir "$out/source" "$out/objects"
cp "$source"/*.[ch] "$out/source/"
cp "$objects"/*.o "$out/objects/"
cp "$repo/aros/platform/afs-single-die-reply.patch" "$0" "$out/"
sha256sum "$source"/*.[ch] "$objects"/*.o "$objects/include/afs_deflibdefs.h" >"$out/inputs.sha256"
patch --batch --fuzz=0 -d "$out/source" -p0 <"$out/afs-single-die-reply.patch"
flags=(--sysroot "$sdk" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
"$cc" "${flags[@]}" -iquote "$out/source" -O2 -fno-asynchronous-unwind-tables \
    -fno-omit-frame-pointer -Wno-pointer-sign -Wno-parentheses \
    -DAROS_BUILD_TYPE=AROS_BUILD_TYPE_PERSONAL -DUSE_EXEC_DEBUG -DDEBUG=0 \
    -D__INTUITION_NOLIBBASE__ -I"$objects/include" -include "$objects/include/afs_deflibdefs.h" \
    '-D__SRCFILENAME__="rom/filesys/afs/main.c"' -c "$out/source/main.c" -o "$out/objects/main.o"
parts=()
for object in "$out/objects"/*.o;do
    case "$object" in */afs_start.o|*/afs_end.o) continue;; esac
    parts+=("$object")
done
"$cc" "${flags[@]}" -nostartfiles -Wl,-Map,"$out/afs.map" \
    "$out/objects/afs_start.o" \
    "${parts[@]}" "$out/objects/afs_end.o" -o "$out/afs-handler" \
    -L"$gen/lib" -static -Wl,--start-group -ldebug -larossupport -Wl,--end-group
sha256sum "$out/afs-handler" >"$out/output.sha256"
python3 "$repo/aros/cli/package-environment-overlay.py" \
    "${AROS_AFS_BASE_PACKAGE:?existing fixture filesystem package required}" "$out" afs-handler aros-fs
sha256sum -c "$out/inputs.sha256"
