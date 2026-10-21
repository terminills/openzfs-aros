#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
sdk=${AROS_SDK:-/opt/aros-workspace/aros-amiwest-nightly-release/bin/pc-x86_64-smp/AROS/Developer}
output=${1:-$repo/build-aros/ExecHelper}
mkdir -p "$(dirname -- "$output")"
# Required: runtime Unix-path handling for execvp PATH components, not shell
# interpretation. Native colon-qualified executable paths remain supported.
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -nix -O2 -Wall -Wextra -Werror \
    "$repo/aros/cli/exec-helper.c" -o "$output"
sha256sum "$output"
