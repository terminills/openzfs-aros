#!/usr/bin/env bash
# Recompile ADT's pipe-aware isatty against our frozen ABI, in a private overlay.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
base=${1:?private POSIX map required}
source=${2:-/opt/aros-workspace/aros-adt-src/compiler/crt/posixc/isatty.c}
release=/opt/aros-workspace/aros-amiwest-nightly-release
baseline=/opt/aros-workspace/AROS-amiwest-nightly-src
sdk=$release/bin/pc-x86_64-smp/AROS/Developer
gen=$release/bin/pc-x86_64-smp/gen/compiler/crt/posixc/posixc
cc=/opt/aros-workspace/aros-crosstools/x86_64-aros-gcc
out=$(mktemp -d "$repo/build-aros/cli-isatty.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
cp "$0" "$source" "$out/"
flags=(--sysroot="$sdk" -B"$sdk/lib/" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
"$cc" "${flags[@]}" -O2 -D__POSIXC_NOLIBBASE__ -DAROS_POSIXC_BUILD \
    -I"$gen/include" -I"$baseline/compiler/crt/posixc" -I"$baseline/compiler/crt/stdc" \
    -c "$out/isatty.c" -o "$out/isatty.o"
objects=();archives=();count=0
while read -r kind path rest;do
    [[ $kind == LOAD ]] || continue
    [[ -f $path ]] || { echo "Missing map input: $path";exit 1; }
    case "$path" in
        */isatty.o) objects+=("$out/isatty.o");count=$((count+1));;
        *.o) objects+=("$path");;
        *.a) archives+=("$path");;
    esac
done <"$base"
[[ $count == 1 ]]
sha256sum "$base" "$out/isatty.c" "${objects[@]}" "${archives[@]}" >"$out/inputs.sha256"
"$cc" "${flags[@]}" -nostartfiles -nostdlib -Wl,-Map,"$out/posixc.map" \
    "${objects[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group -o "$out/posixc.library"
sha256sum "$out/posixc.library" >"$out/output.sha256"
echo "Overlay: $out/posixc.library"
