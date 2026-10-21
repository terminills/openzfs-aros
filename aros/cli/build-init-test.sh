#!/usr/bin/env bash
# Link the genuine upstream init fixture into the public-service client.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
probe=${1:?probe directory containing upstream-init-selected.o required}
client=${AROS_TEST_CLIENT_OBJECT_DIR:-$repo/build-aros/full-native-libzfs-init/client}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
objects=("$probe/upstream-init-selected.o"
    "$client/aros_cli_ioctl-public-test.c.o"
    "$client/lib_libzfs_core_libzfs_core.c.o"
    "$client/lib_libnvpair_libnvpair.c.o"
    "${AROS_NVPAIR_ARCHIVE:-$repo/build-aros/libnvpair-adt-consistent.a}"
    "${AROS_PRIVATE_PTHREAD:-$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a}"
    "${AROS_CRYPTO_ARCHIVE:-/opt/aros-workspace/chromium-aros/src/out/aros-chrome/obj/third_party/boringssl/libboringssl.a}")
sources=(aros/ui/api.c aros/ui/admission-client.c aros/cli/version.c aros/cli/stream-client.c aros/cli/stream-channel.c aros/engine/public-service-test.c)
crypto=${objects[${#objects[@]}-1]}
pthread_include=${AROS_PTHREAD_INCLUDE:-$repo/build-aros/pthread-consistent.s3r1CHla/source}
# Always compile the current control bridge; an old cached object can mask
# transport fixes even when all selected upstream client objects are fresh.
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -g -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
    -include "$repo/aros/engine/config.h" \
    -I"$pthread_include" \
    -I"$repo/aros/cli/include" -I"$repo/build-aros/include" -I"$repo/aros/engine/include" \
    -I"$repo/lib/libspl/include" -I"$repo/lib/libspl/include/os/linux" \
    -I"$repo/include" -I"$repo/include/os/linux/zfs" \
    -c aros/cli/control-client.c -o "$probe/current-control-client.o"
objects=("$probe/current-control-client.o" "${objects[@]}")
sha256sum aros/cli/control-client.c "$probe/current-control-client.o" > "$probe/control-client.sha256"
sha256sum "${objects[@]}" "${sources[@]}" > "$probe/init-client-inputs.sha256"
if file "$crypto" | rg -q 'thin archive'; then
    while IFS= read -r member; do sha256sum "$member"; done < <(ar t "$crypto") \
        > "$probe/init-crypto-members.sha256"
fi
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -DOZ_NATIVE_DISPATCHER -DOZ_TEST_UPSTREAM_INIT \
    -Wall -Wextra -O2 "${sources[@]}" "${objects[@]}" -o "$probe/OpenZFSPublicServiceTest"
sha256sum "$probe/OpenZFSPublicServiceTest" > "$probe/init-client.sha256"
