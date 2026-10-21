#!/usr/bin/env bash
# Extend the frozen test runtime ABI by one vector, without changing installed SDKs.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
release=/opt/aros-workspace/aros-amiwest-nightly-release
source=/opt/aros-workspace/AROS
baseline=/opt/aros-workspace/AROS-amiwest-nightly-src
sdk=$release/bin/pc-x86_64-smp/AROS/Developer
gen=$release/bin/pc-x86_64-smp/gen/compiler/crt/posixc/posixc
generator=$release/bin/linux-x86_64/tools/genmodule
cc=/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc
base=${AROS_PIPE_BASE_MAP:-$repo/build-aros/cli-exec.VG4qQCWw/posixc.map}
out=$(mktemp -d "$repo/build-aros/cli-pipe.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
mkdir -p "$out/include" "$out/stubs"
cp "$source/compiler/crt/posixc/pipe.c" "$out/"
# Preserve every frozen vector index. Only append the new vector.
awk '/^##end functionlist/ {print "int pipe2(int filedes[2], int flags)"} {print}' \
    "$baseline/compiler/crt/posixc/posixc.conf" > "$out/posixc.conf"
"$generator" -c "$out/posixc.conf" -d "$out" -l "$out/stubs" writefiles posixc library
"$generator" -c "$out/posixc.conf" -d "$out/include" writelibdefs posixc library
common=(--sysroot="$sdk" -B"$sdk/lib/" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
for unit in pipe posixc_start posixc_end; do
    "$cc" "${common[@]}" -O2 -D__POSIXC_NOLIBBASE__ -DAROS_POSIXC_BUILD \
        "-DLC_LIBDEFS_FILE=\"$out/include/posixc_libdefs.h\"" \
        -I"$out/include" -I"$gen/include" -I"$baseline/compiler/crt/posixc" \
        -I"$baseline/compiler/crt/stdc" -c "$out/$unit.c" -o "$out/$unit.o"
done
objects=() archives=() count=0
while read -r kind path rest; do
    [[ $kind == LOAD ]] || continue
    [[ -f $path ]] || { echo "Missing map input: $path"; exit 1; }
    case "$path" in
        *.o) case ${path##*/} in
            pipe.o|posixc_start.o|posixc_end.o) objects+=("$out/${path##*/}"); count=$((count+1));;
            *) objects+=("$path");; esac ;;
        *.a) archives+=("$path");;
    esac
done < "$base"
[[ $count == 3 ]]
sha256sum "$base" "$generator" "$out/posixc.conf" "$out/pipe.c" \
    "${objects[@]}" "${archives[@]}" > "$out/inputs.sha256"
"$cc" "${common[@]}" -nostartfiles -nostdlib -Wl,-Map,"$out/posixc.map" \
    "${objects[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group \
    -o "$out/posixc.library"
sha256sum "$out/posixc.library" > "$out/output.sha256"
echo "Overlay: $out/posixc.library"
