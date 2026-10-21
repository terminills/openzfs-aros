#!/usr/bin/env bash
# Rebuild only exec serialization against frozen nightly headers/ABI.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
release=/opt/aros-workspace/aros-amiwest-nightly-release
source=/opt/aros-workspace/AROS
baseline=/opt/aros-workspace/AROS-amiwest-nightly-src
sdk=$release/bin/pc-x86_64-smp/AROS/Developer
gen=$release/bin/pc-x86_64-smp/gen/compiler/crt/posixc/posixc
cc=/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc
base=${AROS_EXEC_BASE_MAP:-$repo/build-aros/flush-overlay.ee67h4mi/posixc.map}
out=$(mktemp -d "$repo/build-aros/cli-exec.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
cp "$source/compiler/crt/posixc/__exec.c" "$out/"
common=(--sysroot="$sdk" -B"$sdk/lib/" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
"$cc" "${common[@]}" -O2 -D__POSIXC_NOLIBBASE__ -DAROS_POSIXC_BUILD \
    -include "$gen/include/posixc_deflibdefs.h" \
    -I"$gen/include" -I"$baseline/compiler/crt/posixc" -I"$baseline/compiler/crt/stdc" \
    -c "$out/__exec.c" -o "$out/__exec.o"
objects=() archives=() count=0
while read -r kind path rest; do
    [[ $kind == LOAD ]] || continue
    [[ -f $path ]] || { echo "Missing map input: $path"; exit 1; }
    case "$path" in
        *.o) if [[ ${path##*/} == __exec.o ]]; then
                 objects+=("$out/__exec.o"); count=$((count+1))
             else objects+=("$path"); fi ;;
        *.a) archives+=("$path") ;;
    esac
done < "$base"
[[ $count == 1 ]]
sha256sum "$base" "${objects[@]}" "${archives[@]}" "$out/__exec.c" \
    "$baseline/compiler/crt/posixc/"*.h "$gen/include/posixc_deflibdefs.h" > "$out/inputs.sha256"
"$cc" "${common[@]}" -nostartfiles -nostdlib -Wl,-Map,"$out/posixc.map" \
    "${objects[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group \
    -o "$out/posixc.library"
sha256sum "$out/posixc.library" > "$out/output.sha256"
echo "Overlay: $out/posixc.library"
