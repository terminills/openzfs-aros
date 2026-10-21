#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
mkdir -p build-aros/compile-logs
mkdir -p build-aros/include
bash scripts/make_gitrev.sh build-aros/include/zfs_gitrev.h
pass=0;fail=0
while read -r source; do
    log="build-aros/compile-logs/${source//\//_}.log"
    if bash aros/engine/compile-object.sh "$source" > "$log" 2>&1; then
        echo "PASS $source";pass=$((pass+1))
    else
        echo "FAIL $source";fail=$((fail+1))
        rg -m 1 'error:' "$log" || true
    fi
done < <(awk '/^[[:space:]]+[^#]*\.c([[:space:]]|$)/ {gsub(/%D%/,"lib/libzpool",$1);print $1}' lib/libzpool/Makefile.am)
echo "COMPILE ONLY pass=$pass fail=$fail; linking and runtime NOT implied"
[[ $fail == 0 ]]
