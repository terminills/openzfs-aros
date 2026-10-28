#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
: "${AROS_CC:?}" "${AROS_SDK:?}" "${AROS_PRIVATE_PTHREAD:?}" "${AROS_PTHREAD_INCLUDE:?}"
out=$(realpath -e "${1:?existing private output directory required}")
base=("$AROS_CC" "--sysroot=$AROS_SDK" "-B$AROS_SDK/lib/" -O2 -Wall -Wextra)
"${base[@]}" aros/ui/command-report-child.c -o "$out/OpenZFSReportChild"
"${base[@]}" -I"$AROS_PTHREAD_INCLUDE" \
    aros/ui/command-report-test.c aros/ui/api.c aros/ui/admission-client.c \
    aros/cli/process.c aros/cli/arguments.c aros/cli/output.c \
    "$AROS_PRIVATE_PTHREAD" -o "$out/OpenZFSCommandReportTest"
sha256sum aros/ui/command-report-test.c aros/ui/command-report-child.c \
    aros/ui/project-worker.c aros/ui/report-page.h aros/ui/report-copy.h aros/ui/report-export.h aros/ui/api.c aros/ui/api.h \
    aros/ui/admission-client.c aros/cli/process.c aros/cli/arguments.c aros/cli/output.c \
    "$AROS_PRIVATE_PTHREAD" "$out/OpenZFSReportChild" "$out/OpenZFSCommandReportTest" \
    >"$out/inputs.sha256"
