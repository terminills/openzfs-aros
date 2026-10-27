#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source build-audit/dependencies.YF6P1Tca/dependencies.env
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$PWD/build-aros" "$PWD/build-audit")
run=$(mktemp -d "$root/mmake-debugger.XXXXXXXX")
echo "Evidence: $run"
cp "$0" "$run/build.sh"
mkdir -p "$run/source" "$run/gen/config"
configuration=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/bin/pc-x86_64-smp/gen/config
cp "$configuration/target.cfg" "$configuration/build.cfg" "$configuration/compiler.cfg" "$run/gen/config/"
mkdir -p "$run/gen/compiler/libinit" "$run/gen/openzfs/aros/library/openzfs/include/"{proto,clib,inline,defines,pragmas}
cp /home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/bin/pc-x86_64-smp/gen/compiler/libinit/libentry.o "$run/gen/compiler/libinit/"
platform=/home/terminills/Documents/GitHub/aros-adt-upstream-merge
ln -s "$PWD" "$run/source/openzfs"
for part in config scripts tools;do ln -s "$platform/$part" "$run/source/$part";done
cp build-audit/shared-tools.7guw1X1e/genmodule "$run/genmodule"
python3 "$platform/tools/genmf/genmf.py" "$platform/config/make.tmpl" \
    aros/library/mmakefile.src "$run/mmakefile" >"$run/genmf.log" 2>&1
for target in contrib-openzfs-library-quick contrib-openzfs-service-quick contrib-openzfs-zdb-quick;do
    make -j8 -f "$run/mmakefile" "$target" TARGET="$target" \
        TOP=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean \
        SRCDIR="$run/source" CURDIR=openzfs/aros/library GENDIR="$run/gen" \
        AROSDIR="$run/stage" DBGDIR="$run/debug" KOBJSDIR="$run/kobjs" GENINCDIR="$run/include" \
        AROS_DEVELOPER="$AROS_SDK" GENMODULE="$run/genmodule" \
        AROS_TARGET_ARCH=pc AROS_TARGET_CPU=x86_64 AROS_HOST_ARCH=linux AROS_HOST_CPU=x86_64 \
        OPENZFS_DEPENDENCIES="$PWD/build-audit/dependencies.YF6P1Tca/dependencies.mk" \
        OPENZFS_ENGINE_OBJECT="$PWD/build-audit/mmake-engine.hZ1s42So/aros/engine/full/full-engine.o" \
        OPENZFS_SERVICE_BUILD="$PWD/build-audit/mmake-engine.hZ1s42So/aros/engine/full" \
        >>"$run/make.log" 2>&1
done
sha256sum "$run/stage/Libs/openzfs.library" "$run/stage/C/zdb" \
    "$run/stage/C/OpenZFSServiceNative" >"$run/binaries.sha256"
echo 'PASS generated MetaMake library and debugger build'
