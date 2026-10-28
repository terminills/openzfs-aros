#!/usr/bin/env bash
# Rebuild exported sources with explicit external SDK/tools; no fetch or install.
set -euo pipefail
[[ $# == 1 ]] || { echo 'Usage: rebuild-exported-dependencies.sh SOURCE_EXPORT' >&2; exit 2; }
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
snapshot=$(realpath -e "$1")
: "${AROS_CC:?explicit C compiler required}" "${AROS_CXX:?explicit C++ compiler required}"
: "${AROS_SDK:?matching SDK required}" "${AROS_HOST_GO:?explicit host Go required}"
: "${AROS_ARTIFACT_ROOT:?external output directory required}"
export AROS_BUILD_JOBS=${AROS_BUILD_JOBS:-8}
[[ $AROS_BUILD_JOBS =~ ^[0-9]+$ ]] && ((AROS_BUILD_JOBS >= 1 && AROS_BUILD_JOBS <= 32))
# Treat retained manifests as data. Do not source the original absolute-path env.
python3 - "$snapshot" <<'PY'
import hashlib,re,sys
from pathlib import Path
root=Path(sys.argv[1]); seen=set()
for line in (root/'SHA256SUMS').read_text().splitlines():
    m=re.fullmatch(r'([0-9a-f]{64})  (.+)',line)
    if not m: raise SystemExit('Malformed export manifest')
    rel=Path(m[2]);p=root/rel
    if rel.is_absolute() or '..' in rel.parts or rel in seen or p.is_symlink() or not p.resolve().is_relative_to(root):
        raise SystemExit('Unsafe or duplicate export path')
    with p.open('rb') as f: actual=hashlib.file_digest(f,'sha256').hexdigest()
    if actual!=m[1]:raise SystemExit('Export input changed: '+str(rel))
    seen.add(rel)
actual={p.relative_to(root) for p in root.rglob('*') if p.is_file() and p!=root/'SHA256SUMS'}
if not seen or actual!=seen:raise SystemExit('Incomplete export inventory')
PY
[[ -f $snapshot/Pthread/pthread.c && -f $snapshot/Crypto/CMakeLists.txt && -f $snapshot/libtirpc-1.3.7.tar.bz2 ]]
source "$repo/aros/engine/storage-preflight.sh"
root=$(oz_artifact_root "$repo" "$AROS_ARTIFACT_ROOT")
run=$(mktemp -d "$root/exported-dependencies.XXXXXXXX")
echo "Exported dependency rebuild: $run"
cp "$0" "$run/rebuild-exported-dependencies.sh"
cp "$snapshot/SHA256SUMS" "$run/export-inputs.sha256"
git -C "$repo" rev-parse HEAD >"$run/source-head.txt"
git -C "$repo" diff --binary >"$run/source.patch"
export AROS_ARTIFACT_ROOT=$run AROS_PTHREAD_SOURCE=$snapshot/Pthread
unset AROS_PTHREAD_DOSTAGS
if [[ -f $snapshot/Pthread/dos/dostags.h ]];then export AROS_PTHREAD_DOSTAGS=$snapshot/Pthread/dos/dostags.h;fi
bash "$repo/aros/engine/build-private-pthread.sh" >"$run/pthread.log" 2>&1
shopt -s nullglob
pthread=("$run"/pthread-consistent.*)
[[ ${#pthread[@]} == 1 ]]
export AROS_PRIVATE_PTHREAD=${pthread[0]}/libpthread.a AROS_PTHREAD_INCLUDE=${pthread[0]}/source
export AROS_CRYPTO_SOURCE=$snapshot/Crypto AROS_CRYPTO_INCLUDE=$snapshot/Crypto/include
bash "$repo/aros/engine/build-crypto.sh" >"$run/crypto.log" 2>&1
crypto=("$run"/crypto-source.*)
[[ ${#crypto[@]} == 1 ]]
export AROS_CRYPTO_ARCHIVE=${crypto[0]}/build/crypto/libcrypto.a
mkdir "$run/nvpair" "$run/tirpc"
export AROS_TIRPC_ARCHIVE=$snapshot/libtirpc-1.3.7.tar.bz2 AROS_TIRPC_SOURCE=$run/tirpc/libtirpc-1.3.7
export AROS_NVPAIR_OUTPUT_DIR=$run/nvpair AROS_NVPAIR_OBJECT_DIR=$run/nvpair/objects AROS_NVPAIR_ARCHIVE=$run/nvpair/libnvpair.a
bash "$repo/aros/engine/build-nvpair.sh" >"$run/nvpair.log" 2>&1
bash "$repo/aros/engine/build-pbkdf2-test.sh" >"$run/pbkdf2.log" 2>&1
cmp "$snapshot/SHA256SUMS" "$run/export-inputs.sha256"
(cd "$snapshot"; sha256sum -c "$run/export-inputs.sha256") >"$run/export-input-stability.log"
# Publish only after every source build and link succeeds.
for variable in AROS_CC AROS_CXX AROS_SDK AROS_HOST_GO AROS_PRIVATE_PTHREAD AROS_PTHREAD_INCLUDE AROS_CRYPTO_SOURCE AROS_CRYPTO_INCLUDE AROS_CRYPTO_ARCHIVE AROS_TIRPC_ARCHIVE AROS_TIRPC_SOURCE AROS_NVPAIR_ARCHIVE;do
    printf 'export %s=%q\n' "$variable" "${!variable}"
done >"$run/dependencies.env"
sha256sum "$AROS_PRIVATE_PTHREAD" "$AROS_CRYPTO_ARCHIVE" "$AROS_NVPAIR_ARCHIVE" "$run/dependencies.env" >"$run/outputs.sha256"
echo "PASS exported dependency source builds; target runtime still required: $run"
