#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
release=/opt/aros-workspace/aros-amiwest-nightly-release
source=/opt/aros-workspace/AROS
baseline=/opt/aros-workspace/AROS-amiwest-nightly-src
sdk=$release/bin/pc-x86_64-smp/AROS/Developer
gen=$release/bin/pc-x86_64-smp/gen/rom/dos/dos
cc=/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc
base=$repo/build-aros/dos-overlay
out=$(mktemp -d "$repo/build-aros/cli-environment.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
mkdir -p "$out/include/dos"
cp "$source/compiler/include/dos/envpolicy.h" "$out/include/dos/"
common=(--sysroot="$sdk" -B"$sdk/lib/" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
for unit in createnewproc getvar; do
    cp "$source/rom/dos/$unit.c" "$out/"
    "$cc" "${common[@]}" -O2 '-DLC_LIBDEFS_FILE="dos_libdefs.h"' \
        -I"$out/include" -I"$gen/include" -I"$baseline/rom/dos" \
        -c "$out/$unit.c" -o "$out/$unit.o"
done
objects=() archives=() count=0
while read -r kind path rest; do
    [[ $kind == LOAD ]] || continue
    [[ -f $path ]] || { echo "Missing map input: $path"; exit 1; }
    case "$path" in
        *.o)
            case ${path##*/} in
                createnewproc.o|getvar.o) objects+=("$out/${path##*/}"); count=$((count+1)) ;;
                *) objects+=("$path") ;;
            esac ;;
        *.a) archives+=("$path") ;;
    esac
done < "$base/dos.map"
[[ $count == 2 ]]
sha256sum "$base/dos.map" "${objects[@]}" "${archives[@]}" "$out/"*.c \
    "$out/include/dos/envpolicy.h" > "$out/inputs.sha256"
"$cc" "${common[@]}" -nostartfiles -nostdlib -static -Wl,-Map,"$out/dos.map" \
    "${objects[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group -o "$out/dos.library"
python3 "$repo/aros/cli/package-environment-overlay.py" "$base/aros-base.pkg.xz" "$out"
echo "Overlay: $out"
