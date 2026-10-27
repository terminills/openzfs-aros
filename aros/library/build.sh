#!/usr/bin/env bash
# Private shared-engine candidate. Does not replace an installed service.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
cc=${AROS_CC:?explicit AROS compiler required}
sdk=${AROS_SDK:?explicit ABI-matched AROS SDK required}
gen=${AROS_GENMODULE:?explicit SDK-matched genmodule required}
engine=${AROS_LINKED_OBJECT:?full normal service engine object required}
normal=${AROS_SERVICE_BUILD_DIR:?normal full service build provenance required}
: "${AROS_PTHREAD_INCLUDE:?matching pthread headers required}"
: "${AROS_CRYPTO_INCLUDE:?matching crypto headers required}"
: "${AROS_TIRPC_SOURCE:?matching tirpc source required}"
nm_tool=${AROS_NM:-$("$cc" -print-prog-name=nm)}
objcopy_tool=$("$cc" -print-prog-name=objcopy)
bash aros/library/verify-engine.sh "$engine" "$normal" "$nm_tool"
nvpair=${AROS_NVPAIR_ARCHIVE:?nvpair archive required}
pthread=${AROS_PRIVATE_PTHREAD:?matching pthread archive required}
crypto=${AROS_CRYPTO_ARCHIVE:?explicit target crypto archive required}
zlib=${AROS_ZLIB_IMPORT:?explicit target zlib import archive required}
sdk=$(realpath -e "$sdk")
gen=$(realpath -e "$gen")
[[ -x $gen && -f $sdk/include/exec/types.h && -d $sdk/lib ]]
engine=$(realpath -e "$engine")
nvpair=$(realpath -e "$nvpair")
pthread=$(realpath -e "$pthread")
crypto=$(realpath -e "$crypto")
zlib=$(realpath -e "$zlib")
source "$repo/aros/engine/storage-preflight.sh"
mkdir -p "$repo/build-aros"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
out=$(mktemp -d "$artifact_root/shared-engine.XXXXXXXX")
echo "Shared engine candidate: $out"
mkdir -p "$out/gen" "$out/include"/{proto,clib,inline,defines,pragmas}
"$cc" --version >"$out/compiler.txt"
printf 'compiler=%s\nsdk=%s\ngenmodule=%s\nnm=%s\nobjcopy=%s\n' \
    "$cc" "$sdk" "$gen" "$nm_tool" "$objcopy_tool" >"$out/toolchain.txt"
sha256sum "$engine" "$nvpair" "$pthread" "$crypto" "$zlib" "$gen" >"$out/inputs.sha256"
for operation in writefiles writelibdefs;do
    "$gen" -c aros/library/openzfs.conf -d "$out/gen" "$operation" openzfs library
done
"$gen" -c aros/library/openzfs.conf -d "$out/include" writeincludes openzfs library
"$objcopy_tool" --redefine-sym main=oz_service_main "$engine" "$out/engine.o"
flags=(--sysroot="$sdk" -B"$sdk/lib/" -O2 -g -Wall -Wextra -I"$out/include" -I"$out/gen")
bash aros/library/build-zdb-object.sh "$out/debugger.o" >"$out/debugger-build.log" 2>&1
sha256sum "$out/debugger.o" >>"$out/inputs.sha256"
"$cc" "${flags[@]}" -c "$out/gen/openzfs_start.c" -o "$out/start.o"
"$cc" "${flags[@]}" -c "$out/gen/openzfs_end.c" -o "$out/end.o"
"$cc" "${flags[@]}" -c aros/library/engine-library.c -o "$out/library.o"
"$cc" "${flags[@]}" -nostartfiles -nix "$out/start.o" "$out/library.o" \
    "$out/engine.o" "$out/debugger.o" "$nvpair" "$zlib" "$crypto" "$pthread" -lm "$out/end.o" -o "$out/openzfs.library"
"$cc" "${flags[@]}" aros/library/service-launcher.c -o "$out/OpenZFSServiceNative"
"$cc" "${flags[@]}" aros/library/lifetime-test.c -o "$out/OpenZFSLibraryTest"
"$cc" "${flags[@]}" aros/library/zdb-launcher.c -o "$out/zdb"
debugger_symbols=$("$nm_tool" "$out/zdb")
[[ $debugger_symbols != *' T zfsdev_ioctl_common'* && $debugger_symbols != *' T oz_zdb_main'* ]]
launcher_symbols=$("$nm_tool" "$out/OpenZFSServiceNative")
[[ $launcher_symbols != *' T zfsdev_ioctl_common'* && $launcher_symbols != *' T oz_engine_start'* ]]
sha256sum -c "$out/inputs.sha256" >"$out/input-stability.log"
sha256sum "$out/openzfs.library" "$out/OpenZFSServiceNative" "$out/OpenZFSLibraryTest" "$out/zdb" >"$out/binaries.sha256"
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
