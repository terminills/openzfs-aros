#!/usr/bin/env bash
# Compile real client support for the diagnostic linker; never run storage I/O.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
: "${AROS_CC:?explicit cross compiler required}"
: "${AROS_SDK:?explicit matching SDK required}"
: "${AROS_PTHREAD_INCLUDE:?matching pthread headers required}"
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
out=$(mktemp -d "$root/zdb-support.XXXXXXXX")
echo "Evidence: $out"
flags=(--sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -O2 -g
    -ffunction-sections -fdata-sections -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
    -include "$repo/aros/engine/config.h" -I"$AROS_PTHREAD_INCLUDE"
    -I"$repo/aros/cli/include" -I"$repo/aros/engine/include"
    -I"$repo/lib/libspl/include" -I"$repo/lib/libspl/include/os/linux"
    -I"$repo/include" -I"$repo/include/os/linux/zfs")
git rev-parse HEAD >"$out/source-head.txt"
git diff --binary >"$out/source.patch"
cp "$0" "$out/"
objects=()
for source in lib/libnvpair/libnvpair.c lib/libzfs_core/libzfs_core.c \
    aros/cli/control-client.c aros/cli/stream-client.c \
    aros/cli/native-device.c \
    aros/ui/admission-client.c;do
    name=${source//\//_}
    sha256sum "$source" >>"$out/sources.sha256"
    command=("$AROS_CC" "${flags[@]}" -MD -MF "$out/$name.d"
        -c "$source" -o "$out/$name.o")
    printf '%q ' "${command[@]}" >>"$out/commands.sh"
    printf '\n' >>"$out/commands.sh"
    "${command[@]}" >"$out/$name.log" 2>&1
    objects+=("$out/$name.o")
done
# Prune unrelated client entry points only in this relocatable intermediate,
# never the final AROS module or executable startup/autoinit sections.
# oz_device_path_parse and oz_stream_dispatch are supplied by the full shared
# engine; do not duplicate device-path or stream-channel in this bundle.
command=("$AROS_CC" -print-prog-name=ld)
ld_tool=$("${command[@]}")
command=("$ld_tool" -r --gc-sections -u dump_nvlist -u libzfs_core_init
    -u libzfs_core_fini -u lzc_get_props -u zfs_dev_is_whole_disk
    "${objects[@]}" -o "$out/support.o")
printf '%q ' "${command[@]}" >>"$out/commands.sh"
printf '\n' >>"$out/commands.sh"
"${command[@]}" >"$out/support-link.log" 2>&1
sha256sum "${objects[@]}" "$out/support.o" >"$out/objects.sha256"
echo 'PASS support compilation and relocatable link (not a native executable test)'
