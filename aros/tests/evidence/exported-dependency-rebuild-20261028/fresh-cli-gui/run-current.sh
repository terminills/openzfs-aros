#!/usr/bin/env bash
set -euo pipefail
work=/run/media/terminills/Amiwest/openzfs-source-replay-20261028
export AROS_TEST_SOURCE_ROOT="$work/source"
export AROS_TEST_SHARED_BUILD="$work/build/shared-engine.ld5CZgji"
export AROS_TEST_CLIENT_BINARY="$work/build/source-engine.3f4plKUL/service/OpenZFSPublicServiceTest"
export AROS_TEST_MANAGER_BINARY="$work/build/fresh-mmake-cli.yqLYANrY/stage/Tools/OpenZFSManagerNative"
export AROS_TEST_CLI_DIR="$work/build/fresh-mmake-cli.yqLYANrY/stage/C"
export AROS_TEST_RUNNER=/mnt/short-term/openzfs-bootenv-candidate-20261025/aros/engine/test-qemu-public-service.sh
exec bash "$work/run-native.sh"
