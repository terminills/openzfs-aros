#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source <(sed '$ d' build-audit/build-accounting-engine.sh)
source aros/engine/storage-preflight.sh
artifact_root=$(oz_artifact_root "$PWD/build-aros" "$AROS_ARTIFACT_ROOT")
run=$(mktemp -d "$artifact_root/mmake-cli.XXXXXXXX")
echo "Evidence: $run"
cp "$0" "$run/build.sh"
git rev-parse HEAD > "$run/source-head.txt"
git diff --binary > "$run/source.patch"
mkdir -p "$run/source" "$run/openzfs" "$run/gen/config"
configuration=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/bin/pc-x86_64-smp/gen/config
cp "$configuration/target.cfg" "$configuration/build.cfg" "$configuration/compiler.cfg" "$run/gen/config/"
sha256sum "$run/gen/config/"*.cfg > "$run/configuration.sha256"
platform=/home/terminills/Documents/GitHub/aros-adt-upstream-merge
ln -s "$PWD" "$run/source/openzfs"
ln -s "$platform/config" "$run/source/config"
ln -s "$platform/scripts" "$run/source/scripts"
ln -s "$platform/tools" "$run/source/tools"
cp build-audit/shared-tools.7guw1X1e/genmodule "$run/genmodule"
python3 "$platform/tools/genmf/genmf.py" "$platform/config/make.tmpl" mmakefile.src "$run/openzfs/mmakefile" > "$run/genmf.log" 2>&1
"$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -c build-audit/force-create/pipe-input/stubs/posixc_pipe2_stub.c -o "$run/pipe2.o"
for target in contrib-openzfs-zfs-quick contrib-openzfs-zpool-quick contrib-openzfs-startup-quick contrib-openzfs-startupmount-quick contrib-openzfs-exechelper-quick contrib-openzfs-runtimecheck-quick contrib-openzfs-hostidentity-quick contrib-openzfs-manager-quick contrib-openzfs-device-quick; do
make -j8 -f "$run/openzfs/mmakefile" "$target" TARGET="$target" \
    TOP=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean \
    SRCDIR="$run/source" CURDIR=openzfs GENDIR="$run/gen" \
    AROSDIR="$run/stage" DBGDIR="$run/debug" KOBJSDIR="$run/kobjs" GENINCDIR="$run/include" \
    AROS_DEVELOPER="$AROS_SDK" \
    GENMODULE="$run/genmodule" \
    AROS_TARGET_ARCH=pc AROS_TARGET_CPU=x86_64 AROS_HOST_ARCH=linux AROS_HOST_CPU=x86_64 \
    OPENZFS_NVPAIR_ARCHIVE="$PWD/build-audit/source-engine.phZ4CAdP/nvpair/libnvpair.a" \
    OPENZFS_TIRPC_SOURCE="$AROS_TIRPC_SOURCE" OPENZFS_TIRPC_INCLUDE="$AROS_TIRPC_SOURCE/tirpc" \
    OPENZFS_PTHREAD_INCLUDE="$AROS_PTHREAD_INCLUDE" OPENZFS_PTHREAD_LIBDIR="${AROS_PRIVATE_PTHREAD%/*}" \
    OPENZFS_PTHREAD_ARCHIVE="$AROS_PRIVATE_PTHREAD" \
    OPENZFS_CRYPTO_INCLUDE="$AROS_CRYPTO_INCLUDE" OPENZFS_CRYPTO_ARCHIVE="$AROS_CRYPTO_ARCHIVE" \
    OPENZFS_ZLIB_ARCHIVE="$AROS_SDK/lib/libz1.a" OPENZFS_PIPE2_OBJECT="$run/pipe2.o" \
    >> "$run/make.log" 2>&1
done
sha256sum "$run/stage/C/"* "$run/stage/Tools/OpenZFS/ExecHelper" "$run/stage/Tools/OpenZFSManagerNative" > "$run/binaries.sha256"
echo "PASS fresh generated MetaMake CLI build: $run"
