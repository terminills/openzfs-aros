#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
export AROS_TEST_ARTIFACT_ROOT=$PWD/build-audit
export AROS_TEST_ISO=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/distfiles/aros-smp-pc-x86_64-20261022-alpha18c5.iso
export AROS_TEST_BASE_PLATFORM=1
export AROS_TEST_BASE_DOS_PACKAGE=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/fat-replay.k72bijo1/aros-base.pkg.xz
export AROS_PACKAGE_TOOL=/home/terminills/Documents/GitHub/AROS-main-adt-compare/tools/package/pkg
export AROS_TEST_POSIX_LIBRARY=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/posix-source.krrt6d62/posixc.library
export AROS_TEST_SERVICE_BINARY=$PWD/build-audit/source-engine.sa4fWOFY/service/OpenZFSServiceNative
export AROS_TEST_CLIENT_BINARY=$PWD/build-audit/private-client/OpenZFSPublicServiceTest
export AROS_TEST_CPUS=8
export AROS_TEST_REQUIRE_BOOTENV=1
export AROS_TEST_REQUIRE_ACCOUNTING_UPGRADE=1
export AROS_TEST_CAPTURE_FAILURE=1
sha256sum "$AROS_TEST_ISO" "$AROS_TEST_BASE_DOS_PACKAGE" "$AROS_TEST_POSIX_LIBRARY" "$AROS_TEST_SERVICE_BINARY" "$AROS_TEST_CLIENT_BINARY" > build-audit/private-replay-inputs.sha256
bash aros/engine/test-qemu-public-service.sh
