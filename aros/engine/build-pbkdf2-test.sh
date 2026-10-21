#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
include=${AROS_CRYPTO_INCLUDE:-/opt/aros-workspace/chromium-aros/src/third_party/boringssl/src/include}
archive=${AROS_CRYPTO_ARCHIVE:-/opt/aros-workspace/chromium-aros/src/out/aros-chrome/obj/third_party/boringssl/libboringssl.a}
pthread=${AROS_PRIVATE_PTHREAD:-$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a}
# Read-only dependency use; never rebuild or mutate another owner's archive.
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -I"$include" -O2 -Wall -Wextra \
    aros/engine/pbkdf2-test.c aros/engine/passphrase.c "$archive" "$pthread" -lm -o build-aros/OpenZFSPBKDF2Test
sha256sum aros/engine/pbkdf2-test.c aros/engine/passphrase.c aros/engine/passphrase.h "$include/openssl/evp.h" "$archive" "$pthread" \
    build-aros/OpenZFSPBKDF2Test >build-aros/pbkdf2-build-inputs.sha256
# A thin archive digest alone does not identify its member content.
if file "$archive" | rg -q 'thin archive';then
    while IFS= read -r member;do sha256sum "$member";done < <(ar t "$archive") \
        >build-aros/pbkdf2-crypto-members.sha256
else
    sha256sum "$archive" >build-aros/pbkdf2-crypto-members.sha256
fi
