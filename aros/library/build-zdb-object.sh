#!/usr/bin/env bash
# Build the upstream debugger INTO the shared engine, not into its launcher.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
output=${1:?debugger object output required}
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
out=$(mktemp -d "$root/shared-debugger.XXXXXXXX")
echo "Debugger evidence: $out"
bash aros/cli/probe-zdb.sh >"$out/compile.log" 2>&1
bash aros/cli/probe-zdb-support.sh >"$out/support.log" 2>&1
probe=$(sed -n 's/^Evidence: //p' "$out/compile.log")
support=$(sed -n 's/^Evidence: //p' "$out/support.log")
[[ -d $probe && -d $support ]]
objects=("$probe/cmd_zdb_zdb.c.o" "$probe/cmd_zdb_zdb_il.c.o"
    "$probe/lib_libzdb_libzdb.c.o" "$support/support.o")
sha256sum "${objects[@]}" >"$out/inputs.sha256"
"$("$AROS_CC" -print-prog-name=ld)" -r "${objects[@]}" -o "$out/combined.o"
"$("$AROS_CC" -print-prog-name=objcopy)" --redefine-sym main=oz_zdb_main \
    "$out/combined.o" "$out/debugger.o"
sha256sum -c "$out/inputs.sha256" >"$out/stability.log"
sha256sum "$out/debugger.o" >"$out/debugger.sha256"
cp "$out/debugger.o" "$output"
