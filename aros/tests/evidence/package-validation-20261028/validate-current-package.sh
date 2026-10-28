#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
root=$PWD
output_parent=$(realpath -e "${OZ_PACKAGE_OUTPUT_PARENT:-$root/build-audit}")
mode=(--validate-only)
if [[ ${1:-} == --stage ]]; then
    source aros/engine/storage-preflight.sh
    oz_storage_preflight "$output_parent"
    mode=()
elif [[ $# != 0 ]]; then
    echo 'Usage: validate-current-package.sh [--stage]' >&2
    exit 2
fi
assembly=$(mktemp -d "$root/build-audit/qualification-links.XXXXXXXX")
mkdir -p "$assembly/stage/C" "$assembly/stage/Tools" "$assembly/stage/L" "$assembly/stage/Libs" "$assembly/package"
for f in "$root"/build-audit/mmake-cli.hG2vWP1T/stage/C/*; do
    [[ ${f##*/} == zdb ]] || ln -s "$f" "$assembly/stage/C/"
done
ln -s "$root/build-audit/mmake-debugger.3zlIqhLI/stage/C/zdb" "$assembly/stage/C/zdb"
for f in "$root"/build-audit/mmake-cli.hG2vWP1T/stage/Tools/*; do ln -s "$f" "$assembly/stage/Tools/"; done
for n in OpenZFSServiceNative openzfs.library service-build-options.txt; do
    ln -s "$root/build-audit/current-service.RarHl71z/$n" "$assembly/package/$n"
done
ln -s "$root/build-audit/handler-build.jxfoSGcL/OpenZFS-Handler" "$assembly/stage/L/OpenZFS-handler"
for pair in posix-flush-source.j099bkwp/posixc.library stdc-source.px9y2jvo/stdc.library stdcio-source.uo0iplm_/stdcio.library dos64-source.jn47era4/dos64.library; do
    ln -s "$root/build-audit/$pair" "$assembly/stage/Libs/"
done
echo "Assembly links: $assembly"
python3 aros/distribution/stage-mmake.py "$assembly" \
    build-audit/gui-replay-20261028 build-audit/public-service-run.rSDRJJ6u \
    "${mode[@]}" --output-parent "$output_parent" \
    --encrypted-root-run build-audit/public-service-run.NQlJU1JU \
    --native-zdb-run build-audit/zdb-label-run.QRkxXhcQ \
    --fat-handler /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/flush-overlay.tufmbxjd/fat.handler \
    --fat-source-snapshot /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/flush-overlay.tufmbxjd \
    --dos-package /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/dos-revision.Pa9psAFv/aros-base.pkg.xz \
    --dos-source-snapshot /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/dos-revision.Pa9psAFv \
    --posix-source-snapshot build-audit/posix-flush-source.j099bkwp \
    --stdc-source-snapshot build-audit/stdc-source.px9y2jvo \
    --stdcio-source-snapshot build-audit/stdcio-source.uo0iplm_ \
    --dos64-source-snapshot build-audit/dos64-source.jn47era4 \
    --afs-handler build-audit/afs-source.wg6ei6nj/afs-handler \
    --afs-source-snapshot build-audit/afs-source.wg6ei6nj
