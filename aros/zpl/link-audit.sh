#!/usr/bin/env bash
# Diagnostic only. A failed link is a retained port dependency inventory.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
run=$(mktemp -d "$repo/build-aros/zpl-link.XXXXXXXX")
echo "ZPL link audit: $run"
git rev-parse HEAD >"$run/source.txt"
git diff --binary >"$run/source.patch"
export AROS_LINKED_OBJECT="$run/full-zpl.o" AROS_ZPL_AUDIT=1
if bash aros/engine/link-engine-test.sh aros/engine/engine-init-test.c >"$run/link.log" 2>&1; then
    echo 'ZPL LINK PASS ONLY; no deployment, runtime or writable-handler claim.'
else
    # The AROS compiler's final symbol check reports "U symbol", not GNU
    # ld's usual "undefined reference" wording.
    rg 'undefined reference|multiple definition|error:|undefined symbols|^[[:space:]]+U ' "$run/link.log" >"$run/unresolved.txt" || true
    echo "ZPL LINK FAIL: retained $run/link.log and unresolved.txt"
    exit 1
fi
