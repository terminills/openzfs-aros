#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "$0")/storage-preflight.sh"
# Stub filesystem observations, never allocate a large fixture for this test.
df() { printf 'Filesystem 1024-blocks Used Available Capacity Mounted\nmock 200000000 0 %s 0%% /\n' "$free_kib"; }
free_kib=104857599
if oz_storage_preflight /; then exit 1; fi
free_kib=104857600
oz_storage_preflight /
free_kib=200000000
oz_storage_preflight /
free_kib=invalid
if oz_storage_preflight /; then exit 1; fi
echo 'PASS storage preflight: below-reserve denied; boundary and above allowed; malformed denied'

# Exercise the selection guard too: a missing external mount must never turn
# into a successful allocation on the system filesystem. Only empty temporary
# directories are real; filesystem identity and capacity are controlled here.
fixture=$(mktemp -d)
trap 'rmdir -- "$fixture/external" "$fixture/default" "$fixture"' EXIT
mkdir "$fixture/default" "$fixture/external"
same_device=0
stat() {
    if [[ $same_device == 1 || ${@: -1} == "$fixture/default" ]]; then
        printf '1\n'
    else
        printf '2\n'
    fi
}
df() {
    local capacity=$free_kib
    if [[ ${@: -1} == "$fixture/external" ]]; then capacity=$external_free; fi
    printf 'Filesystem 1024-blocks Used Available Capacity Mounted\nmock 200000000 0 %s 0%% /\n' "$capacity"
}
free_kib=200000000
external_free=200000000
[[ $(oz_artifact_root "$fixture/default") == "$fixture/default" ]]
[[ $(oz_artifact_root "$fixture/default" "$fixture/external") == "$fixture/external" ]]
if oz_artifact_root "$fixture/default" "$fixture/missing"; then exit 1; fi
same_device=1
if oz_artifact_root "$fixture/default" "$fixture/external"; then exit 1; fi
same_device=0
external_free=104857599
if oz_artifact_root "$fixture/default" "$fixture/external"; then exit 1; fi
external_free=200000000
free_kib=104857599
if oz_artifact_root "$fixture/default" "$fixture/external"; then exit 1; fi
echo 'PASS artifact root: default and separate filesystem allowed; missing, same filesystem and either low reserve denied'
