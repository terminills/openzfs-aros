#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source build-audit/dependencies.YF6P1Tca/dependencies.env
export AROS_ARTIFACT_ROOT="$PWD/build-audit"
inputs=$(mktemp -d "$AROS_ARTIFACT_ROOT/hosted-atime-inputs.XXXXXXXX")
engine="$PWD/build-audit/mmake-engine.cjQbkyg6/aros/engine/full"
shared="$PWD/build-audit/mmake-debugger.3zlIqhLI/stage"
cp "$0" "$inputs/run.sh"
cp build-audit/hosted-service-inputs.NwwJQWD5/service-build-options.txt "$inputs/"
sha256sum "$engine/full-engine.o" >"$inputs/engine-object.sha256"
cp "$shared/Libs/openzfs.library" "$shared/C/OpenZFSServiceNative" "$inputs/"
cp build-audit/dependency-client.V4Xt4xQS/OpenZFSPublicServiceTest "$inputs/"
sha256sum "$inputs/openzfs.library" "$inputs/OpenZFSServiceNative" "$inputs/OpenZFSPublicServiceTest" >"$inputs/binaries.sha256"
export AROS_TEST_SECURITY_LIBRARY=/mnt/short-term/openzfs-old-build-aros-20261027/hosted-security.MmebctgP/security.library
export AROS_TEST_HOSTED_RUNTIME=/mnt/short-term/openzfs-old-hosted-20261026/hosted
export AROS_TEST_HOSTED_SERVICE_BUILD="$inputs"
export AROS_TEST_SHARED_BUILD="$inputs"
export AROS_TEST_CLI_DIR="${AROS_TEST_CLI_DIR:-$PWD/build-audit/mmake-cli.ldOQ3FYG/stage/C}"
export AROS_TEST_HANDLER="$PWD/build-audit/handler-build.jxfoSGcL/OpenZFS-Handler"
export AROS_TEST_HOSTED_PERSISTENT=1 AROS_TEST_HOSTED_ASSIGN=1
export AROS_TEST_HOST_ZDB=/home/terminills/Documents/GitHub/openzfs-aros/build-control/zdb
export AROS_TEST_HOST_LIBRARY_PATH=/home/terminills/Documents/GitHub/openzfs-aros/build-control/.libs:/home/terminills/Documents/GitHub/openzfs-aros/build-control/deps/usr/lib/x86_64-linux-gnu
bash aros/engine/test-hosted-security.sh 2>&1 | tee "$inputs/run.log"
