#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-official-sdk-audit.xi9RkF3r/publication
sdk=/run/media/terminills/Codex/show-consolidation-01a16962/sdk-prerequisite-audit/nightly-20261023/iso-Developer
cc=/home/terminills/Documents/GitHub/aros-adt-crosstools/x86_64-aros-gcc
out=/mnt/short-term/openzfs-ui-worker.khId8Lyi
set -x
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -O2 -Wall -Wextra -Werror \
    aros/ui/runtime-worker-test.c aros/ui/api.c aros/ui/admission-client.c \
    aros/ui/project-worker.c aros/cli/process.c aros/cli/arguments.c aros/cli/output.c \
    -lpthread -Wl,-Map,"$out/worker.map" -o "$out/RuntimeWorkerTest"
sha256sum aros/ui/runtime-worker-test.c aros/ui/api.c aros/ui/admission-client.c \
    aros/ui/project-worker.c aros/cli/process.c aros/cli/arguments.c aros/cli/output.c \
    "$sdk/lib/libpthread.a" "$sdk/include/pthread.h" "$out/RuntimeWorkerTest" > "$out/worker-inputs.sha256"
