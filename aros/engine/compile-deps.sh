#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
mkdir -p build-aros/compile-logs
pass=0; fail=0
while read -r source; do
    log="build-aros/compile-logs/${source//\//_}.log"
    if bash aros/engine/compile-object.sh "$source" > "$log" 2>&1; then
        echo "PASS $source"; pass=$((pass+1))
    else
        echo "FAIL $source"; fail=$((fail+1))
        rg -m 2 'error:' "$log" || true
    fi
done < <(awk '/^[[:space:]]+module\/.*\.(c|S)([[:space:]]|$)/ {print $1}' \
    lib/libicp/Makefile.am lib/libavl/Makefile.am lib/libunicode/Makefile.am lib/libzstd/Makefile.am | \
    awk '!/asm-(aarch64|arm|ppc64)\//')
echo "DEPENDENCY COMPILE ONLY pass=$pass fail=$fail"
[[ $fail == 0 ]]
