#!/usr/bin/env bash
# Fresh upstream engine objects, never seeded from a previous test build.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
: "${AROS_SDK:?explicit ABI-matched AROS SDK required}"
case ${AROS_PTHREAD_BACKEND:-private} in
    sdk)
        if [[ -n ${AROS_PRIVATE_PTHREAD:-}${AROS_PTHREAD_INCLUDE:-} ]]; then
            echo 'SDK pthread mode cannot be combined with private pthread inputs' >&2
            exit 1
        fi
        pthread_archive="$AROS_SDK/lib/libpthread.a"
        export AROS_PTHREAD_INCLUDE="$AROS_SDK/include"
        [[ -f $pthread_archive && -f $AROS_PTHREAD_INCLUDE/pthread.h ]]
        ;;
    private)
        : "${AROS_PRIVATE_PTHREAD:?complete private pthread archive required}"
        : "${AROS_PTHREAD_INCLUDE:?matching private pthread headers required}"
        cmp "$(dirname "$AROS_PRIVATE_PTHREAD")/source/pthread.h" "$AROS_PTHREAD_INCLUDE/pthread.h"
        pthread_archive=$AROS_PRIVATE_PTHREAD
        ;;
    *) echo 'Unsupported pthread backend' >&2; exit 1 ;;
esac
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
python3 aros/platform/check-sdk-links.py --cc "${AROS_CC:?explicit AROS compiler required}" \
    --sdk "$AROS_SDK" --output-parent "$artifact_root" --profile engine
run=$(mktemp -d "$artifact_root/source-engine.XXXXXXXX")
echo "Evidence: $run"
mkdir "$run/engine" "$run/include" "$run/logs"
sha256sum "$pthread_archive" "$AROS_PTHREAD_INCLUDE/pthread.h" > "$run/pthread-inputs.sha256"
printf 'pthread_backend=%s\n' "${AROS_PTHREAD_BACKEND:-private}" > "$run/pthread-backend.txt"
if [[ -z ${AROS_NVPAIR_ARCHIVE:-} ]];then
    export AROS_NVPAIR_ARCHIVE="$run/nvpair/libnvpair.a"
    AROS_NVPAIR_OUTPUT_DIR="$run/nvpair" AROS_NVPAIR_OBJECT_DIR="$run/nvpair/objects" \
        bash aros/engine/build-nvpair.sh >"$run/nvpair-build.log" 2>&1
fi
export AROS_ENGINE_OBJECT_DIR="$run/engine"
export AROS_GENERATED_INCLUDE="$run/include"
export AROS_OUTPUT_DIR="$run/service"
export AROS_LINKED_OBJECT="$run/full-engine.o"
export AROS_FULL_DISPATCHER=1
cp "$0" "$run/build-source.sh"
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
# Upstream requires a relative output name. Run in the output directory so
# an external artifact root never becomes an invalid absolute GITREV path.
(cd "$run" && bash "$repo/scripts/make_gitrev.sh" include/zfs_gitrev.h)
sha256sum "$run/include/zfs_gitrev.h" >"$run/generated.sha256"
# Same source inventories as the engine linker, including portable x86 crypto,
# Unicode, AVL and zstd. ZPL/dispatcher variants are rebuilt by the linker.
{
    awk '/^[[:space:]]+[^#]*\.c([[:space:]]|$)/ {gsub(/%D%/,"lib/libzpool",$1);print $1}' lib/libzpool/Makefile.am
    awk '/^[[:space:]]+module\/.*\.(c|S)([[:space:]]|$)/ {print $1}' \
        lib/libicp/Makefile.am lib/libavl/Makefile.am lib/libunicode/Makefile.am lib/libzstd/Makefile.am |
        awk '!/asm-(aarch64|arm|ppc64)\//'
} | LC_ALL=C sort -u >"$run/sources.txt"
failed=0
while IFS= read -r source;do
    sha256sum "$source" >>"$run/sources.sha256"
    if bash aros/engine/compile-object.sh "$source" >"$run/logs/${source//\//_}.log" 2>&1;then
        printf 'PASS %s\n' "$source" >>"$run/compile-summary.txt"
    else
        printf 'FAIL %s\n' "$source" | tee -a "$run/compile-summary.txt"
        failed=1
    fi
done <"$run/sources.txt"
[[ $failed == 0 ]]
bash aros/engine/build-management.sh >"$run/link.log" 2>&1
sha256sum "$run/service/OpenZFSServiceNative" "$run/service/OpenZFSManagerNative" \
    "$pthread_archive" "$AROS_NVPAIR_ARCHIVE" >"$run/binaries.sha256"
sha256sum -c "$run/pthread-inputs.sha256" > "$run/pthread-after-check.log"
echo "PASS fresh-source engine and Manager build: $run"
