#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
export AROS_TEST_ARTIFACT_ROOT=$PWD/build-audit
export AROS_TEST_ISO=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/distfiles/aros-smp-pc-x86_64-20261022-alpha18c5.iso
export AROS_TEST_DOS_OVERLAY=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/fat-replay.k72bijo1
export AROS_TEST_ATA_OVERLAY=$PWD/build-audit/legacy-base
export AROS_TEST_POSIX_LIBRARY=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/posix-source.krrt6d62/posixc.library
export AROS_TEST_ZLIB_LIBRARY=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/bin/pc-x86_64-smp/AROS/Libs/z1.library
export AROS_TEST_REQUIRE_ZVOL=1
bash aros/zpl/test-qemu-default-quota.sh "$PWD/build-audit/legacy-accounting-client/OpenZFSDefaultQuotaTest"
