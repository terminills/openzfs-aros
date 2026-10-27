#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
export AROS_TEST_SHARED_BUILD=${AROS_TEST_SHARED_BUILD:-$PWD/build-audit/shared-engine.7lsB2LKp}
source <(sed '$ d' build-audit/run-native-manager-split.sh)
unset AROS_TEST_WHOLE_CLI_LINK AROS_TEST_NATIVE_SPLIT AROS_TEST_TYPED_NATIVE_SPLIT AROS_TEST_CLI_POOL_CREATE
export AROS_TEST_NATIVE_WRITABLE=0
export AROS_TEST_CLI_DIR=$PWD/build-audit/mmake-cli.2DYgVEWO/stage/C
export AROS_TEST_SPLIT_TOPOLOGY=${AROS_TEST_SPLIT_TOPOLOGY:-hole}
bash aros/engine/test-qemu-native-import.sh
