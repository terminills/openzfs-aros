#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source aros/engine/storage-preflight.sh
oz_storage_preflight "$PWD/build-audit"
qualified=$(mktemp -d "$PWD/build-audit/qualified-inputs.XXXXXXXX")
printf 'Qualified input assembly: %s\n' "$qualified"
mkdir -p "$qualified/stage/L" "$qualified/stage/Libs" "$qualified/package"
cp "$0" "$qualified/assembly.sh"
cp -a build-audit/mmake-cli.ldOQ3FYG/stage/C "$qualified/stage/C"
cp -a build-audit/mmake-cli.ldOQ3FYG/stage/Tools "$qualified/stage/Tools"
cp build-audit/mmake-debugger.JLOOcjrH/stage/C/zdb "$qualified/stage/C/"
cp build-audit/debugger-service.0umlrjD3/{OpenZFSServiceNative,openzfs.library,service-build-options.txt} "$qualified/package/"
cp build-audit/handler-build.jxfoSGcL/OpenZFS-Handler "$qualified/stage/L/OpenZFS-handler"
cp /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/posix-source.krrt6d62/posixc.library "$qualified/stage/Libs/"
python3 aros/distribution/stage-mmake.py "$qualified" \
    build-audit/native-gui-run.DKW8o83D build-audit/public-service-run.6iuhXkGy \
    --encrypted-root-run build-audit/public-service-run.CS0D8oKi \
    --native-zdb-run build-audit/zdb-label-run.6Y7sxTkv \
    --fat-handler /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/flush-overlay.tufmbxjd/fat.handler \
    --fat-source-snapshot /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/flush-overlay.tufmbxjd \
    --dos-package /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/dos-revision.Pa9psAFv/aros-base.pkg.xz \
    --dos-source-snapshot /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/dos-revision.Pa9psAFv \
    --posix-source-snapshot "$PWD/build-audit/posix-localized.e7euqhlb" \
    --afs-handler build-audit/afs-source.wg6ei6nj/afs-handler \
    --afs-source-snapshot build-audit/afs-source.wg6ei6nj \
    --output-parent "$PWD/build-audit"
