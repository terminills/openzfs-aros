#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo/hosted-ui"
[[ -x boot/linux/AROSBootstrap ]] || { echo 'Isolated hosted runtime is not staged.' >&2; exit 1; }
exec ./boot/linux/AROSBootstrap -c "$repo/aros/ui/hosted.conf"
