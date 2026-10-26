#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
export AROS_CC=/home/terminills/Documents/GitHub/aros-adt-crosstools/x86_64-aros-gcc
export AROS_SDK=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/bin/pc-x86_64-smp/AROS/Developer
export AROS_PRIVATE_PTHREAD=/home/terminills/Documents/GitHub/openzfs-aros-public/build-aros/pthread-consistent.ARVzbA2v/libpthread.a
export AROS_PTHREAD_INCLUDE=${AROS_PRIVATE_PTHREAD%/*}/source
export AROS_TIRPC_SOURCE=/home/terminills/Documents/GitHub/openzfs-aros/build-aros/libtirpc-1.3.7
export AROS_NVPAIR_ARCHIVE=$PWD/build-audit/private-nvpair/libnvpair.a
export AROS_CRYPTO_INCLUDE=/home/terminills/Documents/GitHub/openzfs-aros-public/build-aros/crypto-input.97AxCjOW/source/include
export AROS_CRYPTO_ARCHIVE=/home/terminills/Documents/GitHub/openzfs-aros-public/build-aros/crypto-source.bC1xHzNG/build/crypto/libcrypto.a
export AROS_ZLIB_IMPORT=$AROS_SDK/lib/libz1.a
export AROS_ARTIFACT_ROOT=$PWD/build-audit
bash aros/engine/build-source.sh
