#!/usr/bin/env bash
# Reject stale provenance and diagnostic engine objects before packaging.
set -euo pipefail
engine=${1:?engine object required}
normal=${2:?normal service build required}
nm_tool=${3:-nm}
[[ -n $nm_tool ]] || nm_tool=nm
grep -Fxq 'service_test_modes=0' "$normal/service-build-options.txt"
grep -Fxq 'full_dispatcher=1' "$normal/service-build-options.txt"
read -r expected _ <"$normal/engine-object.sha256"
actual=$(sha256sum "$engine")
[[ $expected =~ ^[0-9a-f]{64}$ && ${actual%% *} == "$expected" ]]
symbols=$("$nm_tool" "$engine")
[[ $symbols == *' T main'* && $symbols == *' T zfsdev_ioctl_common'* ]]
if [[ $symbols == *'__wrap_'* || $symbols == *'oz_test_admission_hold'* ||
      $symbols == *'oz_test_strict_flush_enabled'* ]];then
    echo 'Refusing a diagnostic or fault-injected engine object' >&2
    exit 2
fi
