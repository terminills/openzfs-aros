#!/usr/bin/env bash
set -euo pipefail
cd /mnt/short-term/openzfs-bootenv-candidate-20261025
[[ ${AROS_TEST_OWNED_QEMU_SLOT:-} == 'qemu boot slot' || ${AROS_TEST_OWNED_QEMU_SLOT:-} == 'qemu codex slot 2' ]] || {
    echo 'Claim and verify a Codex relay slot before launch' >&2;exit 1;
}
if pgrep -f '^qemu-system-.*-name openzfs-|^/usr/bin/qemu-system-.*-name openzfs-' >/dev/null;then
    echo 'An OpenZFS QEMU already exists; refusing duplicate launch' >&2;exit 1
fi
export AROS_TEST_ARTIFACT_ROOT="$PWD/build-audit"
export AROS_SDK=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/bin/pc-x86_64-smp/AROS/Developer
export AROS_TEST_ISO=/home/terminills/Documents/GitHub/aros-adt-pc-x86_64-clean/distfiles/aros-smp-pc-x86_64-20261022-alpha18c5.iso
export AROS_TEST_BASE_PLATFORM=1
export AROS_TEST_BASE_DOS_PACKAGE=/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/fat-replay.k72bijo1/aros-base.pkg.xz
export AROS_PACKAGE_TOOL=/home/terminills/Documents/GitHub/AROS-main-adt-compare/tools/package/pkg
export AROS_TEST_POSIX_LIBRARY="$PWD/build-audit/posix-flush-source.j099bkwp/posixc.library"
export AROS_TEST_STDC_LIBRARY="$PWD/build-audit/stdc-source.px9y2jvo/stdc.library"
export AROS_TEST_STDCIO_LIBRARY="$PWD/build-audit/stdcio-source.uo0iplm_/stdcio.library"
export AROS_TEST_DOS64_LIBRARY="$PWD/build-audit/dos64-source.jn47era4/dos64.library"
export AROS_TEST_SHARED_BUILD="$PWD/build-audit/current-service.RarHl71z"
export AROS_TEST_SERVICE_BINARY="$AROS_TEST_SHARED_BUILD/OpenZFSServiceNative"
export AROS_TEST_CLIENT_BINARY="$PWD/build-audit/dependency-client.V4Xt4xQS/OpenZFSPublicServiceTest"
export AROS_TEST_CLI_DIR="$PWD/build-audit/mmake-cli.hG2vWP1T/stage/C"
export AROS_TEST_MANAGER_BINARY="$PWD/build-audit/mmake-cli.hG2vWP1T/stage/Tools/OpenZFSManagerNative"
export AROS_TEST_HANDLER_BINARY="$PWD/build-audit/handler-build.jxfoSGcL/OpenZFS-Handler"
export AROS_TEST_DEVICE_BINARY="$AROS_TEST_CLI_DIR/OpenZFSDevice"
export AROS_TEST_AFS_HANDLER="$PWD/build-audit/afs-source.wg6ei6nj/afs-handler"
export AROS_TEST_COMPANION_TOOLS=1 AROS_TEST_REQUIRE_ERRNO=1
export AROS_TEST_CPUS=8 AROS_TEST_REQUIRE_BOOTENV=1 AROS_TEST_REQUIRE_ACCOUNTING_UPGRADE=1
export AROS_TEST_CAPTURE_FAILURE=1
bash aros/engine/test-qemu-public-service.sh
