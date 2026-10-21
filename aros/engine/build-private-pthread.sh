#!/usr/bin/env bash
# Never mix a newer pthread.c core with archive objects using another ThreadInfo.
# Snapshot and compile the entire source set; never install into shared SDKs.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
sdk=${AROS_SDK:?native AROS SDK required}
src=${AROS_PTHREAD_SOURCE:?explicit pthread source tree required}
run=$(mktemp -d "$repo/build-aros/pthread-consistent.XXXXXXXX")
echo "Pthread build: $run"
mkdir "$run/source" "$run/objects"
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
for name in "${sources[@]}";do
    [[ $name =~ ^[a-zA-Z0-9_]+$ ]]
    "$cc" --sysroot="$sdk" -B"$sdk/lib/" -I"$run/source" -O2 -g \
        -mcmodel=large -fno-strict-aliasing -c "$run/source/$name.c" -o "$run/objects/$name.o" \
        >"$run/$name.log" 2>&1
    objects+=("$run/objects/$name.o")
done
"${cc%gcc}ar" rcs "$run/libpthread.a" "${objects[@]}"
sha256sum "$run/libpthread.a" >"$run/library.sha256"
echo "AROS_PRIVATE_PTHREAD=$run/libpthread.a"
echo "AROS_PTHREAD_INCLUDE=$run/source"
