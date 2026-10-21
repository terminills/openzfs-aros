#!/usr/bin/env bash
# Rebuild one runtime object against a matching frozen SDK and link map.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
release=/opt/aros-workspace/aros-amiwest-nightly-release
source=/opt/aros-workspace/AROS
baseline=${AROS_STDIO_BASELINE:-/opt/aros-workspace/AROS-amiwest-nightly-src}
sdk=${AROS_SDK:-$release/bin/pc-x86_64-smp/AROS/Developer}
gen=${AROS_STDIO_GENERATED:-$release/bin/pc-x86_64-smp/gen/compiler/crt/stdc/stdcio}
cc=${AROS_CC:-/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc}
out=$(mktemp -d "$repo/build-aros/cli-stdio.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
printf 'SDK: %s\nGenerated: %s\nBaseline: %s\nCompiler: %s\n' "$sdk" "$gen" "$baseline" "$cc"
mkdir -p "$out/compiler/crt/stdc" "$out/rom/dos"
cp "$source/compiler/crt/stdc/__stdio.c" "$out/compiler/crt/stdc/"
cp "$source/rom/dos/dos_fhflags.h" "$out/rom/dos/"
cp "$baseline/compiler/crt/stdc/__stdio.h" "$baseline/compiler/crt/stdc/__stdcio_intbase.h" \
    "$baseline/compiler/crt/stdc/debug.h" "$out/compiler/crt/stdc/"
common=(--sysroot="$sdk" -B"$sdk/lib/" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
"$cc" "${common[@]}" -O2 -noposixc -DSTDC_IO -D_GNU_SOURCE -D__ISO_C_VISIBLE=2011 \
    -I"$sdk/include/aros/stdc" -I"$gen/include" \
    -c "$out/compiler/crt/stdc/__stdio.c" -o "$out/__stdio.o"
objects=() archives=() count=0
while read -r kind path rest; do
    [[ $kind == LOAD ]] || continue
    [[ -f $path ]] || { echo "Missing map input: $path"; exit 1; }
    case "$path" in
        *.o)
            if [[ ${path##*/} == __stdio.o ]]; then
                objects+=("$out/__stdio.o"); count=$((count+1))
            else objects+=("$path"); fi ;;
        *.a) archives+=("$path") ;;
    esac
done < "$gen/.map"
[[ $count == 1 ]]
sha256sum "$gen/.map" "${objects[@]}" "${archives[@]}" \
    "$out/compiler/crt/stdc/"* "$out/rom/dos/dos_fhflags.h" > "$out/inputs.sha256"
"$cc" "${common[@]}" -nostartfiles -nostdlib -Wl,-Map,"$out/stdcio.map" \
    "${objects[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group \
    -o "$out/stdcio.library"
sha256sum "$out/stdcio.library" > "$out/output.sha256"
echo "Overlay: $out/stdcio.library"
