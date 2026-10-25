#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-official-sdk-audit.xi9RkF3r/publication
export AROS_CC=/home/terminills/Documents/GitHub/aros-adt-crosstools/x86_64-aros-gcc
export AROS_CXX=/home/terminills/Documents/GitHub/aros-adt-crosstools/x86_64-aros-g++
export AROS_SDK=/run/media/terminills/Codex/show-consolidation-01a16962/sdk-prerequisite-audit/nightly-20261023/iso-Developer
export AROS_ARTIFACT_ROOT=/mnt/short-term/openzfs-official-deps.fVjvrmGW
export AROS_PTHREAD_INCLUDE="$AROS_SDK/include"
unset AROS_PRIVATE_PTHREAD
export AROS_NVPAIR_OUTPUT_DIR="$AROS_ARTIFACT_ROOT/nvpair"
export AROS_NVPAIR_OBJECT_DIR="$AROS_NVPAIR_OUTPUT_DIR/objects"
export AROS_NVPAIR_ARCHIVE="$AROS_NVPAIR_OUTPUT_DIR/libnvpair.a"
export AROS_TIRPC_SOURCE=/home/terminills/Documents/GitHub/openzfs-aros/build-aros/libtirpc-1.3.7
export AROS_TIRPC_ARCHIVE=/home/terminills/Documents/GitHub/openzfs-aros/build-aros/libtirpc-1.3.7.tar.bz2
export AROS_CRYPTO_SOURCE=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/build-workspace.98Zx7n6B/build-aros/crypto-input.97AxCjOW/source
export AROS_BUILD_JOBS=8
export AROS_HOST_GO=/home/terminills/Documents/GitHub/openzfs-aros-public/build-aros/host-go-1.27.3/go/bin/go
sha256sum "$AROS_SDK/lib/libpthread.a" "$AROS_SDK/include/pthread.h" > "$AROS_ARTIFACT_ROOT/pthread-inputs.sha256"
if [[ ${1:-all} != crypto ]]; then
    bash aros/engine/build-nvpair.sh > "$AROS_ARTIFACT_ROOT/nvpair-build.log" 2>&1
fi
bash aros/engine/build-crypto.sh > "$AROS_ARTIFACT_ROOT/crypto-build-retry.log" 2>&1
sha256sum -c "$AROS_ARTIFACT_ROOT/pthread-inputs.sha256" > "$AROS_ARTIFACT_ROOT/pthread-after.log"
