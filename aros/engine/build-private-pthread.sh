#!/usr/bin/env bash
# Never mix a newer pthread.c core with archive objects using another ThreadInfo.
# Snapshot and compile the entire source set; never install into shared SDKs.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:?explicit AROS compiler required}
sdk=${AROS_SDK:?native AROS SDK required}
src=${AROS_PTHREAD_SOURCE:?explicit pthread source tree required}
sdk=$(realpath -e "$sdk")
src=$(realpath -e "$src")
[[ -f $sdk/include/exec/types.h && -d $sdk/lib ]]
[[ -f $src/pthread.c && -f $src/pthread.h && -f $src/mmakefile.src ]]
source "$repo/aros/engine/storage-preflight.sh"
mkdir -p "$repo/build-aros"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/pthread-consistent.XXXXXXXX")
echo "Pthread build: $run"
mkdir "$run/source" "$run/objects"
cp "$0" "$run/build-private-pthread.sh"
"$cc" --version >"$run/compiler.txt"
printf 'sdk=%s\nsource=%s\ncompiler=%s\n' "$sdk" "$src" "$cc" >"$run/inputs.txt"
git -C "$repo" rev-parse HEAD >"$run/source-head.txt"
git -C "$repo" diff --binary >"$run/source.patch"
if git -C "$src" rev-parse --show-toplevel >"$run/pthread-source-root.txt" 2>/dev/null;then
    git -C "$src" rev-parse HEAD >"$run/pthread-source-head.txt"
    git -C "$src" diff HEAD --binary -- . >"$run/pthread-source.patch"
    git -C "$src" status --short -- . >"$run/pthread-source-status.txt"
else
    printf '%s\n' 'Source is not a Git checkout; snapshot hashes identify the inputs.' >"$run/pthread-source-status.txt"
fi
cp "$src/"*.c "$src/"*.h "$src/mmakefile.src" "$run/source/"
# Consumers must use this snapshot's public headers as well as its archive.
# The ADT and OPAL trees have different pthread_attr_t layouts.
sha256sum "$run/source/"* >"$run/source.sha256"
if [[ -n ${AROS_PTHREAD_DOSTAGS:-} ]];then
    mkdir "$run/source/dos"
    cp "$AROS_PTHREAD_DOSTAGS" "$run/source/dos/dostags.h"
    sha256sum "$run/source/dos/dostags.h" >>"$run/source.sha256"
fi
sources=(pthread sched semaphore)
while read -r name;do sources+=("$name");done < <(
    awk '/^PTHREADFUNCS :=/{active=1;next} active&&NF==0{exit} active{print $1}' "$run/source/mmakefile.src")
objects=()
printf '%s\n' "${sources[@]}" >"$run/source-inventory.txt"
for name in "${sources[@]}";do
    [[ $name =~ ^[a-zA-Z0-9_]+$ ]]
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" -I"$run/source" -O2 -g \
        -mcmodel=large -fno-strict-aliasing -c "$run/source/$name.c" -o "$run/objects/$name.o" \
        >"$run/$name.log" 2>&1
    objects+=("$run/objects/$name.o")
done
sha256sum -c "$run/source.sha256" >"$run/source-after-check.log"
archiver=$("$cc" -print-prog-name=ar)
"$archiver" rcs "$run/libpthread.a" "${objects[@]}"
sha256sum "${objects[@]}" >"$run/objects.sha256"
sha256sum "$run/libpthread.a" >"$run/library.sha256"
echo "AROS_PRIVATE_PTHREAD=$run/libpthread.a"
echo "AROS_PTHREAD_INCLUDE=$run/source"
