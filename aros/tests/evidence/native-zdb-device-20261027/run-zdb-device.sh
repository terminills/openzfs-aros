#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source build-audit/dependencies.YF6P1Tca/dependencies.env
export AROS_ARTIFACT_ROOT="$PWD/build-audit"
export AROS_TEST_ISO=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/distfiles/aros-smp-pc-x86_64-20261022-alpha18c5.iso
export AROS_TEST_BASE_DOS_PACKAGE=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/fat-replay.k72bijo1/aros-base.pkg.xz
export AROS_TEST_POSIX_LIBRARY=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/posix-source.krrt6d62/posixc.library
export AROS_TEST_ZDB_BINARY="$PWD/build-audit/mmake-debugger.lg9uITeY/stage/C/zdb"
export AROS_TEST_ZDB_LIBRARY="$PWD/build-audit/mmake-debugger.lg9uITeY/stage/Libs/openzfs.library"
export AROS_TEST_ZDB_INCLUDE="$PWD/build-audit/mmake-debugger.lg9uITeY/gen/openzfs/aros/library/openzfs/include"
export AROS_TEST_ZDB_MEMBER="$PWD/build-audit/native-import-run.NI5sF03m/topology-members/member0"
export AROS_TEST_ZDB_POOL_SECOND="$PWD/build-audit/native-import-run.NI5sF03m/topology-members/member2"
export AROS_TEST_HOST_ZDB=/home/terminills/Documents/GitHub/openzfs-aros/build-control/zdb
export AROS_TEST_ZDB_BAD_LABELS=1 AROS_TEST_ZDB_DEVICE=1
bash aros/cli/test-zdb-labels.sh
