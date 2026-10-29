#!/usr/bin/env bash
set -euo pipefail
work=/run/media/terminills/Amiwest/openzfs-source-replay-20261028
qualified=/mnt/short-term/openzfs-bootenv-candidate-20261025/build-audit
cd "$work/source"
[[ ${AROS_TEST_OWNED_QEMU_SLOT:-} == 'qemu boot slot' ]]
if pgrep -f '^qemu-system-.*-name openzfs-|^/usr/bin/qemu-system-.*-name openzfs-' >/dev/null;then exit 1;fi
source "$work/build/exported-dependencies.jjAcgPAs/dependencies.env"
export AROS_ARTIFACT_ROOT="$work/build"
export AROS_TEST_ISO=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/distfiles/aros-smp-pc-x86_64-20261022-alpha18c5.iso
export AROS_TEST_BASE_DOS_PACKAGE=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/fat-replay.k72bijo1/aros-base.pkg.xz
export AROS_TEST_POSIX_LIBRARY="$qualified/posix-flush-source.j099bkwp/posixc.library"
export AROS_TEST_STDC_LIBRARY="$qualified/stdc-source.px9y2jvo/stdc.library"
export AROS_TEST_STDCIO_LIBRARY="$qualified/stdcio-source.uo0iplm_/stdcio.library"
export AROS_TEST_DOS64_LIBRARY="$qualified/dos64-source.jn47era4/dos64.library"
export AROS_TEST_ZDB_BINARY="$work/build/shared-engine.ld5CZgji/zdb"
export AROS_TEST_ZDB_LIBRARY="$work/build/shared-engine.ld5CZgji/openzfs.library"
export AROS_TEST_ZDB_INCLUDE="$work/build/shared-engine.ld5CZgji/include"
export AROS_TEST_ZDB_MEMBER="$qualified/native-import-run.NI5sF03m/topology-members/member0"
export AROS_TEST_ZDB_POOL_SECOND="$qualified/native-import-run.NI5sF03m/topology-members/member2"
export AROS_TEST_HOST_ZDB="$qualified/host-zdb-overlay.a4R8NlmV/zdb"
export AROS_TEST_ZDB_L2ARC_LIBNVPAIR=/home/terminills/Documents/GitHub/openzfs-aros/build-control/.libs/libnvpair.so
export AROS_TEST_ZDB_L2ARC_LOGS=1 AROS_TEST_ZDB_L2ARC_BOUNDS=1
export AROS_TEST_ZDB_BAD_LABELS=1 AROS_TEST_ZDB_DEVICE=1
export AROS_TEST_ZDB_PARTITIONS=1 AROS_TEST_ZDB_PARTITION_TABLE=gpt AROS_TEST_ZDB_NATIVE_POOL=1
export AROS_TEST_FS_PACKAGE="$qualified/partition-gpt.YGZVq2U0/aros-fs.pkg.xz"
bash aros/cli/test-zdb-labels.sh
