#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source <(sed '$ d' build-audit/build-accounting-engine.sh)
export AROS_TEST_ARTIFACT_ROOT=$PWD/build-audit
export AROS_TEST_NATIVE_FIXTURE=${AROS_TEST_NATIVE_FIXTURE:-$PWD/build-audit/qemu-run.rewind-corrupt-20261026}
export AROS_TEST_ISO=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/distfiles/aros-smp-pc-x86_64-20261022-alpha18c5.iso
export AROS_TEST_RUNTIME_PROFILE=dos-posix
export AROS_TEST_DOS_OVERLAY=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/fat-replay.k72bijo1
export AROS_TEST_POSIX_LIBRARY=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/posix-source.krrt6d62/posixc.library
export AROS_TEST_SERVICE_RELEASE=1
export AROS_TEST_SERVICE_BINARY=$PWD/build-audit/shared-engine.IiSPGMPM/OpenZFSServiceNative
export AROS_TEST_ENGINE_LIBRARY=$PWD/build-audit/shared-engine.IiSPGMPM/openzfs.library
export AROS_TEST_HANDLER_BINARY=$PWD/build-audit/handler-build.jxfoSGcL/OpenZFS-Handler
export AROS_TEST_CLI_DIR=$PWD/build-audit/mmake-cli.EksGNipO/stage/C
export AROS_TEST_IMPORT_REWIND=1 AROS_TEST_SMP=8
bash aros/engine/test-qemu-native-import.sh
