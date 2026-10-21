#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cc=${AROS_CC:-/opt/aros-workspace/aros-adt-crosstools/x86_64-aros-gcc}
mkdir -p "$repo/ui-build"
src="$repo/aros/ui"
# No-engine UI protocol fixture, never a distributable native engine service.
"$cc" -DOZ_SERVICE_TEST_MODES -Wall -Wextra -O2 "$src/api.c" "$src/engine.c" "$src/broker.c" "$src/service.c" -o "$repo/ui-build/OpenZFSService"
"$cc" -Wall -Wextra -O2 "$src/api.c" "$src/admission-client.c" "$src/admission-worker.c" "$src/manager.c" "$src/project-worker.c" "$src/../cli/process.c" "$src/../cli/arguments.c" "$src/../cli/output.c" "$src/preferences.c" "$src/preferences-file.c" "$src/startup-settings.c" "$src/startup-settings-file.c" "$src/hostid-settings.c" "$src/../engine/hostid-record.c" -lpthread -lmuimaster -lintuition -lutility -liffparse -o "$repo/ui-build/OpenZFSManager"
"$cc" -Wall -Wextra -O2 "$src/api.c" "$src/apitest.c" -o "$repo/ui-build/OpenZFSAPITest"
