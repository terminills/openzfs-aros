#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source <(sed '$ d' build-audit/build-accounting-engine.sh)
source aros/engine/storage-preflight.sh
artifact_root=$(oz_artifact_root "$PWD/build-aros" "$AROS_ARTIFACT_ROOT")
run=$(mktemp -d "$artifact_root/mmake-engine.XXXXXXXX")
echo "Evidence: $run"
cp "$0" "$run/build.sh"
git rev-parse HEAD > "$run/source-head.txt"
git diff --binary > "$run/source.patch"
make -j8 -f aros/engine/build-system.mk contrib-openzfs-engine \
    SRCDIR="$PWD" CURDIR=aros/engine GENDIR="$run" MKDIR='mkdir -p' \
    AROS_CC="$AROS_CC" AROS_LD="$("$AROS_CC" -print-prog-name=ld)" \
    TARGET_SYSROOT="--sysroot=$AROS_SDK -B$AROS_SDK/lib/" \
    OPENZFS_PTHREAD_INCLUDE="$AROS_PTHREAD_INCLUDE" \
    OPENZFS_TIRPC_SOURCE="$AROS_TIRPC_SOURCE" \
    OPENZFS_CRYPTO_INCLUDE="$AROS_CRYPTO_INCLUDE" \
    > "$run/make.log" 2>&1
bash aros/library/verify-engine.sh "$run/aros/engine/full/full-engine.o" \
    "$run/aros/engine/full" "$("$AROS_CC" -print-prog-name=nm)" \
    > "$run/verify-engine.log" 2>&1
echo "PASS source engine through actual MetaMake make fragment: $run"
