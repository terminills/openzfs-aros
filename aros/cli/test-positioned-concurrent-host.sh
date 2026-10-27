#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
source "$repo/aros/engine/storage-preflight.sh"
root=$(oz_artifact_root "$repo" "${AROS_TEST_ARTIFACT_ROOT:?external evidence directory required}")
run=$(mktemp -d "$root/positioned-host.XXXXXXXX")
cc=${CC:-cc}
"$cc" -O2 -Wall -Wextra -Werror -pthread "$repo/aros/cli/positioned-concurrent-test.c" -o "$run/positive"
"$cc" -O2 -Wall -Wextra -Werror -pthread "$repo/aros/cli/positioned-concurrent-test.c" "$repo/aros/cli/positioned-unsafe-control.c" -Wl,--wrap=pread,--wrap=pwrite -o "$run/negative"
cd "$run"
for iteration in 1 2 3;do
    ./positive >"positive-$iteration.log" 2>&1
    rg -q '^RESULT positioned concurrency PASS workers=4' "positive-$iteration.log"
    status=0
    ./negative >"negative-$iteration.log" 2>&1 || status=$?
    [[ $status == 20 ]]
    rg -q '^RESULT positioned concurrency FAIL workers=4' "negative-$iteration.log"
    [[ ! -e openzfs-positioned-concurrent.tmp ]]
done
sha256sum "$repo/aros/cli/positioned-concurrent-test.c" "$repo/aros/cli/positioned-unsafe-control.c" positive negative >inputs.sha256
echo "PASS positional concurrency host controls: positive=3 negative-detected=3 cleanup=6 evidence=$run"
