#!/usr/bin/env bash
# Dependency audit only: this is NOT the production shared-library zdb build.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
: "${AROS_CC:?explicit cross compiler required}"
: "${AROS_SDK:?explicit matching SDK required}"
: "${AROS_NVPAIR_ARCHIVE:?matching nvpair archive required}"
: "${AROS_CRYPTO_ARCHIVE:?matching crypto archive required}"
: "${AROS_PRIVATE_PTHREAD:?matching pthread archive required}"
probe=$(realpath -e "${1:?zdb compile probe required}")
engine=$(realpath -e "${2:?renamed engine object required}")
source aros/engine/storage-preflight.sh
root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
out=$(mktemp -d "$root/zdb-link-probe.XXXXXXXX")
echo "Evidence: $out"
objects=("$probe/cmd_zdb_zdb.c.o" "$probe/cmd_zdb_zdb_il.c.o"
    "$probe/lib_libzdb_libzdb.c.o" "$engine")
if [[ -n ${3:-} ]];then
    objects+=("$(realpath -e "$3")")
fi
archives=("$AROS_NVPAIR_ARCHIVE" "$AROS_CRYPTO_ARCHIVE"
    "$AROS_PRIVATE_PTHREAD" "$AROS_SDK/lib/libz.static.a")
sha256sum "${objects[@]}" "${archives[@]}" >"$out/inputs.sha256"
git rev-parse HEAD >"$out/source-head.txt"
git diff --binary >"$out/source.patch"
cp "$0" "$out/"
"$AROS_CC" --version >"$out/compiler.txt"
# AROS executable startup must retain its autoinit sections. Do not apply
# --gc-sections to the final executable (it also lacks an ELF entry symbol).
command=("$AROS_CC" --sysroot="$AROS_SDK" -B"$AROS_SDK/lib/" -nix
    "${objects[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group
    -lm -o "$out/zdb-link-audit")
printf '%q ' "${command[@]}" >"$out/command.sh"
printf '\n' >>"$out/command.sh"
status=0
"${command[@]}" >"$out/link.log" 2>&1 || status=$?
sha256sum -c "$out/inputs.sha256" >"$out/input-stability.log"
printf 'link_exit=%s\nproduction_build=0\nruntime_verified=0\n' "$status" >"$out/result.txt"
cat "$out/link.log"
echo 'Dependency audit only: shared-engine integration, admission and native execution remain unverified.'
exit "$status"
