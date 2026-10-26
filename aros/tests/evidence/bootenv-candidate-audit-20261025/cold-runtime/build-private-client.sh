#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
export AROS_CC=/home/terminills/Documents/GitHub/aros-adt-crosstools/x86_64-aros-gcc
export AROS_SDK=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/bin/pc-x86_64-smp/AROS/Developer
export AROS_PRIVATE_PTHREAD=/home/terminills/Documents/GitHub/openzfs-aros-public/build-aros/pthread-consistent.ARVzbA2v/libpthread.a
export AROS_PTHREAD_INCLUDE=${AROS_PRIVATE_PTHREAD%/*}/source
export AROS_TIRPC_SOURCE=/home/terminills/Documents/GitHub/openzfs-aros/build-aros/libtirpc-1.3.7
export AROS_TIRPC_ARCHIVE=/home/terminills/Documents/GitHub/openzfs-aros/build-aros/libtirpc-1.3.7.tar.bz2
export AROS_NVPAIR_OUTPUT_DIR=$PWD/build-audit/private-nvpair
export AROS_NVPAIR_ARCHIVE=$AROS_NVPAIR_OUTPUT_DIR/libnvpair.a
export AROS_GENERATED_INCLUDE=/mnt/short-term/openzfs-official-deps.fVjvrmGW/source-engine.ry8mBCM8/include
export AROS_ENGINE_OBJECT_DIR=$PWD/build-audit/private-client
export AROS_FULL_DISPATCHER=1
bash aros/engine/build-nvpair.sh
objects=()
for file in aros/cli/ioctl-public-test.c aros/cli/control-client.c aros/cli/stream-client.c aros/cli/stream-channel.c lib/libzfs_core/libzfs_core.c lib/libnvpair/libnvpair.c;do
    bash aros/engine/compile-object.sh "$file"
    objects+=("$AROS_ENGINE_OBJECT_DIR/${file//\//_}.o")
done
"$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" \
    -DOZ_NATIVE_DISPATCHER -DOZ_NATIVE_ONEXIT -I"$AROS_PTHREAD_INCLUDE" -Wall -Wextra -O2 \
    aros/ui/api.c aros/ui/admission-client.c aros/ui/project-worker.c \
    aros/cli/process.c aros/cli/arguments.c aros/cli/output.c \
    aros/ui/startup-settings.c aros/ui/startup-settings-file.c \
    aros/ui/preferences.c aros/ui/preferences-file.c aros/engine/hostid-record.c \
    aros/cli/version.c aros/engine/public-service-test.c \
    "${objects[@]}" "$AROS_NVPAIR_ARCHIVE" "$AROS_PRIVATE_PTHREAD" -liffparse \
    -o "$AROS_ENGINE_OBJECT_DIR/OpenZFSPublicServiceTest"
sha256sum "$AROS_ENGINE_OBJECT_DIR/OpenZFSPublicServiceTest" "$AROS_NVPAIR_ARCHIVE" "$AROS_PRIVATE_PTHREAD" > "$AROS_ENGINE_OBJECT_DIR/binaries.sha256"
