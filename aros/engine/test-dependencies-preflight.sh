#!/usr/bin/env bash
# Requires the same explicit source/tool environment as build-dependencies.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
source "$repo/aros/engine/storage-preflight.sh"
root=$(oz_artifact_root "$repo" "${AROS_ARTIFACT_ROOT:?external root required}")
run=$(mktemp -d "$root/dependency-preflight.XXXXXXXX")
export AROS_ARTIFACT_ROOT=$run
builder=$repo/aros/engine/build-dependencies.sh
bash "$builder" --check >"$run/valid.log" 2>&1
if AROS_BUILD_JOBS=33 bash "$builder" --check >"$run/jobs.log" 2>&1;then exit 1;fi
if AROS_PTHREAD_SOURCE="$run/missing" bash "$builder" --check >"$run/source.log" 2>&1;then exit 1;fi
if AROS_ARTIFACT_ROOT=/ bash "$builder" --check >"$run/system.log" 2>&1;then exit 1;fi
rg -q 'External artifact root must reside outside the system filesystem' "$run/system.log"
mkdir "$run/path with spaces"
if AROS_ARTIFACT_ROOT="$run/path with spaces" bash "$builder" --check >"$run/path.log" 2>&1;then exit 1;fi
rg -q 'make-special characters or whitespace' "$run/path.log"
printf 'deliberately invalid archive\n' >"$run/invalid.tar.bz2"
if AROS_TIRPC_ARCHIVE="$run/invalid.tar.bz2" bash "$builder" --check >"$run/archive.log" 2>&1;then exit 1;fi
rg -q 'FAILED' "$run/archive.log"
if find "$run" -type d -name 'dependencies.*' | rg -q .;then exit 1;fi
echo "PASS dependency preflight: valid=1 rejected=5 no-build-allocation=1 evidence=$run"
