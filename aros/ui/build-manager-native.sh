#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
: "${AROS_SDK:?set AROS_SDK to the SDK matching the target runtime (hosted and PC/SMP are not interchangeable)}"
sdk=$AROS_SDK
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
pthread=${AROS_PRIVATE_PTHREAD:-$repo/build-aros/pthread-consistent.s3r1CHla/libpthread.a}
pthread_include=${AROS_PTHREAD_INCLUDE:-$repo/build-aros/pthread-consistent.s3r1CHla/source}
output=${1:-build-aros/OpenZFSManagerNative}
cmp "$(dirname "$pthread")/source/pthread.h" "$pthread_include/pthread.h"
mkdir -p "$(dirname "$output")"
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
    "$output" >"$output.inputs.sha256"
