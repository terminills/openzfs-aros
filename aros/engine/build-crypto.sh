#!/usr/bin/env bash
# Private native BoringSSL crypto archive; never build in another project's tree.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:?explicit AROS C compiler required}
cxx=${AROS_CXX:?explicit AROS C++ compiler required}
sdk=$(realpath -e "${AROS_SDK:?matching AROS SDK required}")
src=$(realpath -e "${AROS_CRYPTO_SOURCE:?AROS-compatible BoringSSL source required}")
pthread_include=$(realpath -e "${AROS_PTHREAD_INCLUDE:?matching pthread headers required}")
go=${AROS_HOST_GO:-$(command -v go || true)}
[[ -n $go && -x $go ]] || { echo 'Host Go is required; set AROS_HOST_GO.' >&2; exit 2; }
jobs=${AROS_BUILD_JOBS:-8}
[[ $jobs =~ ^[0-9]+$ ]] && (( jobs >= 1 && jobs <= 32 ))
[[ -f $sdk/include/exec/types.h && -f $src/CMakeLists.txt && -f $src/include/openssl/evp.h ]]
source "$repo/aros/engine/storage-preflight.sh"
mkdir -p "$repo/build-aros"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/crypto-source.XXXXXXXX")
echo "Crypto build: $run"
cp "$0" "$run/build-crypto.sh"
"$cc" --version >"$run/compiler.txt"
"$cxx" --version >"$run/cxx.txt"
"$go" version >"$run/go.txt"
export GOCACHE="$run/go-cache" GOPATH="$run/go-path" GOTOOLCHAIN=local
printf 'source=%s\nsdk=%s\npthread_include=%s\njobs=%s\n' "$src" "$sdk" "$pthread_include" "$jobs" >"$run/inputs.txt"
# Hash the selected source tree, including local platform changes, not just HEAD.
find "$src" -type f ! -path '*/.git/*' -print0 | LC_ALL=C sort -z |
    xargs -0 sha256sum >"$run/source.sha256"
find "$pthread_include" -type f -name '*.h' -print0 | LC_ALL=C sort -z |
    xargs -0 sha256sum >>"$run/source.sha256"
sha256sum "$repo/aros/engine/crypto-pthread-limits.h" >>"$run/source.sha256"
# ADT's static pthread initializer omits trailing SMP semaphore fields, which
# C initializes to zero. Retain that warning without upstream's blanket Werror.
flags="--sysroot=$sdk -B$sdk/lib/ -I$pthread_include -include $repo/aros/engine/crypto-pthread-limits.h -mcmodel=large -Wno-error=missing-field-initializers"
cmake -S "$src" -B "$run/build" -G Ninja \
    -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
    -DGO_EXECUTABLE="$go" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
    -DCMAKE_BUILD_TYPE=Release -DOPENSSL_NO_ASM=ON \
    >"$run/configure.log" 2>&1
cmake --build "$run/build" --target crypto --parallel "$jobs" >"$run/build.log" 2>&1
sha256sum -c "$run/source.sha256" >"$run/source-after-check.log"
[[ -f $run/build/crypto/libcrypto.a ]]
sha256sum "$run/build/crypto/libcrypto.a" >"$run/library.sha256"
echo "AROS_CRYPTO_ARCHIVE=$run/build/crypto/libcrypto.a"
echo "AROS_CRYPTO_INCLUDE=$src/include"
