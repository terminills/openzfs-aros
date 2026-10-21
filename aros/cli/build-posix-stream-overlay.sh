#!/usr/bin/env bash
# Add a teardown hook to a private frozen-runtime map; no installed files change.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
base=${1:?private POSIX map required}
release=/opt/aros-workspace/aros-amiwest-nightly-release
baseline=/opt/aros-workspace/AROS-amiwest-nightly-src
sdk=$release/bin/pc-x86_64-smp/AROS/Developer
gen=$release/bin/pc-x86_64-smp/gen/compiler/crt/posixc/posixc
cc=/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc
out=$(mktemp -d "$repo/build-aros/cli-posix-stream.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
cp "$0" aros/cli/posix-stream-end.c "$out/"
flags=(--sysroot="$sdk" -B"$sdk/lib/" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
"$cc" "${flags[@]}" -O2 -D__POSIXC_NOLIBBASE__ -DAROS_POSIXC_BUILD \
    "-DLC_LIBDEFS_FILE=\"$(dirname "$base")/include/posixc_libdefs.h\"" \
    -I"$(dirname "$base")/include" -I"$gen/include" \
    -I"$baseline/compiler/crt/posixc" -I"$baseline/compiler/crt/stdc" \
    -c "$out/posix-stream-end.c" -o "$out/posix-stream-end.o"
objects=("$out/posix-stream-end.o");archives=()
while read -r kind path rest;do
    [[ $kind == LOAD ]] || continue
    [[ -f $path ]] || { echo "Missing map input: $path";exit 1; }
    case "$path" in
        */posix-stream-end.o) echo 'Already contains stream teardown hook';exit 2;;
        *.o) objects+=("$path");;
        *.a) archives+=("$path");;
    esac
done <"$base"
sha256sum "$base" "${objects[@]}" "${archives[@]}" \
    "$baseline/compiler/crt/posixc/__stdio.h" \
    "$baseline/compiler/crt/posixc/__posixc_intbase.h" >"$out/inputs.sha256"
"$cc" "${flags[@]}" -nostartfiles -nostdlib -Wl,-Map,"$out/posixc.map" \
    "${objects[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group \
    -o "$out/posixc.library"
sha256sum "$out/posixc.library" >"$out/output.sha256"
echo "Overlay: $out/posixc.library"
