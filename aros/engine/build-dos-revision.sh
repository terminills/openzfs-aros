#!/usr/bin/env bash
# Recompile DOS from an explicit revision, never from shared cached objects.
# Produces a control library; use build-loader-overlay.sh for the patched pair.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
source_repo=${AROS_SOURCE_REPO:?source repository required}
revision=${AROS_SOURCE_REVISION:?ISO source revision required}
sdk=${AROS_SDK:?matching SDK required}
input_map=${AROS_DOS_INPUT_MAP:?module input map required}
generator=${AROS_GENMODULE:?matching genmodule required}
strings=${AROS_DOS_STRINGS:?generated DOS catalog header required}
cc=${AROS_CC:?cross compiler required}
loader_policy=${AROS_DOS_LOADER_POLICY:-0}
[[ $loader_policy == 0 || $loader_policy == 1 ]]
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
out=$(mktemp -d "$artifact_root/dos-revision.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
revision=$(git -C "$source_repo" rev-parse --verify "$revision^{commit}")
printf '%s\n' "$revision" >"$out/source-revision.txt"
mkdir "$out/source" "$out/gen" "$out/objects"
git -C "$source_repo" archive "$revision" LICENSE LICENSE.Author LICENSE.GPL LICENSE.LGPL ACKNOWLEDGEMENTS rom/dos rom/dosboot rom/expansion compiler/include compiler/libinit/libentry.c |
    tar -x -C "$out/source"
src="$out/source/rom/dos"
printf 'loader_policy=%s\n' "$loader_policy" >"$out/build-options.txt"
if [[ $loader_policy == 1 ]];then
    cp "$repo/aros/patches/dos-loader-execute.patch" "$out/"
    patch --batch --fuzz=0 -d "$src" -p1 <"$out/dos-loader-execute.patch"
fi
catalog_revision=$(git -C "$source_repo" rev-parse "$revision:rom/dos/catalogs")
git -C "$source_repo/rom/dos/catalogs" archive "$catalog_revision" dos.cd |
    tar -x -C "$src/catalogs"
printf '%s\n' "$catalog_revision" >"$out/catalog-revision.txt"
for operation in writefiles writelibdefs;do
    "$generator" -c "$src/dos.conf" -d "$out/gen" "$operation" dos library
done
python3 "$src/genstrings.py" "$src/catalogs/dos.cd" >"$out/gen/errorlist.h"
# This generated catalog header is an explicit input, not the SDK strings.h.
cp "$strings" "$out/gen/strings.h"
sha256sum "$input_map" "$generator" "$cc" "$sdk/include/exec/tasks.h" \
    "$sdk/include/dos/dosextens.h" "$src/catalogs/dos.cd" "$strings" >"$out/inputs.sha256"
find "$out/source" "$out/gen" -type f -print0 | sort -z |
    xargs -0 sha256sum >>"$out/inputs.sha256"
sha256sum "$0" "$repo/aros/patches/dos-loader-execute.patch" >>"$out/inputs.sha256"
cp "$0" "$out/build-dos-revision.sh"
flags=(--sysroot="$sdk" -B"$sdk/lib/" -m64 -mcmodel=large -mno-red-zone
    -mno-ms-bitfields -O2 -fno-asynchronous-unwind-tables -fno-omit-frame-pointer
    -D__DOS_NOLIBBASE__ -DUSE_EXEC_DEBUG -DDOCACHECLEAR
    '-DAROS_CPU="x86_64"' '-DISODATE="2026-10-22"'
    "-DREPOREVISION=\"$revision\"" '-DREPOTYPE="Git"' '-DREPOID="AROS"'
    '-DLC_LIBDEFS_FILE="dos_libdefs.h"' -I"$src" -I"$out/gen"
    -I"$out/source/compiler/include")
objects=() archives=()
while read -r tag path rest;do
    [[ $tag == LOAD ]] || continue
    case "$path" in
        *.o)
            unit=${path##*/}; unit=${unit%.o}
            case "$unit" in
                dos_start|dos_end) source_file="$out/gen/$unit.c" ;;
                libentry) source_file="$out/source/compiler/libinit/libentry.c" ;;
                *) source_file="$src/$unit.c" ;;
            esac
            test -f "$source_file"
            test ! -e "$out/objects/$unit.o"
            sha256sum "$source_file" >>"$out/inputs.sha256"
            python3 "$repo/aros/engine/record-source-build.py" run "$out" \
                "$cc" "${flags[@]}" -c "$source_file" -o "$out/objects/$unit.o"
            objects+=("$out/objects/$unit.o") ;;
        *.a)
            test -f "$path"
            sha256sum "$path" >>"$out/inputs.sha256"
            archives+=("$path") ;;
    esac
done <"$input_map"
[[ ${#objects[@]} -gt 100 ]]
python3 "$repo/aros/engine/record-source-build.py" run "$out" \
    "$cc" "${flags[@]}" -nostartfiles -nostdlib -Wl,-Map,"$out/dos.map" \
    "${objects[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group -o "$out/dos.library"
sha256sum --quiet -c "$out/inputs.sha256"
sha256sum "$out/dos.library" >"$out/output.sha256"
if [[ -n ${AROS_DOS_BASE_PACKAGE:-} ]];then
    python3 "$repo/aros/cli/package-environment-overlay.py" "$AROS_DOS_BASE_PACKAGE" "$out"
fi
python3 "$repo/aros/engine/record-source-build.py" seal "$out" dos.library dos.map
echo "PASS private DOS revision build (runtime unverified): $out/dos.library"
