#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
cd "$(dirname "$0")/../.."
: "${AROS_TEST_ARTIFACT_ROOT:?Set an external artifact directory}"
run=$(mktemp -d "$AROS_TEST_ARTIFACT_ROOT/rename-lock.XXXXXXXX")
echo "Artifacts: $run"
# Compile the production ancestry helper, not a duplicate implementation.
awk '/^typedef struct zfs_zlock / { copy=1 }
    copy {
        if ($0 == "/*") { pending=$0 ORS; next }
        if (pending != "") {
            if ($0 ~ /^ \* Move an entry/) exit
            printf "%s", pending; pending=""
        }
        print
    }' aros/zpl/rename.c > "$run/rename-lock.inc"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$run" aros/zpl/rename-lock-test.c -o "$run/test"
"$run/test" 2>&1 | tee "$run/result.log"
