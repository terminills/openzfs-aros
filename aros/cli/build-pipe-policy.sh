#!/usr/bin/env bash
# Experimental private named-pipe policy; never modifies a shared AROS tree.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source=${1:-/opt/aros-workspace/aros-adt-src/workbench/fs/pipe}
sdk=/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer
cc=/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc
out=$(mktemp -d "$repo/build-aros/pipe-policy.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
mkdir "$out/source"
cp "$source/"*.[ch] "$out/source/"
cp "$0" aros/cli/pipe-disconnect.patch aros/cli/PIPE-Disconnect-Test "$out/"
sha256sum "$out/source/"* >"$out/original-source.sha256"
patch --batch --fuzz=0 -d "$out/source" -p1 <"$out/pipe-disconnect.patch"
sha256sum "$out/source/"* "$out/pipe-disconnect.patch" >"$out/patched-source.sha256"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -nostartfiles -static \
    "$out/source/"*.c -o "$out/pipe-handler"
sha256sum "$out/pipe-handler" >"$out/output.sha256"
