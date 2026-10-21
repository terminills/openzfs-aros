#!/usr/bin/env bash
# Rebuild one object into a private, ABI-matched DOS library. Never install it.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
sdk=${AROS_SDK:?matching SDK required}
source=${AROS_DOS_SOURCE:?DOS source directory required}
generated=${AROS_DOS_GENERATED:?matching generated DOS directory required}
input_map=${AROS_DOS_INPUT_MAP:-$generated/.map}
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
source "$repo/aros/engine/storage-preflight.sh"
artifact_root=$(oz_artifact_root "$repo/build-aros" "${AROS_ARTIFACT_ROOT:-}")
run=$(mktemp -d "$artifact_root/loader-overlay.XXXXXXXX")
echo "Evidence: $run"
cp "$source/loadseg.c" "$run/loadseg.c"
sha256sum "$source/loadseg.c" "$source/dos_intern.h" "$input_map" \
    "$repo/aros/patches/dos-loader-execute.patch" >"$run/inputs.sha256"
patch --batch --fuzz=0 -d "$run" -p1 <"$repo/aros/patches/dos-loader-execute.patch"
common=("$cc" --sysroot="$sdk" -B"$sdk/lib/" -m64 -mcmodel=large -mno-red-zone -mno-ms-bitfields)
probe=()
if [[ ${AROS_LOADER_PROBE:-0} == 1 ]];then probe=(-DAROS_LOADER_PROBE);fi
printf 'loader_probe=%s\n' "${AROS_LOADER_PROBE:-0}" >"$run/build-options.txt"
"${common[@]}" "${probe[@]}" -O2 -D__NOLIBBASE__ -I"$source" -I"$generated/include" \
    -include "$generated/include/dos_deflibdefs.h" -c "$run/loadseg.c" -o "$run/loadseg.o"
runcommand_probe=${AROS_RUNCOMMAND_PROBE:-0}
runcommand_verbose=${AROS_RUNCOMMAND_VERBOSE:-1}
[[ $runcommand_probe == 0 || $runcommand_probe == 1 ]]
[[ $runcommand_verbose == 0 || $runcommand_verbose == 1 ]]
printf 'runcommand_probe=%s\n' "$runcommand_probe" >>"$run/build-options.txt"
printf 'runcommand_verbose=%s\n' "$runcommand_verbose" >>"$run/build-options.txt"
if [[ $runcommand_probe == 1 ]];then
    cp "$source/runcommand.c" "$run/runcommand.c"
    sha256sum "$source/runcommand.c" "$repo/aros/patches/dos-runcommand-probe.patch" >>"$run/inputs.sha256"
    patch --batch --fuzz=0 -d "$run" -p1 <"$repo/aros/patches/dos-runcommand-probe.patch"
    "${common[@]}" -O2 -D__NOLIBBASE__ -DOZ_RUNCOMMAND_VERBOSE="$runcommand_verbose" -I"$source" -I"$generated/include" \
        -include "$generated/include/dos_deflibdefs.h" -c "$run/runcommand.c" -o "$run/runcommand.o"
fi
ioerr_probe=${AROS_IOERR_PROBE:-0}
[[ $ioerr_probe == 0 || $ioerr_probe == 1 ]]
printf 'ioerr_probe=%s\n' "$ioerr_probe" >>"$run/build-options.txt"
if [[ $ioerr_probe == 1 ]];then
    cp "$source/setioerr.c" "$run/setioerr.c"
    sha256sum "$source/setioerr.c" "$repo/aros/patches/dos-ioerr-probe.patch" >>"$run/inputs.sha256"
    patch --batch --fuzz=0 -d "$run" -p1 <"$repo/aros/patches/dos-ioerr-probe.patch"
    "${common[@]}" -O2 -D__NOLIBBASE__ -I"$source" -I"$generated/include" \
        -include "$generated/include/dos_deflibdefs.h" -c "$run/setioerr.c" -o "$run/setioerr.o"
fi
objects=(); archives=(); replaced=0; runcommand_replaced=0; ioerr_replaced=0
while read -r tag path rest;do
    [[ $tag == LOAD ]] || continue
    case "$path" in
        *.o)
            test -f "$path"
            sha256sum "$path" >>"$run/inputs.sha256"
            if [[ ${path##*/} == loadseg.o ]];then
                objects+=("$run/loadseg.o");replaced=$((replaced+1))
            elif [[ $runcommand_probe == 1 && ${path##*/} == runcommand.o ]];then
                objects+=("$run/runcommand.o");runcommand_replaced=$((runcommand_replaced+1))
            elif [[ $ioerr_probe == 1 && ${path##*/} == setioerr.o ]];then
                objects+=("$run/setioerr.o");ioerr_replaced=$((ioerr_replaced+1))
            else objects+=("$path");fi ;;
        *.a) test -f "$path";archives+=("$path");sha256sum "$path" >>"$run/inputs.sha256" ;;
    esac
done <"$input_map"
[[ $replaced == 1 && ${#objects[@]} -gt 100 ]]
[[ $runcommand_replaced == "$runcommand_probe" ]]
[[ $ioerr_replaced == "$ioerr_probe" ]]
"${common[@]}" -nostartfiles -nostdlib -Wl,-Map,"$run/dos.map" \
    "${objects[@]}" -Wl,--start-group "${archives[@]}" -Wl,--end-group -o "$run/dos.library"
sha256sum "$run/dos.library" >"$run/output.sha256"
if [[ -n ${AROS_DOS_BASE_OVERLAY:-} ]];then
    base=$(realpath -e "$AROS_DOS_BASE_OVERLAY")
    test -f "$base/package/dos.library"
    cp -a "$base/package" "$run/package"
    cp "$run/dos.library" "$run/package/dos.library"
    # Check every other original module remains byte-identical.
    for module in "$base/package/"*;do
        [[ ${module##*/} == dos.library ]] && continue
        cmp "$module" "$run/package/${module##*/}"
        sha256sum "$module" >>"$run/inputs.sha256"
    done
    pkg=${AROS_PACKAGE_TOOL:?AROS package tool required}
    # Package order is boot order, not a directory traversal detail.
    python3 - "$pkg" "$base/aros-base.pkg.xz" "$run" <<'PY'
import io, lzma, pathlib, runpy, subprocess, sys
tool, original, output = sys.argv[1:]
api = runpy.run_path(tool)
stream = io.BytesIO(lzma.decompress(pathlib.Path(original).read_bytes()))
assert api['readHeader'](stream) == 1
names = []
while stream.tell() < len(stream.getbuffer()):
    raw, data = api['readFile'](stream)
    name = raw.rstrip(b'\0').decode('iso-8859-1')
    assert name and pathlib.Path(name).name == name and name not in names
    candidate = pathlib.Path(output, 'package', name).read_bytes()
    assert name == 'dos.library' or candidate == data
    names.append(name)
assert names.count('dos.library') == 1
subprocess.run([sys.executable, tool, 'c', str(pathlib.Path(output, 'aros-base.pkg')),
                str(pathlib.Path(output, 'package')), *names], check=True)
PY
    xz --check=crc32 --x86 -9e -k "$run/aros-base.pkg"
    sha256sum "$run/aros-base.pkg.xz" >>"$run/output.sha256"
fi
echo "PASS private loader overlay: $run/dos.library"
