#!/usr/bin/env bash
# Fetch a pinned upstream tree and apply the reviewed AROS portability delta.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
upstream=${AROS_BORINGSSL_REPOSITORY:-https://boringssl.googlesource.com/boringssl.git}
revision=4fa4804c8ab4521079af62dba5260a99c34b8a29
source "$repo/aros/engine/storage-preflight.sh"
mkdir -p "$repo/build-aros"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/crypto-input.XXXXXXXX")
echo "Crypto source preparation: $run"
git init -q "$run/source"
git -C "$run/source" fetch --depth=1 "$upstream" "$revision" >"$run/fetch.log" 2>&1
git -C "$run/source" checkout --detach FETCH_HEAD >"$run/checkout.log" 2>&1
[[ $(git -C "$run/source" rev-parse HEAD) == "$revision" ]]
cp "$repo/aros/patches/boringssl-aros.patch" "$run/boringssl-aros.patch"
sha256sum "$run/boringssl-aros.patch" >"$run/patch.sha256"
git -C "$run/source" apply --check "$run/boringssl-aros.patch"
git -C "$run/source" apply "$run/boringssl-aros.patch"
git -C "$run/source" diff --check
git -C "$run/source" diff --binary >"$run/applied.patch"
git -C "$run/source" rev-parse HEAD >"$run/upstream-revision.txt"
find "$run/source" -type f ! -path '*/.git/*' -print0 | LC_ALL=C sort -z |
    xargs -0 sha256sum >"$run/source.sha256"
echo "AROS_CRYPTO_SOURCE=$run/source"
