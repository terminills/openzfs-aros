#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
: "${AROS_SDK:?set AROS_SDK to the SDK matching the target runtime (hosted and PC/SMP are not interchangeable)}"
sdk=$AROS_SDK
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
pthread_backend=${AROS_PTHREAD_BACKEND:-private}
case "$pthread_backend" in
    sdk)
        if [[ -n ${AROS_PRIVATE_PTHREAD:-}${AROS_PTHREAD_INCLUDE:-} ]]; then
            echo 'SDK pthread mode cannot be combined with private pthread inputs' >&2
            exit 1
        fi
        pthread="$sdk/lib/libpthread.a"
        pthread_include="$sdk/include"
        test -f "$pthread"
        test -f "$pthread_include/pthread.h"
        ;;
    private)
        pthread=${AROS_PRIVATE_PTHREAD:-$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a}
        pthread_include=${AROS_PTHREAD_INCLUDE:-$repo/build-aros/pthread-consistent.s3r1CHla/source}
        cmp "$(dirname "$pthread")/source/pthread.h" "$pthread_include/pthread.h"
        ;;
    *) echo "Unsupported pthread backend: $pthread_backend" >&2; exit 1 ;;
esac
output=${1:-build-aros/OpenZFSManagerNative}
mkdir -p "$(dirname "$output")"
"$cc" --version >"$output.compiler.txt"
printf 'sdk=%s\npthread=%s\npthread_include=%s\npthread_backend=%s\n' "$sdk" "$pthread" "$pthread_include" "$pthread_backend" >"$output.target.txt"
# Preserve the actual compile-time layout and startup archive identity. AROS
# SMP SDK generations can differ despite sharing the x86_64 target triple.
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -I"$pthread_include" -S \
    aros/ui/target-abi.c -o "$output.target-abi.s"
sha256sum "$sdk/include/exec/tasks.h" "$sdk/include/exec/ports.h" \
    "$sdk/include/dos/dosextens.h" "$sdk/lib/libautoinit.a" \
    >"$output.sdk.sha256"
"$cc" --sysroot="$sdk" -B"$sdk/lib/" -I"$pthread_include" -O2 -g -Wall -Wextra \
    aros/ui/api.c aros/ui/admission-client.c aros/ui/admission-worker.c \
    aros/ui/manager.c aros/ui/project-worker.c aros/cli/process.c aros/cli/arguments.c aros/cli/output.c aros/ui/preferences.c aros/ui/preferences-file.c \
    aros/ui/startup-settings.c aros/ui/startup-settings-file.c aros/ui/hostid-settings.c aros/engine/hostid-record.c \
    "$pthread" -lmuimaster -lintuition -lutility -liffparse -o "$output"
sha256sum "$pthread" "$pthread_include/pthread.h" aros/ui/api.c aros/ui/api.h \
    aros/ui/admission-client.c aros/ui/admission-worker.c aros/ui/manager.c aros/ui/project-worker.c aros/ui/project-worker.h aros/cli/process.c aros/cli/arguments.c aros/cli/output.c aros/ui/operations.def aros/ui/pool_editor.h aros/ui/iostat-rates.h aros/ui/iostat-refresh.h \
    aros/ui/partition-api.h aros/ui/gpt-api.h aros/ui/admission-worker.h aros/ui/partition-view.h aros/platform/partitions.h aros/device/mapping.h aros/device/mountfile.h \
    aros/ui/preferences.c aros/ui/preferences-file.c aros/ui/preferences.h aros/ui/pool-wizard.h aros/ui/encrypted-create.h \
    aros/ui/startup-settings.c aros/ui/startup-settings-file.c aros/ui/startup-settings.h aros/ui/startup-editor.h aros/ui/settings-file.h \
    aros/ui/hostid-settings.c aros/ui/hostid-settings.h aros/engine/hostid-record.c aros/engine/hostid-record.h \
    aros/ui/simple-pools.h aros/ui/simple-health.h aros/ui/target-abi.c "$output.target-abi.s" "$output.sdk.sha256" \
    "$output" >"$output.inputs.sha256"
sha256sum -c "$output.sdk.sha256" >"$output.sdk-after-check.log"
