#!/usr/bin/env bash
set -euo pipefail
work=/run/media/terminills/Amiwest/openzfs-source-replay-20261028
qualified=/mnt/short-term/openzfs-bootenv-candidate-20261025/build-audit
cd "$work/source"
source "$work/build/exported-dependencies.jjAcgPAs/dependencies.env"
source aros/engine/storage-preflight.sh
artifact_root=$(oz_artifact_root "$PWD" "$work/build")
run=$(mktemp -d "$artifact_root/fresh-mmake-cli.XXXXXXXX")
echo "Evidence: $run"
cp "$0" "$run/build.sh"
git rev-parse HEAD >"$run/source-head.txt"
git diff --binary >"$run/source.patch"
mkdir -p "$run/source" "$run/openzfs" "$run/gen/config"
configuration=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/bin/pc-x86_64-smp/gen/config
cp "$configuration/target.cfg" "$configuration/build.cfg" "$configuration/compiler.cfg" "$run/gen/config/"
sha256sum "$run/gen/config/"*.cfg >"$run/configuration.sha256"
platform=/home/terminills/Documents/GitHub/aros-adt-upstream-merge
ln -s "$PWD" "$run/source/openzfs"
for part in config scripts tools;do ln -s "$platform/$part" "$run/source/$part";done
cp "$qualified/shared-tools.7guw1X1e/genmodule" "$run/genmodule"
python3 "$platform/tools/genmf/genmf.py" "$platform/config/make.tmpl" mmakefile.src "$run/openzfs/mmakefile" >"$run/genmf.log" 2>&1
python3 /mnt/short-term/openzfs-bootenv-candidate-20261025/aros/cli/build-posix-import.py \
 "$qualified/posix-flush-source.j099bkwp" --library "$qualified/posix-flush-source.j099bkwp/posixc.library" \
 --cc "$AROS_CC" --sdk "$AROS_SDK" --genmodule "$run/genmodule" --output-parent "$work/build" >"$run/pipe-import.log"
pipe_import=$(sed -n 's/^Evidence: //p' "$run/pipe-import.log")
[[ -d $pipe_import ]]
pipe_source="$pipe_import/stubs/posixc_pipe2_stub.c"
"$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -c "$pipe_source" -o "$run/pipe2.o"
sha256sum "$pipe_source" "$AROS_NVPAIR_ARCHIVE" "$AROS_PRIVATE_PTHREAD" "$AROS_CRYPTO_ARCHIVE" "$run/genmodule" >"$run/inputs.sha256"
for target in contrib-openzfs-zfs-quick contrib-openzfs-zpool-quick contrib-openzfs-startup-quick contrib-openzfs-startupmount-quick contrib-openzfs-exechelper-quick contrib-openzfs-runtimecheck-quick contrib-openzfs-hostidentity-quick contrib-openzfs-manager-quick contrib-openzfs-device-quick;do
 make -j8 -f "$run/openzfs/mmakefile" "$target" TARGET="$target" \
 TOP=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean \
 SRCDIR="$run/source" CURDIR=openzfs GENDIR="$run/gen" \
 AROSDIR="$run/stage" DBGDIR="$run/debug" KOBJSDIR="$run/kobjs" GENINCDIR="$run/include" \
 AROS_DEVELOPER="$AROS_SDK" GENMODULE="$run/genmodule" \
 AROS_TARGET_ARCH=pc AROS_TARGET_CPU=x86_64 AROS_HOST_ARCH=linux AROS_HOST_CPU=x86_64 \
 OPENZFS_NVPAIR_ARCHIVE="$AROS_NVPAIR_ARCHIVE" OPENZFS_TIRPC_SOURCE="$AROS_TIRPC_SOURCE" \
 OPENZFS_TIRPC_INCLUDE="$AROS_TIRPC_SOURCE/tirpc" OPENZFS_PTHREAD_INCLUDE="$AROS_PTHREAD_INCLUDE" \
 OPENZFS_PTHREAD_LIBDIR="${AROS_PRIVATE_PTHREAD%/*}" OPENZFS_PTHREAD_ARCHIVE="$AROS_PRIVATE_PTHREAD" \
 OPENZFS_CRYPTO_INCLUDE="$AROS_CRYPTO_INCLUDE" OPENZFS_CRYPTO_ARCHIVE="$AROS_CRYPTO_ARCHIVE" \
 OPENZFS_ZLIB_ARCHIVE="$AROS_SDK/lib/libz1.a" OPENZFS_PIPE2_OBJECT="$run/pipe2.o" \
 >>"$run/make.log" 2>&1
done
sha256sum -c "$run/inputs.sha256" >"$run/unchanged.log"
sha256sum "$run/stage/C/"* "$run/stage/Tools/OpenZFS/ExecHelper" "$run/stage/Tools/OpenZFSManagerNative" >"$run/binaries.sha256"
echo "PASS fresh exported-dependency MetaMake CLI build: $run"
