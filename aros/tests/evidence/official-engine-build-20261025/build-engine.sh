#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-official-sdk-audit.xi9RkF3r/publication
export AROS_CC=/home/terminills/Documents/GitHub/aros-adt-crosstools/x86_64-aros-gcc
export AROS_SDK=/run/media/terminills/Codex/show-consolidation-01a16962/sdk-prerequisite-audit/nightly-20261023/iso-Developer
export AROS_ARTIFACT_ROOT=/mnt/short-term/openzfs-official-deps.fVjvrmGW
export AROS_PTHREAD_BACKEND=sdk
unset AROS_PRIVATE_PTHREAD AROS_PTHREAD_INCLUDE AROS_PTHREAD_CORE
export AROS_NVPAIR_ARCHIVE="$AROS_ARTIFACT_ROOT/nvpair/libnvpair.a"
export AROS_TIRPC_SOURCE=/home/terminills/Documents/GitHub/openzfs-aros/build-aros/libtirpc-1.3.7
export AROS_CRYPTO_INCLUDE=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/build-workspace.98Zx7n6B/build-aros/crypto-input.97AxCjOW/source/include
export AROS_CRYPTO_ARCHIVE="$AROS_ARTIFACT_ROOT/crypto-source.OGx1CS2g/build/crypto/libcrypto.a"
export AROS_ZLIB_IMPORT="$AROS_SDK/lib/libz1.a"
bash aros/engine/build-source.sh
