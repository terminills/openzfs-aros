#!/usr/bin/env bash
# Fresh diagnostic frontends around a source-verified normal engine object.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
: "${AROS_CC:?}" "${AROS_SDK:?}" "${AROS_TEST_ENGINE_BUILD:?}"
: "${AROS_NVPAIR_ARCHIVE:?}" "${AROS_PRIVATE_PTHREAD:?}" "${AROS_CRYPTO_ARCHIVE:?}" "${AROS_ZLIB_IMPORT:?}"
engine=$(realpath -e "$AROS_TEST_ENGINE_BUILD")
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo" "${AROS_ARTIFACT_ROOT:?}")
out=$(mktemp -d "$root/large-pool-fixtures.XXXXXXXX")
echo "Evidence: $out"
exec > >(tee "$out/build.log") 2>&1
# This is reuse of a qualified engine build, not a fresh engine compilation.
# Refuse source/header/object drift before compiling the new frontends.
test -f "$engine/compiler-inputs.json"
bash aros/library/verify-engine.sh "$engine/full-engine.o" "$engine" \
    "$("$AROS_CC" -print-prog-name=nm)"
sha256sum -c "$engine/sources.sha256" >"$out/engine-source-check.log"
cp "$engine/compiler-inputs.json" "$engine/sources.sha256" "$engine/engine-object.sha256" "$out/"
cp "$0" aros/engine/pool-test.c aros/engine/raid-read-test.c "$out/"
git rev-parse HEAD >"$out/source.txt"
git diff --binary >"$out/source.patch"
sha256sum "$engine/full-engine.o" "$AROS_NVPAIR_ARCHIVE" "$AROS_PRIVATE_PTHREAD" \
    "$AROS_CRYPTO_ARCHIVE" "$AROS_ZLIB_IMPORT" "$AROS_CC" >"$out/inputs.sha256"
"$("$AROS_CC" -print-prog-name=objcopy)" --redefine-sym main=oz_service_main \
    "$engine/full-engine.o" "$out/engine.o"
export AROS_ENGINE_OBJECT_DIR="$out/objects"
export AROS_GENERATED_INCLUDE="$engine/include"
export AROS_FULL_DISPATCHER=1
for unit in pool-test raid-read-test;do
    bash aros/engine/compile-object.sh "aros/engine/$unit.c"
    output=OpenZFSPoolTest
    [[ $unit != raid-read-test ]] || output=OpenZFSRAIDReadTest
    "$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -nix \
        "$out/objects/aros_engine_${unit}.c.o" "$out/engine.o" \
        "$AROS_NVPAIR_ARCHIVE" "$AROS_ZLIB_IMPORT" "$AROS_CRYPTO_ARCHIVE" \
        "$AROS_PRIVATE_PTHREAD" -ldos64 -lm -o "$out/$output"
done
sha256sum -c "$out/inputs.sha256" >"$out/input-stability.log"
sha256sum "$out/OpenZFSPoolTest" "$out/OpenZFSRAIDReadTest" >"$out/binaries.sha256"
printf 'PASS fresh large-pool frontends linked against verified normal engine\n'
