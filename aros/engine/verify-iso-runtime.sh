#!/usr/bin/env bash
# Extract actual guest libraries and bind them to the explicitly selected inputs.
# OUTPUT must be a new directory. Evidence is OUTPUT.sha256 and OUTPUT-verify.log.
set -euo pipefail
iso=${1:?ISO required}
output=${2:?new output directory required}
shift 2
(( $# > 0 )) || { echo 'At least one selected runtime required' >&2; exit 2; }
[[ ! -e $output && ! -e $output.sha256 && ! -e $output-verify.log ]]
declare -A names=()
for selected in "$@";do
    name=${selected##*/}
    [[ $name == *.library && -f $selected && ! ${names[$name]+present} ]]
    names[$name]=1
done
mkdir "$output"
for selected in "$@";do
    name=${selected##*/}
    xorriso -osirrox on -indev "$iso" \
        -extract "/Libs/$name" "$output/$name" >>"$output-verify.log" 2>&1
    cmp "$selected" "$output/$name"
    sha256sum "$output/$name" >>"$output.sha256"
done
