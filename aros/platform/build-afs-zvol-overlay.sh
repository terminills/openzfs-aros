#!/usr/bin/env bash
# Private dependency overlay: never change shared AROS source/build artifacts.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
build=${AROS_RELEASE_BUILD:?explicit configured PC/SMP build required}
source=${AROS_BASELINE_SOURCE:?matching AROS baseline source required}/rom/filesys/afs
sdk="$build/bin/pc-x86_64-smp/AROS/Developer"
gen="$build/bin/pc-x86_64-smp/gen"
objects="$gen/rom/filesys/afs/afs"
cc=${AROS_CC:?explicit AROS cross compiler required}
available=$(df -Pk "$repo" | awk 'NR==2 {print $4}')
(( available >= 104857600 )) || { echo '100 GiB workspace reserve required' >&2; exit 2; }
source "$repo/aros/engine/storage-preflight.sh"
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:?}")
out=$(mktemp -d "$root/afs-zvol.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
mkdir "$out/source" "$out/objects"
cp "$source"/*.[ch] "$out/source/"
cp "$objects"/*.o "$out/objects/"
cp "$repo/aros/platform/afs-single-die-reply.patch" "$0" "$out/"
sha256sum "$source"/*.[ch] "$objects"/*.o "$objects/include/afs_deflibdefs.h" >"$out/inputs.sha256"
patch --batch --fuzz=0 -d "$out/source" -p0 <"$out/afs-single-die-reply.patch"
if [[ ${AROS_AFS_RENAME_FIX:-0} == 1 ]];then
    cp "$repo/aros/platform/afs-rename-pinned-link.patch" "$out/"
    patch --batch --fuzz=0 -d "$out/source" -p0 <"$out/afs-rename-pinned-link.patch"
fi
flags=(--sysroot "$sdk" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
if [[ ${AROS_AFS_INHIBIT_TRACE:-0} == 1 ]];then
    cp "$repo/aros/platform/afs-inhibit-trace.patch" "$out/"
    patch --batch --fuzz=0 -d "$out/source" -p0 <"$out/afs-inhibit-trace.patch"
    "$cc" "${flags[@]}" -iquote "$out/source" -O2 -fno-asynchronous-unwind-tables \
        -fno-omit-frame-pointer -Wno-pointer-sign -Wno-parentheses \
        -DAROS_BUILD_TYPE=AROS_BUILD_TYPE_PERSONAL -DUSE_EXEC_DEBUG -DDEBUG=0 \
        -D__INTUITION_NOLIBBASE__ -I"$objects/include" -include "$objects/include/afs_deflibdefs.h" \
        '-D__SRCFILENAME__="rom/filesys/afs/os_aros_support.c"' -c "$out/source/os_aros_support.c" -o "$out/objects/os_aros_support.o"
fi
"$cc" "${flags[@]}" -iquote "$out/source" -O2 -fno-asynchronous-unwind-tables \
    -fno-omit-frame-pointer -Wno-pointer-sign -Wno-parentheses \
    -DAROS_BUILD_TYPE=AROS_BUILD_TYPE_PERSONAL -DUSE_EXEC_DEBUG -DDEBUG=0 \
    -D__INTUITION_NOLIBBASE__ -I"$objects/include" -include "$objects/include/afs_deflibdefs.h" \
    '-D__SRCFILENAME__="rom/filesys/afs/main.c"' -c "$out/source/main.c" -o "$out/objects/main.o"
if [[ ${AROS_AFS_RENAME_FIX:-0} == 1 ]];then
    "$cc" "${flags[@]}" -iquote "$out/source" -O2 -fno-asynchronous-unwind-tables \
        -fno-omit-frame-pointer -Wno-pointer-sign -Wno-parentheses \
        -DAROS_BUILD_TYPE=AROS_BUILD_TYPE_PERSONAL -DUSE_EXEC_DEBUG -DDEBUG=0 \
        -D__INTUITION_NOLIBBASE__ -I"$objects/include" -include "$objects/include/afs_deflibdefs.h" \
        '-D__SRCFILENAME__="rom/filesys/afs/filehandles2.c"' -c "$out/source/filehandles2.c" -o "$out/objects/filehandles2.o"
fi
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
if [[ -n ${AROS_AFS_BASE_PACKAGE:-} ]];then
    python3 "$repo/aros/cli/package-environment-overlay.py" \
        "$AROS_AFS_BASE_PACKAGE" "$out" afs-handler aros-fs
fi
sha256sum -c "$out/inputs.sha256"
