#!/usr/bin/env bash
# Exercise launcher admission only. Never supplies a service or boots a guest.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
source "$repo/aros/engine/storage-preflight.sh"
root=$(oz_artifact_root "$repo" "${1:?external evidence directory required}")
system_root=${2:?writable directory on system filesystem required}
[[ -d $system_root && -w $system_root && $(stat -c %d "$system_root") == $(stat -c %d /) ]]
run=$(mktemp -d "$root/native-preflight.XXXXXXXX")
mkdir "$run/qemu-run.fixture" "$run/unrelated-fixture"
invoke()
{
    env -u OZ_NATIVE_RUNNER_REPO -u OZ_NATIVE_RUNNER_FROZEN \
        -u AROS_TEST_SERVICE_BINARY -u AROS_TEST_NATIVE_BUNDLE \
        AROS_TEST_ARTIFACT_ROOT="$1" AROS_TEST_NATIVE_FIXTURE="$2" \
        bash "$repo/aros/engine/test-qemu-native-import.sh"
}
set +e
invoke "$run" "$run/qemu-run.fixture" >"$run/accepted-path.log" 2>&1
accepted=$?
invoke "$run" "$run/unrelated-fixture" >"$run/rejected-path.log" 2>&1
rejected=$?
invoke "$system_root" "$run/qemu-run.fixture" >"$run/system-root.log" 2>&1
system=$?
set -e
[[ $accepted != 0 ]]
rg -q 'AROS_TEST_SERVICE_BINARY: service required' "$run/accepted-path.log"
[[ $rejected == 2 ]]
[[ $system != 0 ]]
rg -q 'External artifact root must reside outside the system filesystem' "$run/system-root.log"
count=0
for frozen in "$run"/native-runner.*/test-qemu-native-import.sh;do
    cmp "$repo/aros/engine/test-qemu-native-import.sh" "$frozen"
    ((count+=1))
done
[[ $count == 2 ]]
if find "$run" -name '*.iso' -o -name '*.qcow2' | rg -q .;then
    echo 'Unexpected guest image allocation' >&2;exit 1
fi
printf 'PASS external snapshot=2 source-preserved=1 fixture-admission=1 unrelated-denied=1 system-denied=1 no-images=1\n'
printf 'Evidence: %s\n' "$run"
