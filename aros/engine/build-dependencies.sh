#!/usr/bin/env bash
# Build private dependency archives together; never install into a shared SDK.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
mode=${1:-build}
[[ $# -le 1 && ( $mode == build || $mode == --check ) ]] || { echo 'Usage: build-dependencies.sh [--check]' >&2; exit 2; }
: "${AROS_CC:?explicit AROS C compiler required}"
: "${AROS_CXX:?explicit AROS C++ compiler required}"
: "${AROS_SDK:?matching AROS SDK required}"
: "${AROS_PTHREAD_SOURCE:?complete matching pthread source required}"
: "${AROS_ARTIFACT_ROOT:?external artifact root required}"
AROS_CC=$(realpath -e "$AROS_CC")
AROS_CXX=$(realpath -e "$AROS_CXX")
AROS_SDK=$(realpath -e "$AROS_SDK")
AROS_PTHREAD_SOURCE=$(realpath -e "$AROS_PTHREAD_SOURCE")
export AROS_CC AROS_CXX AROS_SDK AROS_PTHREAD_SOURCE
export AROS_HOST_GO=${AROS_HOST_GO:-$(command -v go || true)}
[[ -n $AROS_HOST_GO && -x $AROS_HOST_GO ]] || { echo 'Set AROS_HOST_GO to host Go' >&2; exit 2; }
AROS_HOST_GO=$(realpath -e "$AROS_HOST_GO")
[[ -x $AROS_CC && -x $AROS_CXX && -f $AROS_SDK/include/exec/types.h && -d $AROS_SDK/lib ]]
[[ -f $AROS_PTHREAD_SOURCE/pthread.c && -f $AROS_PTHREAD_SOURCE/pthread.h && -f $AROS_PTHREAD_SOURCE/mmakefile.src ]]
export AROS_BUILD_JOBS=${AROS_BUILD_JOBS:-8}
[[ $AROS_BUILD_JOBS =~ ^[0-9]+$ ]] && (( AROS_BUILD_JOBS >= 1 && AROS_BUILD_JOBS <= 32 ))
for tool in cmake ninja perl git tar sha256sum;do command -v "$tool" >/dev/null;done
if [[ -n ${AROS_PTHREAD_DOSTAGS:-} ]];then [[ -f $AROS_PTHREAD_DOSTAGS ]];fi
if [[ -n ${AROS_TIRPC_ARCHIVE:-} ]];then
    [[ -f $AROS_TIRPC_ARCHIVE ]]
    echo "b47d3ac19d3549e54a05d0019a6c400674da716123858cfdb6d3bdd70a66c702  $AROS_TIRPC_ARCHIVE" | sha256sum -c -
fi
# The enclosing AROS make macros use unquoted path lists. Fail clearly rather
# than generate a make include which interprets path characters as syntax.
for path in "$repo" "$AROS_CC" "$AROS_CXX" "$AROS_SDK" "$AROS_PTHREAD_SOURCE" "$AROS_ARTIFACT_ROOT";do
    [[ $path != *[[:space:]\#\$\:\\]* ]] || { echo 'Dependency paths must not contain make-special characters or whitespace' >&2; exit 2; }
done
source "$repo/aros/engine/storage-preflight.sh"
root=$(oz_artifact_root "$repo" "$AROS_ARTIFACT_ROOT")
if [[ $mode == --check ]];then echo 'PASS dependency source/tool/storage preflight; no build or network access';exit 0;fi
run=$(mktemp -d "$root/dependencies.XXXXXXXX")
echo "Dependency build: $run"
export AROS_ARTIFACT_ROOT=$run
cp "$0" "$run/build-dependencies.sh"
git -C "$repo" rev-parse HEAD >"$run/source-head.txt"
git -C "$repo" diff --binary >"$run/source.patch"
sha256sum "$AROS_CC" "$AROS_CXX" "$AROS_HOST_GO" >"$run/host-tools.sha256"
printf 'sdk=%s\npthread_source=%s\njobs=%s\n' "$AROS_SDK" "$AROS_PTHREAD_SOURCE" "$AROS_BUILD_JOBS" >"$run/inputs.txt"

bash "$repo/aros/engine/build-private-pthread.sh" >"$run/pthread.log" 2>&1
shopt -s nullglob
pthread=("$run"/pthread-consistent.*)
[[ ${#pthread[@]} == 1 && -f ${pthread[0]}/libpthread.a ]]
export AROS_PRIVATE_PTHREAD=${pthread[0]}/libpthread.a AROS_PTHREAD_INCLUDE=${pthread[0]}/source

# Always prepare the pinned upstream revision afresh, including when the
# explicitly selected repository is a local mirror. Do not consume an old .a.
bash "$repo/aros/engine/prepare-crypto-source.sh" >"$run/crypto-prepare.log" 2>&1
crypto_input=("$run"/crypto-input.*)
[[ ${#crypto_input[@]} == 1 ]]
export AROS_CRYPTO_SOURCE=${crypto_input[0]}/source
bash "$repo/aros/engine/build-crypto.sh" >"$run/crypto.log" 2>&1
crypto=("$run"/crypto-source.*)
[[ ${#crypto[@]} == 1 && -f ${crypto[0]}/build/crypto/libcrypto.a ]]
export AROS_CRYPTO_ARCHIVE=${crypto[0]}/build/crypto/libcrypto.a AROS_CRYPTO_INCLUDE=$AROS_CRYPTO_SOURCE/include

mkdir "$run/tirpc" "$run/nvpair"
if [[ -n ${AROS_TIRPC_ARCHIVE:-} ]];then cp "$AROS_TIRPC_ARCHIVE" "$run/tirpc/libtirpc-1.3.7.tar.bz2";fi
export AROS_TIRPC_ARCHIVE=$run/tirpc/libtirpc-1.3.7.tar.bz2 AROS_TIRPC_SOURCE=$run/tirpc/libtirpc-1.3.7
export AROS_NVPAIR_OUTPUT_DIR=$run/nvpair AROS_NVPAIR_OBJECT_DIR=$run/nvpair/objects AROS_NVPAIR_ARCHIVE=$run/nvpair/libnvpair.a
bash "$repo/aros/engine/build-nvpair.sh" >"$run/nvpair.log" 2>&1
bash "$repo/aros/engine/build-pbkdf2-test.sh" >"$run/pbkdf2.log" 2>&1
sha256sum -c "$run/host-tools.sha256" >"$run/host-tools-after.log"

# Publication is last: an interrupted/failed build never offers a usable
# dependency manifest. These describe source builds, not runtime acceptance.
for variable in AROS_CC AROS_CXX AROS_SDK AROS_HOST_GO AROS_PRIVATE_PTHREAD AROS_PTHREAD_INCLUDE AROS_CRYPTO_SOURCE AROS_CRYPTO_ARCHIVE AROS_CRYPTO_INCLUDE AROS_TIRPC_SOURCE AROS_TIRPC_ARCHIVE AROS_NVPAIR_ARCHIVE;do
    printf 'export %s=%q\n' "$variable" "${!variable}"
done >"$run/dependencies.env"
{
    printf 'OPENZFS_PTHREAD_INCLUDE := %s\n' "$AROS_PTHREAD_INCLUDE"
    printf 'OPENZFS_PTHREAD_ARCHIVE := %s\n' "$AROS_PRIVATE_PTHREAD"
    printf 'OPENZFS_PTHREAD_LIBDIR := %s\n' "${AROS_PRIVATE_PTHREAD%/*}"
    printf 'OPENZFS_CRYPTO_INCLUDE := %s\n' "$AROS_CRYPTO_INCLUDE"
    printf 'OPENZFS_CRYPTO_ARCHIVE := %s\n' "$AROS_CRYPTO_ARCHIVE"
    printf 'OPENZFS_TIRPC_SOURCE := %s\n' "$AROS_TIRPC_SOURCE"
    printf 'OPENZFS_TIRPC_INCLUDE := %s/tirpc\n' "$AROS_TIRPC_SOURCE"
    printf 'OPENZFS_NVPAIR_ARCHIVE := %s\n' "$AROS_NVPAIR_ARCHIVE"
    printf 'OPENZFS_NVPAIR_LIBDIR := %s\n' "${AROS_NVPAIR_ARCHIVE%/*}"
} >"$run/dependencies.mk"
sha256sum "$AROS_PRIVATE_PTHREAD" "$AROS_CRYPTO_ARCHIVE" "$AROS_NVPAIR_ARCHIVE" "$run/dependencies.env" "$run/dependencies.mk" >"$run/outputs.sha256"
echo "PASS dependency source builds; native acceptance still required: $run"
