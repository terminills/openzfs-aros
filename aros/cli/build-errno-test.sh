#!/usr/bin/env bash
# Build a storage-free native errno ABI probe against one explicit SDK.
set -euo pipefail
sdk=${1:?usage: build-errno-test.sh SDK NEW_OUTPUT_DIRECTORY}
out=${2:?new output directory required}
cc=${AROS_CC:?AROS_CC must identify the cross compiler}
here=$(cd -- "$(dirname -- "$0")" && pwd)
sdk=$(realpath -- "$sdk")
test -d "$sdk/include"
test -f "$sdk/lib/libstdc.a"
mkdir -- "$out"
out=$(realpath -- "$out")
cp "$here/errno-native-test.c" "$here/errno-check.h" "$out/"
printf '%s\n' "$sdk" > "$out/sdk-path.txt"
"$cc" --version > "$out/compiler-version.txt"
(
    set -x
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" -nix -O2 -Wall -Wextra -Werror \
        "$out/errno-native-test.c" -Wl,-Map,"$out/link.map" \
        -o "$out/OpenZFSErrnoTest"
) > "$out/build.log" 2>&1
sha256sum "$out/errno-native-test.c" "$out/errno-check.h" \
    "$out/OpenZFSErrnoTest" "$sdk/lib/libstdc.a" \
    "$sdk/lib/libposixc.a" "$sdk/lib/libstdcio.a" > "$out/inputs.sha256"
echo "Built $out/OpenZFSErrnoTest (not runtime-qualified)"
