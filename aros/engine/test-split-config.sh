#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
host=$(realpath -e "${OPENZFS_HOST_BUILD:-$repo/build-control}")
[[ -f $host/zfs_config.h && -d $host/.libs ]]
mkdir -p "$repo/build-aros"
source "$repo/aros/engine/storage-preflight.sh"
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
out=$(mktemp -d "$root/split-config-run.XXXXXXXX")
printf 'Artifacts: %s\n' "$out"
flags=(-Wall -Wextra -Werror -I"$host" -Iinclude -Ilib/libspl/include
    -include "$host/zfs_config.h")
if [[ ${AROS_HOST_SANITIZE:-0} == 1 ]];then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
fi
cc "${flags[@]}" aros/engine/split-config.c aros/engine/split-config-test.c \
    -L"$host/.libs" -Wl,-rpath,"$host/.libs" -lnvpair \
    -o "$out/split-config-test" >"$out/build.log" 2>&1
"$out/split-config-test" >"$out/test.log" 2>&1
cat "$out/test.log"
