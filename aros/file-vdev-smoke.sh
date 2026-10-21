#!/usr/bin/env bash
# Linux userspace control only. Upstream ztest creates its own regular files.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
build="$repo/build-control"
testbin="$build/ztest"
[[ -x "$testbin" ]] || { echo "Build ztest first: $testbin" >&2; exit 1; }
[[ $(uname -s) == Linux ]] || { echo 'This runner is the Linux control.' >&2; exit 1; }
mkdir -p "$build/evidence"
run=$(mktemp -d "$build/evidence/file-vdev.XXXXXXXX")
echo "Evidence: $run"
git -C "$repo" rev-parse HEAD > "$run/source.txt"
git -C "$repo" diff --binary > "$run/source.patch"
for topology in mirror raidz1 raidz2 raidz3; do
    mkdir "$run/$topology"
    case "$topology" in
        mirror) geometry=(-m 2 -r 1 -R 1) ;;
        raidz1) geometry=(-m 0 -r 3 -R 1) ;;
        raidz2) geometry=(-m 0 -r 4 -R 2) ;;
        raidz3) geometry=(-m 0 -r 5 -R 3) ;;
    esac
    args=(-f "$run/$topology" -p "aros_$topology" -K raidz -v 1
          -s 256M -a 12 -d 2 -t 2 -T 15 -P 5 -k 0 -C special=off -V "${geometry[@]}")
    printf '%q ' "$testbin" "${args[@]}" > "$run/$topology/command.txt"
    printf '\n' >> "$run/$topology/command.txt"
    if "$testbin" "${args[@]}" > "$run/$topology/output.log" 2>&1; then
        echo "$topology PASS" | tee -a "$run/results.txt"
    else
        rc=$?
        echo "$topology FAIL exit=$rc" | tee -a "$run/results.txt"
        exit "$rc"
    fi
done
