#!/usr/bin/env bash
set -euo pipefail
repo=/mnt/short-term/openzfs-bootenv-candidate-20261025
qualified=$repo/build-audit
work=/run/media/terminills/Amiwest/openzfs-source-replay-20261028
fresh=$work/build/shared-engine.ld5CZgji
source "$repo/aros/engine/storage-preflight.sh"
oz_storage_preflight "$work/build"
assembly=$(mktemp -d "$work/build/fresh-package-links.XXXXXXXX")
mkdir -p "$assembly/stage/"{C,Tools,L,Libs} "$assembly/package"
for f in "$work"/build/fresh-mmake-cli.yqLYANrY/stage/C/*;do
 [[ ${f##*/} == zdb ]] || ln -s "$f" "$assembly/stage/C/"
done
ln -s "$fresh/zdb" "$assembly/stage/C/zdb"
for f in "$work"/build/fresh-mmake-cli.yqLYANrY/stage/Tools/*;do
 [[ ${f##*/} == OpenZFSManagerNative ]] || ln -s "$f" "$assembly/stage/Tools/"
done
ln -s "$work/build/fresh-mmake-cli.yqLYANrY/stage/Tools/OpenZFSManagerNative" "$assembly/stage/Tools/OpenZFSManagerNative"
for n in OpenZFSServiceNative openzfs.library service-build-options.txt;do
 ln -s "$fresh/$n" "$assembly/package/$n"
done
ln -s "$qualified/handler-build.jxfoSGcL/OpenZFS-Handler" "$assembly/stage/L/OpenZFS-handler"
for pair in posix-flush-source.j099bkwp/posixc.library stdc-source.px9y2jvo/stdc.library stdcio-source.uo0iplm_/stdcio.library dos64-source.jn47era4/dos64.library;do
 ln -s "$qualified/$pair" "$assembly/stage/Libs/"
done
mode=(--validate-only)
if [[ ${1:-} == --stage ]];then mode=();elif [[ $# != 0 ]];then exit 2;fi
echo "Assembly links: $assembly"
python3 "$repo/aros/distribution/stage-mmake.py" "$assembly" \
 "$work/build/native-gui-run.8dKQw2DA" "$work/build/public-service-run.ahyOZbBB" \
 "${mode[@]}" --output-parent "$work/build" \
 --encrypted-root-run "$work/build/public-service-run.mDaACflJ" \
 --native-zdb-run "$work/build/zdb-label-run.068Hkwv9" \
 --fat-handler /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/flush-overlay.tufmbxjd/fat.handler \
 --fat-source-snapshot /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/flush-overlay.tufmbxjd \
 --dos-package /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/dos-revision.Pa9psAFv/aros-base.pkg.xz \
 --dos-source-snapshot /mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/dos-revision.Pa9psAFv \
 --posix-source-snapshot "$qualified/posix-flush-source.j099bkwp" \
 --stdc-source-snapshot "$qualified/stdc-source.px9y2jvo" \
 --stdcio-source-snapshot "$qualified/stdcio-source.uo0iplm_" \
 --dos64-source-snapshot "$qualified/dos64-source.jn47era4" \
 --afs-handler "$qualified/afs-source.wg6ei6nj/afs-handler" \
 --afs-source-snapshot "$qualified/afs-source.wg6ei6nj"
