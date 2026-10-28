#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/openzfs-report-tests.XXXXXXXX")
trap 'rm -f "$out/basic" "$out/fault" "$out/page" "$out/export" "$out/copy-fault"; rmdir "$out"' EXIT
"${CC:-cc}" -Wall -Wextra -Werror -O2 "$root/aros/ui/report-save-test.c" -o "$out/basic"
"${CC:-cc}" -Wall -Wextra -Werror -O2 "$root/aros/ui/report-save-fault-test.c" -o "$out/fault"
"$out/basic"
"$out/fault"
"${CC:-cc}" -Wall -Wextra -Werror -O2 "$root/aros/ui/report-page-test.c" -o "$out/page"
"$out/page"
"${CC:-cc}" -Wall -Wextra -Werror -O2 -pthread "$root/aros/ui/report-export-test.c" -o "$out/export"
"$out/export"
"${CC:-cc}" -Wall -Wextra -Werror -O2 "$root/aros/ui/report-copy-fault-test.c" -o "$out/copy-fault"
"$out/copy-fault"
