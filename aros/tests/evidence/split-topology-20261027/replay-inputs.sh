#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source build-audit/dependencies.YF6P1Tca/dependencies.env
export AROS_TEST_ARTIFACT_ROOT=$PWD/build-audit
export AROS_TEST_NATIVE_FIXTURE=$PWD/build-audit/qemu-run.clear-cut-repaired.KTpzJcSo
export AROS_TEST_ISO=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/distfiles/aros-smp-pc-x86_64-20261022-alpha18c5.iso
export AROS_TEST_RUNTIME_PROFILE=dos-posix-ata
export AROS_TEST_DOS_OVERLAY=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/fat-replay.k72bijo1
export AROS_TEST_POSIX_LIBRARY=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/posix-source.krrt6d62/posixc.library
export AROS_TEST_ATA_OVERLAY=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/ata-capacity.6a3qpi9h
shared=${AROS_TEST_SHARED_BUILD:-$PWD/build-audit/shared-engine.EAysXbuw}
export AROS_TEST_SERVICE_BINARY=$shared/OpenZFSServiceNative
export AROS_TEST_ENGINE_LIBRARY=$shared/openzfs.library
export AROS_TEST_SERVICE_RELEASE=1
export AROS_TEST_HANDLER_BINARY=$PWD/build-audit/handler-build.jxfoSGcL/OpenZFS-Handler
export AROS_TEST_WHOLE_CLI_LINK=$PWD/build-audit/native-split-cli
export AROS_TEST_NATIVE_WRITABLE=1 AROS_TEST_SMP=8
export AROS_TEST_CLI_POOL_CREATE=1 AROS_TEST_NATIVE_SPLIT=1 AROS_TEST_TYPED_NATIVE_SPLIT=1
export AROS_TEST_TIMEOUT=900
bash aros/engine/test-qemu-native-import.sh
