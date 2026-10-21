#!/usr/bin/env bash
# Private shared-engine candidate. Does not replace an installed service.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
gen=${AROS_GENMODULE:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/linux-x86_64/tools/genmodule}
engine=${AROS_LINKED_OBJECT:?full normal service engine object required}
symbols=$(nm "$engine")
if [[ $symbols == *'oz_test_strict_flush_enabled'* ]];then
    echo 'Refusing an unqualified strict-flush experimental engine.' >&2;exit 2
fi
normal=${AROS_SERVICE_BUILD_DIR:?normal full service build provenance required}
rg -Fxq 'service_test_modes=0' "$normal/service-build-options.txt"
rg -Fxq 'full_dispatcher=1' "$normal/service-build-options.txt"
read -r expected_engine_hash _ <"$normal/engine-object.sha256"
actual_engine_hash=$(sha256sum "$engine")
[[ $expected_engine_hash =~ ^[0-9a-f]{64}$ && ${actual_engine_hash%% *} == "$expected_engine_hash" ]]
nvpair=${AROS_NVPAIR_ARCHIVE:?nvpair archive required}
pthread=${AROS_PRIVATE_PTHREAD:?matching pthread archive required}
crypto=${AROS_CRYPTO_ARCHIVE:-/opt/aros-workspace/chromium-aros/src/out/aros-chrome/obj/third_party/boringssl/libboringssl.a}
zlib=${AROS_ZLIB_IMPORT:-/opt/aros-workspace/aros-adt-hosted-clean/bin/linux-x86_64/AROS/Developer/lib/libz1.a}
[[ $symbols == *' T main'* && $symbols == *' T zfsdev_ioctl_common'* ]]
if [[ $symbols == *'__wrap_'* || $symbols == *'oz_test_admission_hold'* ]];then
    echo 'Refusing a fault-injected engine object.' >&2;exit 2
fi
out=$(mktemp -d "$artifact_root/shared-engine.XXXXXXXX")
echo "Shared engine candidate: $out"
mkdir -p "$out/gen" "$out/include"/{proto,clib,inline,defines,pragmas}
for operation in writefiles writelibdefs;do
    "$gen" -c aros/library/openzfs.conf -d "$out/gen" "$operation" openzfs library
done
"$gen" -c aros/library/openzfs.conf -d "$out/include" writeincludes openzfs library
"${cc%gcc}objcopy" --redefine-sym main=oz_service_main "$engine" "$out/engine.o"
flags=(--sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra -I"$out/include" -I"$out/gen")
"$cc" "${flags[@]}" -c "$out/gen/openzfs_start.c" -o "$out/start.o"
"$cc" "${flags[@]}" -c "$out/gen/openzfs_end.c" -o "$out/end.o"
"$cc" "${flags[@]}" -c aros/library/engine-library.c -o "$out/library.o"
"$cc" "${flags[@]}" -nostartfiles -nix "$out/start.o" "$out/library.o" \
    "$out/engine.o" "$nvpair" "$zlib" "$crypto" "$pthread" -lm "$out/end.o" -o "$out/openzfs.library"
"$cc" "${flags[@]}" aros/library/service-launcher.c -o "$out/OpenZFSServiceNative"
"$cc" "${flags[@]}" aros/library/lifetime-test.c -o "$out/OpenZFSLibraryTest"
launcher_symbols=$(nm "$out/OpenZFSServiceNative")
[[ $launcher_symbols != *' T zfsdev_ioctl_common'* && $launcher_symbols != *' T oz_engine_start'* ]]
sha256sum "$engine" "$nvpair" "$pthread" "$crypto" "$zlib" >"$out/inputs.sha256"
sha256sum "$out/openzfs.library" "$out/OpenZFSServiceNative" "$out/OpenZFSLibraryTest" >"$out/binaries.sha256"
cp "$normal/engine-object.sha256" "$normal/engine-metadata.h" "$normal/engine-metadata.sha256" "$out/"
printf 'service_test_modes=0\nfull_dispatcher=1\nshared_engine=1\n' >"$out/service-build-options.txt"
# Preserve engine diagnostic provenance, not the wrapper's environment.
if rg -q '^status_trace=' "$normal/service-build-options.txt";then
    rg '^status_trace=' "$normal/service-build-options.txt" >>"$out/service-build-options.txt"
fi
sha256sum "$out/OpenZFSServiceNative" >"$out/service-build.sha256"
sha256sum "$out/openzfs.library" >"$out/engine-library.sha256"
git rev-parse HEAD >"$out/source-head.txt"
git diff --binary >"$out/source.patch"
cp aros/library/* "$out/"
file "$out/openzfs.library" "$out/OpenZFSServiceNative"
