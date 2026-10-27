#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source build-audit/dependencies.YF6P1Tca/dependencies.env
export AROS_TEST_SHARED_BUILD="$PWD/build-audit/debugger-service.0umlrjD3"
export AROS_TEST_SERVICE_BINARY="$AROS_TEST_SHARED_BUILD/OpenZFSServiceNative"
export AROS_TEST_CLIENT_BINARY="$PWD/build-audit/dependency-client.dIXInP1o/OpenZFSPublicServiceTest"
export AROS_TEST_CLI_DIR="$PWD/build-audit/mmake-cli.uLkDlrcx/stage/C"
export AROS_TEST_HANDLER_BINARY="$PWD/build-audit/handler-build.jxfoSGcL/OpenZFS-Handler"
export AROS_TEST_DEVICE_BINARY="$AROS_TEST_CLI_DIR/OpenZFSDevice"
export AROS_TEST_COMPANION_TOOLS=1 AROS_TEST_REQUIRE_ERRNO=1
export AROS_TEST_REQUIRE_PARTITIONS=1 AROS_TEST_NATIVE_DISCOVERY=1
bash build-audit/run-accounting-replay.sh
