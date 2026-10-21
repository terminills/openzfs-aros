#!/usr/bin/env bash
# Build only into a fresh private directory; never mutate the platform build.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source=${AROS_HOSTED_SOURCE:-/opt/aros-workspace/aros-adt-src}
platform=${AROS_HOSTED_BUILD:-/opt/aros-workspace/aros-adt-hosted-clean}
sdk="$platform/bin/linux-x86_64/AROS/Developer"
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
gen="$platform/bin/linux-x86_64/tools/genmodule"
out=$(mktemp -d "$repo/build-aros/hosted-security.XXXXXXXX")
echo "Evidence: $out"
cp -a "$source/rom/security" "$out/source"
cp "$platform/rom/security/strings.h" "$out/source/strings.h"
mkdir -p "$out/gen" "$out/include"/{proto,clib,inline,defines,pragmas}
for operation in writefiles writelibdefs;do
    "$gen" -c "$out/source/security.conf" -d "$out/gen" "$operation" security library
done
"$gen" -c "$out/source/security.conf" -d "$out/include" writeincludes security library
# Preserve the platform module's explicit compilation list, not every .c file
# that might happen to be present in a future source checkout.
awk '
 /^(PUBLICFUNCS|CRTPUBLICFUNCS|FSFUNCS|PLUGINFUNCS|DOSFUNCS|SECURITYFILES) :=/ { active=1; next }
 active { gsub(/\\/, ""); if ($0 ~ /^[[:space:]]*$/) {active=0; next}; print $1 }
' "$out/source/mmakefile.src" >"$out/source-list.txt"
printf '%s\n' security_init >>"$out/source-list.txt"
flags=(--sysroot="$sdk" -B"$sdk/lib/" -O2 -g -m64 -mcmodel=large -mno-red-zone
    -mno-ms-bitfields -DDEBUG=0 -DUSE_EXEC_DEBUG '-DLC_LIBDEFS_FILE="security_libdefs.h"' -I"$out/include" -I"$out/gen"
    -I"$out/source" -I"$source/rom/exec")
for includes in "$platform"/bin/linux-x86_64/gen/rom/exec/exec/include/.exec.includeflag.*;do
    read -ra extra <"$includes"
    flags+=("${extra[@]}")
done
objects=()
while read -r unit;do
    [[ $unit =~ ^[a-zA-Z_][a-zA-Z_0-9]*$ && -f "$out/source/$unit.c" ]]
    "$cc" "${flags[@]}" -c "$out/source/$unit.c" -o "$out/$unit.o"
    objects+=("$out/$unit.o")
done <"$out/source-list.txt"
[[ ${#objects[@]} -gt 0 ]]
for unit in security_start security_end;do
    "$cc" "${flags[@]}" -c "$out/gen/$unit.c" -o "$out/$unit.o"
done
"$cc" "${flags[@]}" -nostartfiles -static "$out/security_start.o" \
    "${objects[@]}" "$out/security_end.o" -lamiga -o "$out/security.library"
sha256sum "$out/source/"*.[ch] "$out/source/security.conf" "$out/source/mmakefile.src" \
    "$cc" "$gen" >"$out/inputs.sha256"
sha256sum "$out/security.library" >"$out/binaries.sha256"
git rev-parse HEAD >"$out/source-head.txt"
git diff --binary >"$out/source.patch"
cp "$0" "$out/build-hosted-security.sh"
file "$out/security.library"
echo "PASS private hosted security library build: $out"
