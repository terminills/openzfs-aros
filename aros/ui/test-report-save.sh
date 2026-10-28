#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/openzfs-report-tests.XXXXXXXX")
trap 'rm -f "$out/basic" "$out/fault"; rmdir "$out"' EXIT
"${CC:-cc}" -Wall -Wextra -Werror -O2 "$root/aros/ui/report-save-test.c" -o "$out/basic"
"${CC:-cc}" -Wall -Wextra -Werror -O2 "$root/aros/ui/report-save-fault-test.c" -o "$out/fault"
"$out/basic"
"$out/fault"
