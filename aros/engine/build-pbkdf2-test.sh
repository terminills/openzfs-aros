#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
sdk=${AROS_SDK:?explicit matching AROS SDK required}
cc=${AROS_CC:?explicit AROS compiler required}
include=${AROS_CRYPTO_INCLUDE:?matching crypto headers required}
archive=$(realpath -e "${AROS_CRYPTO_ARCHIVE:?native crypto archive required}")
pthread=${AROS_PRIVATE_PTHREAD:?matching complete pthread archive required}
source "$repo/aros/engine/storage-preflight.sh"
mkdir -p "$repo/build-aros"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/pbkdf2-test.XXXXXXXX")
echo "PBKDF2 test build: $run"
sha256sum aros/engine/pbkdf2-test.c aros/engine/passphrase.c aros/engine/passphrase.h "$include/openssl/evp.h" "$archive" "$pthread" >"$run/inputs.sha256"
# Read-only dependency use; never rebuild or mutate another owner's archive.
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -I"$include" -O2 -Wall -Wextra \
    aros/engine/pbkdf2-test.c aros/engine/passphrase.c "$archive" "$pthread" -lm -o "$run/OpenZFSPBKDF2Test"
sha256sum -c "$run/inputs.sha256" >"$run/input-stability.log"
sha256sum "$run/OpenZFSPBKDF2Test" >"$run/binary.sha256"
# A thin archive digest alone does not identify its member content.
if file "$archive" | rg -q 'thin archive';then
    while IFS= read -r member;do sha256sum "$member";done < <("$("$cc" -print-prog-name=ar)" t "$archive") \
        >"$run/crypto-members.sha256"
else
    sha256sum "$archive" >"$run/crypto-members.sha256"
fi
