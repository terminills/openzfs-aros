#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
out=$(mktemp -d "$repo/build-aros/split-config-run.XXXXXXXX")
printf 'Artifacts: %s\n' "$out"
flags=(-Wall -Wextra -Werror -Ibuild-control -Iinclude -Ilib/libspl/include
    -include build-control/zfs_config.h)
if [[ ${AROS_HOST_SANITIZE:-0} == 1 ]];then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
fi
cc "${flags[@]}" aros/engine/split-config.c aros/engine/split-config-test.c \
    -Lbuild-control/.libs -Wl,-rpath,"$repo/build-control/.libs" -lnvpair \
    -o "$out/split-config-test" >"$out/build.log" 2>&1
"$out/split-config-test" >"$out/test.log" 2>&1
cat "$out/test.log"
