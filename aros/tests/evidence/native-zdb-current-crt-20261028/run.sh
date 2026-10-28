#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
source build-audit/dependencies.YF6P1Tca/dependencies.env
export AROS_ARTIFACT_ROOT="$PWD/build-audit"
export AROS_TEST_ISO=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/distfiles/aros-smp-pc-x86_64-20261022-alpha18c5.iso
export AROS_TEST_BASE_DOS_PACKAGE=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/fat-replay.k72bijo1/aros-base.pkg.xz
export AROS_TEST_POSIX_LIBRARY="$PWD/build-audit/posix-flush-source.j099bkwp/posixc.library"
export AROS_TEST_STDC_LIBRARY="$PWD/build-audit/stdc-source.px9y2jvo/stdc.library"
export AROS_TEST_STDCIO_LIBRARY="$PWD/build-audit/stdcio-source.uo0iplm_/stdcio.library"
export AROS_TEST_DOS64_LIBRARY="$PWD/build-audit/dos64-source.jn47era4/dos64.library"
export AROS_TEST_ZDB_BINARY="$PWD/build-audit/mmake-debugger.3zlIqhLI/stage/C/zdb"
export AROS_TEST_ZDB_LIBRARY="$PWD/build-audit/mmake-debugger.3zlIqhLI/stage/Libs/openzfs.library"
export AROS_TEST_ZDB_INCLUDE="$PWD/build-audit/mmake-debugger.3zlIqhLI/gen/openzfs/aros/library/openzfs/include"
export AROS_TEST_ZDB_MEMBER="$PWD/build-audit/native-import-run.NI5sF03m/topology-members/member0"
export AROS_TEST_HOST_ZDB="$PWD/build-audit/host-zdb-overlay.a4R8NlmV/zdb"
export AROS_TEST_ZDB_L2ARC_LIBNVPAIR=/home/terminills/Documents/GitHub/openzfs-aros/build-control/.libs/libnvpair.so
export AROS_TEST_ZDB_L2ARC_LOGS=1 AROS_TEST_ZDB_L2ARC_BOUNDS=1
export AROS_TEST_ZDB_BAD_LABELS=1 AROS_TEST_ZDB_DEVICE=1
bash aros/cli/test-zdb-labels.sh
